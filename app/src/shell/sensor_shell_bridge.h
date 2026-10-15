#pragma once

#include <memory>

#include "subsys/threading/work_queue_thread.h"
#include "subsys/time/i_time_service.h"
#include "domain/sensor_domain/configuration/sensors_configuration_manager.h"
#include "domain/sensor_domain/services/sensors_processing_service.h"
#include "domain/sensor_domain/utilities/sensor_readings_frame.hpp"

namespace eerie_leap::shell {

// Bench aid: 'sensor set <id> <value>' commits a reading to the readings frame from the shell, so a
// bench drives triggers and the CAN messages that carry a sensor without an input. The commit runs
// on the configuration work queue, serialized with a sensors apply and a processing restart, and
// only while processing runs; a restart drops the value. 'sensor get <id>' prints the latest
// reading. Built with CONFIG_APP_SENSOR_SHELL only.
void AttachSensorShell(
    std::shared_ptr<eerie_leap::domain::sensor_domain::utilities::SensorReadingsFrame> sensor_readings_frame,
    std::shared_ptr<eerie_leap::domain::sensor_domain::configuration::SensorsConfigurationManager> sensors_configuration_manager,
    std::shared_ptr<eerie_leap::domain::sensor_domain::services::SensorsProcessingService> sensors_processing_service,
    std::shared_ptr<eerie_leap::subsys::time::ITimeService> time_service,
    std::shared_ptr<eerie_leap::subsys::threading::WorkQueueThread> config_work_queue_thread);

} // namespace eerie_leap::shell
