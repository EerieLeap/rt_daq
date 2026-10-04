#pragma once

#include <memory>

#include "subsys/fs/services/i_fs_service.h"
#include "subsys/threading/work_queue_thread.h"
#include "subsys/time/i_time_service.h"

#include "domain/configuration_domain/services/configuration_service.h"
#include "domain/sensor_domain/configuration/sensors_configuration_manager.h"
#include "domain/sensor_domain/utilities/sensor_readings_frame.hpp"
#include "domain/logging_domain/configuration/logging_configuration_manager.h"
#include "domain/logging_domain/services/log_writer_service.h"
#include "domain/logging_domain/loggers/mdf4_logger_sensor_reading.h"
#include "domain/canbus_com_domain/services/canbus_com_service.h"
#include "controllers/display_controller.h"

namespace eerie_leap::controllers {

using eerie_leap::subsys::fs::services::IFsService;
using eerie_leap::subsys::threading::WorkQueueThread;
using eerie_leap::subsys::time::ITimeService;

using eerie_leap::domain::configuration_domain::services::ConfigurationService;
using eerie_leap::domain::sensor_domain::configuration::SensorsConfigurationManager;
using eerie_leap::domain::sensor_domain::utilities::SensorReadingsFrame;
using eerie_leap::domain::logging_domain::configuration::LoggingConfigurationManager;
using eerie_leap::domain::logging_domain::services::LogWriterService;
using eerie_leap::domain::logging_domain::loggers::Mdf4LoggerSensorReading;
using eerie_leap::domain::canbus_com_domain::services::CanbusComService;

class LoggingController {
private:
    static constexpr const char* LOGGING_CONFIGURATION_NAME = "logging_config";

    std::shared_ptr<IFsService> fs_service_;
    std::shared_ptr<WorkQueueThread> config_work_queue_thread_;
    std::shared_ptr<ConfigurationService> configuration_service_;
    std::shared_ptr<IFsService> sd_fs_service_;
    std::shared_ptr<ITimeService> time_service_;
    std::shared_ptr<SensorReadingsFrame> sensor_readings_frame_;
    std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager_;
    std::shared_ptr<CanbusComService> canbus_com_service_;
    std::shared_ptr<DisplayController> display_controller_;

    std::shared_ptr<LoggingConfigurationManager> logging_configuration_manager_;
    std::shared_ptr<LogWriterService> log_writer_service_;
    // NOTE: Recreate on sensor configuration change
    std::shared_ptr<Mdf4LoggerSensorReading> logger_;

public:
    LoggingController(
        std::shared_ptr<IFsService> fs_service,
        std::shared_ptr<WorkQueueThread> config_work_queue_thread,
        std::shared_ptr<ConfigurationService> configuration_service,
        std::shared_ptr<IFsService> sd_fs_service,
        std::shared_ptr<ITimeService> time_service,
        std::shared_ptr<SensorReadingsFrame> sensor_readings_frame,
        std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager,
        std::shared_ptr<CanbusComService> canbus_com_service,
        std::shared_ptr<DisplayController> display_controller);

    int Initialize();

    bool LogWriterStart();
    bool LogWriterStop();

    std::shared_ptr<LoggingConfigurationManager> GetConfigurationManager() const { return logging_configuration_manager_; }
};

} // namespace eerie_leap::controllers
