#pragma once

#include <memory>

#include "controllers/logging_controller.h"
#include "domain/sensor_domain/configuration/sensors_configuration_manager.h"
#include "subsys/threading/work_queue_thread.h"

namespace eerie_leap::shell {

// Bench aid: 'logging start|stop|status' on the Zephyr shell stands in for the CDMP LOGGING
// command when no CAN peer is connected; 'logging config' and 'logging reapply-sensors' store a
// configuration the way an SMP apply does, on the configuration work queue, so the restarts they
// trigger can be exercised without an SMP client. Built with CONFIG_APP_LOGGING_SHELL only.
void AttachLoggingShell(
    std::shared_ptr<eerie_leap::controllers::LoggingController> controller,
    std::shared_ptr<eerie_leap::domain::sensor_domain::configuration::SensorsConfigurationManager>
        sensors_configuration_manager,
    std::shared_ptr<eerie_leap::subsys::threading::WorkQueueThread> config_work_queue_thread
);

} // namespace eerie_leap::shell
