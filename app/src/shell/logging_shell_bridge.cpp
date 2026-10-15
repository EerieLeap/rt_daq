#ifdef CONFIG_APP_LOGGING_SHELL

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <string>

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/shell/shell.h>

#include "utilities/memory/memory_resource_manager.h"
#include "subsys/device_tree/dt_fs.h"
#include "domain/logging_domain/control/i_logging_control.h"
#include "domain/logging_domain/models/logging_configuration.h"

#include "logging_shell_bridge.h"

namespace eerie_leap::shell {

using eerie_leap::utilities::memory::Mrm;
using eerie_leap::subsys::threading::WorkQueueThread;
using eerie_leap::controllers::LoggingController;
using eerie_leap::domain::sensor_domain::configuration::SensorsConfigurationManager;
using eerie_leap::domain::logging_domain::control::ILoggingControl;
using eerie_leap::domain::logging_domain::models::LoggingConfiguration;

namespace {

constexpr int32_t CONFIGURATION_JOB_TIMEOUT_S = 10;

std::shared_ptr<LoggingController> logging_controller;
std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager;
std::shared_ptr<WorkQueueThread> config_work_queue_thread;

// Null, with the reason printed, when there is no controller to ask.
std::shared_ptr<ILoggingControl> GetControl(const struct shell* sh) {
    auto control = logging_controller != nullptr ? logging_controller->GetControl() : nullptr;
    if(control == nullptr)
        shell_error(sh, "No logging controller.");

    return control;
}

bool ParseUint32(const char* text, uint32_t& value) {
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);

    if(errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX)
        return false;

    value = static_cast<uint32_t>(parsed);

    return true;
}

struct JobResult {
    k_sem done;
    int code = -ETIMEDOUT;
    std::string message;
};

// Runs @p job on the configuration work queue, where an SMP apply runs: the parsing and validation
// get that queue's stack rather than the shell's, and the restart the stored configuration
// triggers is queued behind the job, as it is behind an apply. The job reports through the result,
// which the shell thread prints; it outlives a timeout.
int RunConfigurationJob(const struct shell* sh, std::function<int(std::string&)> job) {
    auto result = std::make_shared<JobResult>();
    k_sem_init(&result->done, 0, 1);

    config_work_queue_thread->Run([result, job = std::move(job)] {
        try {
            result->code = job(result->message);
        } catch(const std::exception& e) {
            result->code = -EIO;
            result->message = e.what();
        }

        k_sem_give(&result->done);
    });

    if(k_sem_take(&result->done, K_SECONDS(CONFIGURATION_JOB_TIMEOUT_S)) != 0) {
        shell_error(sh, "The configuration work queue did not run the job within %d s.", CONFIGURATION_JOB_TIMEOUT_S);
        return -ETIMEDOUT;
    }

    if(result->code != 0)
        shell_error(sh, "%s", result->message.c_str());
    else
        shell_print(sh, "%s", result->message.c_str());

    return result->code;
}

} // namespace

void AttachLoggingShell(
    std::shared_ptr<LoggingController> controller,
    std::shared_ptr<SensorsConfigurationManager> sensors_manager,
    std::shared_ptr<WorkQueueThread> work_queue_thread) {

    logging_controller = std::move(controller);
    sensors_configuration_manager = std::move(sensors_manager);
    config_work_queue_thread = std::move(work_queue_thread);
}

} // namespace eerie_leap::shell

// The command table itself is C (app/src/shell/logging_shell.c); these are its handlers.
// A request neither throws nor waits: the supervisor starts or stops the writer on the configuration
// work queue, and 'logging status' shows the outcome.
extern "C" int app_logging_shell_start(const struct shell* sh) {
    auto control = eerie_leap::shell::GetControl(sh);
    if(control == nullptr)
        return -ENODEV;

    control->RequestStart();
    shell_print(sh, "Logging start requested; 'logging status' shows the outcome.");

    return 0;
}

extern "C" int app_logging_shell_stop(const struct shell* sh) {
    auto control = eerie_leap::shell::GetControl(sh);
    if(control == nullptr)
        return -ENODEV;

    control->RequestStop();
    shell_print(sh, "Logging stop requested; 'logging status' shows the outcome.");

    return 0;
}

extern "C" int app_logging_shell_status(const struct shell* sh) {
    auto control = eerie_leap::shell::GetControl(sh);
    if(control == nullptr)
        return -ENODEV;

    auto service = eerie_leap::shell::logging_controller->GetLogWriterService();
    if(!service) {
        shell_error(sh, "No log writer service.");
        return -ENODEV;
    }

    const auto stats = service->GetStats();
    shell_print(sh, "active: %s, running: %s, failed: %s",
        control->IsActive() ? "yes" : "no", service->IsRunning() ? "yes" : "no", service->HasFailed() ? "yes" : "no");
    shell_print(sh, "ticks %u, readings %u, frames %u (dropped %u), bytes %llu, writes %u, syncs %u, rotations %u, overruns %u, errors %u, tick last/max %u/%u us",
        stats.ticks, stats.readings, stats.frames, stats.frames_dropped,
        static_cast<unsigned long long>(stats.bytes), stats.stream_writes, stats.syncs,
        stats.rotations, stats.overruns, stats.write_errors, stats.tick_last_us, stats.tick_max_us);

    return 0;
}

// Replaces the stored logging configuration's interval and size limit, keeping its sensor entries.
// The updated handler restarts a running writer with it.
extern "C" int app_logging_shell_config(const struct shell* sh, size_t argc, char** argv) {
    using namespace eerie_leap::shell;

    if(!logging_controller || !config_work_queue_thread) {
        shell_error(sh, "No logging controller.");
        return -ENODEV;
    }

    uint32_t interval_ms = 0;
    uint32_t max_log_size_mb = 0;
    if(argc != 3 || !ParseUint32(argv[1], interval_ms) || !ParseUint32(argv[2], max_log_size_mb)) {
        shell_error(sh, "Usage: logging config <interval_ms> <max_log_size_mb>");
        return -EINVAL;
    }

    const auto manager = logging_controller->GetConfigurationManager();

    return RunConfigurationJob(sh, [manager, interval_ms, max_log_size_mb](std::string& message) {
        const auto current = manager->Get();
        if(current == nullptr) {
            message = "No logging configuration is available.";
            return -ENODATA;
        }

        // What restores it: printed whether or not the new one is stored.
        const std::string previous = "Previous: logging config " + std::to_string(current->logging_interval_ms)
            + " " + std::to_string(current->max_log_size_mb);

        LoggingConfiguration configuration(std::allocator_arg, Mrm::GetExtPmr());
        configuration = *current;
        configuration.logging_interval_ms = interval_ms;
        configuration.max_log_size_mb = max_log_size_mb;

        if(!manager->Update(configuration)) {
            message = previous + "\nThe configuration was rejected or not stored; see the log.";
            return -EIO;
        }

        message = previous + "\nStored; a running log writer restarts with it.";
        return 0;
    });
}

// Applies the stored sensors configuration again through ApplyCborConfiguration(), as an SMP apply
// of the same bytes would: processing restarts, and with it a running log writer.
extern "C" int app_logging_shell_reapply_sensors(const struct shell* sh) {
    using namespace eerie_leap::shell;

    if(!sensors_configuration_manager || !config_work_queue_thread) {
        shell_error(sh, "No sensors configuration manager.");
        return -ENODEV;
    }

    const auto manager = sensors_configuration_manager;

    return RunConfigurationJob(sh, [manager](std::string& message) {
        const auto cbor = manager->GetCborConfiguration();
        if(cbor.empty()) {
            message = "No stored sensors configuration.";
            return -ENODATA;
        }

        char reason[96] = {};
        if(!manager->ApplyCborConfiguration(cbor, reason)) {
            message = std::string("The stored sensors configuration did not apply: ") + reason;
            return -EIO;
        }

        message = "Applied " + std::to_string(cbor.size()) + " bytes; processing restarts, and with it a running log writer.";
        return 0;
    });
}

// Unmounting the card under an open file makes the writer's next write or sync fail, which is
// what a pulled card does at the file system layer; the disk is powered off, so reboot to recover.
extern "C" int app_logging_shell_unmount_sd(const struct shell* sh) {
    auto* mount = eerie_leap::subsys::device_tree::DtFs::GetSdFsMp();
    if(mount == nullptr) {
        shell_error(sh, "No SD card mount.");
        return -ENODEV;
    }

    const int rc = fs_unmount(mount);
    if(rc != 0) {
        shell_error(sh, "Unmount failed: %d", rc);
        return rc;
    }

    shell_print(sh, "SD card unmounted; reboot to mount it again.");

    return 0;
}

#endif // CONFIG_APP_LOGGING_SHELL
