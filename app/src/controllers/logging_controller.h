#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

#include <zephyr/kernel.h>

#include "subsys/event_bus/i_scoped_subscription.h"
#include "subsys/fs/services/i_fs_service.h"
#include "subsys/threading/i_service.h"
#include "subsys/threading/scoped_mutex.h"
#include "subsys/threading/work_queue_thread.h"
#include "subsys/time/i_time_service.h"

#include "domain/configuration_domain/services/configuration_service.h"
#include "domain/sensor_domain/configuration/sensors_configuration_manager.h"
#include "domain/sensor_domain/utilities/sensor_readings_frame.hpp"
#include "domain/logging_domain/configuration/logging_configuration_manager.h"
#include "domain/logging_domain/services/log_writer_service.h"
#include "domain/logging_domain/loggers/logger_metadata.h"
#include "domain/logging_domain/loggers/mdf4_logger_sensor_reading.h"
#include "domain/logging_domain/event_bus/logging_network_state.h"
#include "domain/canbus_com_domain/services/canbus_com_service.h"
#include "domain/canbus_com_domain/state_bridge/network_state_exporter.h"
#include "controllers/display_controller.h"

namespace eerie_leap::controllers {

using eerie_leap::subsys::event_bus::AnySubscription;
using eerie_leap::subsys::fs::services::IFsService;
using eerie_leap::subsys::threading::IService;
using eerie_leap::subsys::threading::WorkQueueTask;
using eerie_leap::subsys::threading::WorkQueueThread;
using eerie_leap::subsys::time::ITimeService;

using eerie_leap::domain::configuration_domain::services::ConfigurationService;
using eerie_leap::domain::sensor_domain::configuration::SensorsConfigurationManager;
using eerie_leap::domain::sensor_domain::utilities::SensorReadingsFrame;
using eerie_leap::domain::logging_domain::configuration::LoggingConfigurationManager;
using eerie_leap::domain::logging_domain::services::LogWriterService;
using eerie_leap::domain::logging_domain::loggers::LoggerMetadata;
using eerie_leap::domain::logging_domain::loggers::Mdf4LoggerSensorReading;
using eerie_leap::domain::logging_domain::event_bus::LoggingNetworkState;
using eerie_leap::domain::canbus_com_domain::services::CanbusComService;
using eerie_leap::domain::canbus_com_domain::state_bridge::NetworkStateExporter;

class LoggingController {
private:
    static constexpr const char* LOGGING_CONFIGURATION_NAME = "logging_config";

    using LoggingStatusExporter = NetworkStateExporter<LoggingNetworkState>;

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

    // Starts, stops and restarts come from the CDMP handler, the shell, the autostart hook and the
    // configuration work queue: one at a time, so a restart cannot bring back a writer that a stop
    // has just stopped.
    k_mutex start_stop_mutex_;
    // Set by a start, cleared by a stop or a failure: whether a restart brings the writer back.
    // Under start_stop_mutex_.
    bool is_logging_requested_ = false;

    // The log writer as a dependent of sensor processing; see GetDependentService().
    class DependentLogWriter;
    std::shared_ptr<DependentLogWriter> dependent_log_writer_;

    // Both run on the configuration work queue.
    std::optional<WorkQueueTask<LoggingController>> restart_task_;
    std::optional<WorkQueueTask<LoggingController>> status_task_;

    AnySubscription status_subscription_;
    // The writer's last published status; the display follows it.
    std::atomic<bool> is_logging_active_ = false;
    std::atomic<uint32_t> last_inactive_reason_ = 0;
    // Configuration work queue only.
    bool is_log_status_shown_ = false;

    // Shares the writer's status with the other units on the CDMP network; none without the COM
    // service. Its destruction withdraws the status.
    std::unique_ptr<LoggingStatusExporter> logging_status_exporter_;

    // The log writer's factory, run at every start, so a file always describes the configurations
    // and the CDMP device ID in effect when it was opened.
    std::shared_ptr<Mdf4LoggerSensorReading> CreateLogger() const;
    LoggerMetadata CreateLoggerMetadata() const;

    void RestartLogWriter();
    void StopIfFailed();
    void UpdateStatus();

    bool StopForProcessingRestart();
    bool StartAfterProcessingRestart();

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
    ~LoggingController();

    int Initialize();

    bool LogWriterStart();
    bool LogWriterStop();

    /**
     * What to register with SensorsController::RegisterDependentService(): the log writer, stopped
     * and started around a restart of sensor processing under the same lock as LogWriterStart() and
     * LogWriterStop(), and started again only when logging is still wanted and did not fail.
     */
    std::shared_ptr<IService> GetDependentService() const;

    std::shared_ptr<LoggingConfigurationManager> GetConfigurationManager() const { return logging_configuration_manager_; }
    std::shared_ptr<LogWriterService> GetLogWriterService() const { return log_writer_service_; }
};

} // namespace eerie_leap::controllers
