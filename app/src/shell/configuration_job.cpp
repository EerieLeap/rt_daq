#if defined(CONFIG_APP_LOGGING_SHELL) || defined(CONFIG_APP_SENSOR_SHELL)

#include <cerrno>
#include <exception>
#include <memory>

#include <zephyr/kernel.h>

#include "configuration_job.h"

namespace eerie_leap::shell {

namespace {

constexpr int32_t CONFIGURATION_JOB_TIMEOUT_S = 10;

struct JobResult {
    k_sem done;
    int code = -ETIMEDOUT;
    std::string message;
};

} // namespace

int RunConfigurationJob(
    const struct shell* sh,
    eerie_leap::subsys::threading::WorkQueueThread& config_work_queue_thread,
    std::function<int(std::string&)> job) {

    auto result = std::make_shared<JobResult>();
    k_sem_init(&result->done, 0, 1);

    config_work_queue_thread.Run([result, job = std::move(job)] {
        try {
            result->code = job(result->message);
        } catch(const std::exception& e) {
            result->code = -EIO;
            result->message = e.what();
        } catch(...) {
            result->code = -EIO;
            result->message = "Unknown error.";
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

} // namespace eerie_leap::shell

#endif // CONFIG_APP_LOGGING_SHELL || CONFIG_APP_SENSOR_SHELL
