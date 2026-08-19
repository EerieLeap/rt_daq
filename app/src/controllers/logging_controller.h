#pragma once

#include <memory>

#include "domain/logging_domain/services/log_writer_service.h"
#include "domain/sensor_domain/configuration/sensors_configuration_manager.h"
#include "domain/logging_domain/loggers/mdf4_logger_sensor_reading.h"
#include "domain/logging_domain/configuration/logging_configuration_manager.h"
#include "domain/canbus_com_domain/services/canbus_com_service.h"
#include "controllers/display_controller.h"

namespace eerie_leap::controllers {

using eerie_leap::domain::sensor_domain::configuration::SensorsConfigurationManager;
using eerie_leap::domain::logging_domain::configuration::LoggingConfigurationManager;
using eerie_leap::domain::logging_domain::services::LogWriterService;
using eerie_leap::domain::logging_domain::loggers::Mdf4LoggerSensorReading;
using eerie_leap::domain::canbus_com_domain::services::CanbusComService;

class LoggingController {
private:
    std::shared_ptr<LogWriterService> log_writer_service_;
    std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager_;
    std::shared_ptr<LoggingConfigurationManager> logging_configuration_manager_;
    std::shared_ptr<CanbusComService> canbus_com_service_;
    std::shared_ptr<DisplayController> display_controller_;
    // NOTE: Recreate on sensor configuration change
    std::shared_ptr<Mdf4LoggerSensorReading> logger_;

public:
    LoggingController(
        std::shared_ptr<LogWriterService> log_writer_service,
        std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager,
        std::shared_ptr<LoggingConfigurationManager> logging_configuration_manager,
        std::shared_ptr<CanbusComService> canbus_com_service,
        std::shared_ptr<DisplayController> display_controller);

    bool LogWriterStart();
    bool LogWriterStop();
};

} // namespace eerie_leap::controllers
