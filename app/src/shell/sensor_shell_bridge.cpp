#ifdef CONFIG_APP_SENSOR_SHELL

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "domain/sensor_domain/models/reading_error.h"
#include "domain/sensor_domain/models/reading_source.h"
#include "domain/sensor_domain/models/reading_status.h"
#include "domain/sensor_domain/models/sensor_reading.h"
#include "domain/sensor_domain/models/sensor_reading_update_method.h"
#include "domain/sensor_domain/models/sensor_type_traits.h"

#include "configuration_job.h"
#include "sensor_shell_bridge.h"

namespace eerie_leap::shell {

using eerie_leap::subsys::threading::WorkQueueThread;
using eerie_leap::subsys::time::ITimeService;
using eerie_leap::domain::sensor_domain::configuration::SensorsConfigurationManager;
using eerie_leap::domain::sensor_domain::models::ReadingSource;
using eerie_leap::domain::sensor_domain::models::ReadingStatus;
using eerie_leap::domain::sensor_domain::models::SensorReading;
using eerie_leap::domain::sensor_domain::models::SensorReadingUpdateMethod;
using eerie_leap::domain::sensor_domain::models::SensorValueKind;
using eerie_leap::domain::sensor_domain::services::SensorsProcessingService;
using eerie_leap::domain::sensor_domain::utilities::SensorReadingsFrame;

namespace {

std::shared_ptr<SensorReadingsFrame> sensor_readings_frame;
std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager;
std::shared_ptr<SensorsProcessingService> sensors_processing_service;
std::shared_ptr<ITimeService> time_service;
std::shared_ptr<WorkQueueThread> config_work_queue_thread;

bool IsAttached() {
    return sensor_readings_frame && sensors_configuration_manager && sensors_processing_service
        && time_service && config_work_queue_thread;
}

bool ParseFiniteFloat(const char* text, float& value) {
    char* end = nullptr;
    const float parsed = strtof(text, &end);

    if(end == text || *end != '\0' || !std::isfinite(parsed))
        return false;

    value = parsed;

    return true;
}

// The shortest text, of 6 to 9 significant digits, that reads back as the value: 1, 0.5, 12.25.
std::string FormatValue(float value) {
    char text[32];
    for(int digits = 6; digits <= 9; ++digits) {
        snprintf(text, sizeof(text), "%.*g", digits, static_cast<double>(value));
        if(strtof(text, nullptr) == value)
            break;
    }

    return text;
}

const char* ToString(ReadingStatus status) {
    switch(status) {
        case ReadingStatus::UNINITIALIZED: return "UNINITIALIZED";
        case ReadingStatus::RAW: return "RAW";
        case ReadingStatus::INTERPOLATED: return "INTERPOLATED";
        case ReadingStatus::EXPRESSION_EVALUATED: return "EXPRESSION_EVALUATED";
        case ReadingStatus::PROCESSED: return "PROCESSED";
        case ReadingStatus::ERROR: return "ERROR";
    }

    return "?";
}

// Runs @p handler so that nothing escapes into the C shell.
int Guarded(const struct shell* sh, const std::function<int()>& handler) {
    try {
        return handler();
    } catch(const std::exception& e) {
        shell_error(sh, "%s", e.what());
    } catch(...) {
        shell_error(sh, "Unknown error.");
    }

    return -EIO;
}

} // namespace

void AttachSensorShell(
    std::shared_ptr<SensorReadingsFrame> frame,
    std::shared_ptr<SensorsConfigurationManager> sensors_manager,
    std::shared_ptr<SensorsProcessingService> processing_service,
    std::shared_ptr<ITimeService> time,
    std::shared_ptr<WorkQueueThread> work_queue_thread) {

    sensor_readings_frame = std::move(frame);
    sensors_configuration_manager = std::move(sensors_manager);
    sensors_processing_service = std::move(processing_service);
    time_service = std::move(time);
    config_work_queue_thread = std::move(work_queue_thread);
}

} // namespace eerie_leap::shell

// The command table itself is C (app/src/shell/sensor_shell.c); these are its handlers.
extern "C" int app_sensor_shell_set(const struct shell* sh, size_t argc, char** argv) {
    using namespace eerie_leap::shell;

    return Guarded(sh, [&]() -> int {
        if(!IsAttached()) {
            shell_error(sh, "Not attached yet.");
            return -ENODEV;
        }

        float value = 0.0F;
        if(argc != 3 || !ParseFiniteFloat(argv[2], value)) {
            shell_error(sh, "Usage: sensor set <id> <value>");
            return -EINVAL;
        }

        const std::string id = argv[1];

        return RunConfigurationJob(sh, *config_work_queue_thread, [id, value](std::string& message) {
            // Only while a generation runs: the frame keeps its slots between a stop and the next
            // start, but the sensors behind them are gone, and the next start drops the value. A
            // restart runs as one job on this queue, so this job never sees one in progress; it
            // refuses a stopped service (before boot starts it, or after a failed start) and a
            // paused one (an ADC calibration).
            if(!sensors_processing_service->IsRunning()) {
                message = "Sensor processing is stopped or paused; set the value again once it runs.";
                return -EAGAIN;
            }

            const auto sensors = sensors_configuration_manager->Get();
            if(sensors == nullptr) {
                message = "No sensors configuration is available.";
                return -ENODATA;
            }

            std::shared_ptr<eerie_leap::domain::sensor_domain::models::Sensor> sensor;
            for(const auto& candidate : *sensors) {
                if(std::string_view(candidate->id) == id) {
                    sensor = candidate;
                    break;
                }
            }

            if(sensor == nullptr) {
                message = "No sensor " + id + " in the sensors configuration.";
                return -ENOENT;
            }

            const auto traits = sensor->configuration.GetTraits();
            // An indicator holds 0 or 1, as the expression processor stores it.
            const float stored =
                traits.value_kind == SensorValueKind::INDICATOR ? (value != 0.0F ? 1.0F : 0.0F) : value;

            SensorReading reading(sensor.get());
            reading.value = stored;
            reading.raw_value = stored;
            reading.timestamp = time_service->GetCurrentTime();
            reading.source = ReadingSource::PROCESSING;
            reading.status = ReadingStatus::PROCESSED;

            if(!sensor_readings_frame->AddOrUpdateReading(reading)) {
                message = id + " is not in the running sensors; a sensors configuration is waiting to be applied.";
                return -EAGAIN;
            }

            message = id + " = " + FormatValue(stored);

            const auto& configuration = sensor->configuration;
            switch(configuration.GetReadingUpdateMethod()) {
                case SensorReadingUpdateMethod::SCHEDULER:
                    message += "\nSampled every " + std::to_string(configuration.sampling_rate_ms.value_or(0))
                        + " ms: its next sample replaces it.";
                    break;
                case SensorReadingUpdateMethod::DEPENDENT:
                    message += "\nEvaluated when an input commits, which replaces it.";
                    break;
                case SensorReadingUpdateMethod::ISR:
                    message += "\nReplaced when its source (a CAN frame or an input edge) next updates.";
                    break;
                case SensorReadingUpdateMethod::NONE:
                    break;
            }

            if(configuration.HasConditioning())
                message += "\nDebounce and Toggle do not apply to a value set here.";
            if(!sensor->is_enabled)
                message += "\nThe sensor is disabled.";

            return 0;
        });
    });
}

extern "C" int app_sensor_shell_get(const struct shell* sh, size_t argc, char** argv) {
    using namespace eerie_leap::shell;

    return Guarded(sh, [&]() -> int {
        if(!IsAttached()) {
            shell_error(sh, "Not attached yet.");
            return -ENODEV;
        }

        if(argc != 2) {
            shell_error(sh, "Usage: sensor get <id>");
            return -EINVAL;
        }

        const auto reading = sensor_readings_frame->TryGetReading(std::string_view(argv[1]));
        if(!reading.has_value()) {
            shell_print(sh, "No reading of %s.", argv[1]);
            return 0;
        }

        if(reading->value.has_value())
            shell_print(sh, "%s = %s", argv[1], FormatValue(*reading->value).c_str());
        else
            shell_print(sh, "%s = none", argv[1]);

        shell_print(sh, "status %s", ToString(reading->status));
        if(reading->HasError())
            shell_print(sh, "error %s",
                std::string(eerie_leap::domain::sensor_domain::models::ToString(reading->error)).c_str());

        if(reading->timestamp.has_value()) {
            const auto age = time_service->GetCurrentTime() - *reading->timestamp;
            shell_print(sh, "age %lld ms",
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(age).count()));
        }

        return 0;
    });
}

#endif // CONFIG_APP_SENSOR_SHELL
