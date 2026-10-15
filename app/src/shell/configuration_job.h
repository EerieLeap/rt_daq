#pragma once

#include <functional>
#include <string>

#include <zephyr/shell/shell.h>

#include "subsys/threading/work_queue_thread.h"

namespace eerie_leap::shell {

// Runs @p job on the configuration work queue, where an SMP apply and a processing restart run: the
// work gets that queue's stack rather than the shell's, and it never lands inside a restart, which is
// queued behind it as it is behind an apply. The job reports a message and returns 0 or an errno;
// the shell thread prints it. A throw is reported, never let out into the shell. The result outlives
// a timeout. Built with CONFIG_APP_LOGGING_SHELL or CONFIG_APP_SENSOR_SHELL.
int RunConfigurationJob(
    const struct shell* sh,
    eerie_leap::subsys::threading::WorkQueueThread& config_work_queue_thread,
    std::function<int(std::string&)> job);

} // namespace eerie_leap::shell
