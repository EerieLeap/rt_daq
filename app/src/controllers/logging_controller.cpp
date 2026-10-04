#include <zephyr/logging/log.h>

#include "configuration/cbor/cbor_logging_config/cbor_logging_config.h"
#include "configuration/services/cbor_configuration_service.h"
#include "domain/canbus_com_domain/commands/canbus_com_logging_command.h"

#include "logging_controller.h"

namespace eerie_leap::controllers {

using namespace eerie_leap::domain::canbus_com_domain::commands;

namespace config_services = eerie_leap::configuration::services;

LOG_MODULE_REGISTER(logging_controller_logger);

LoggingController::LoggingController(
    std::shared_ptr<IFsService> fs_service,
    std::shared_ptr<WorkQueueThread> config_work_queue_thread,
    std::shared_ptr<ConfigurationService> configuration_service,
    std::shared_ptr<IFsService> sd_fs_service,
    std::shared_ptr<ITimeService> time_service,
    std::shared_ptr<SensorReadingsFrame> sensor_readings_frame,
    std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager,
    std::shared_ptr<CanbusComService> canbus_com_service,
    std::shared_ptr<DisplayController> display_controller)
        : fs_service_(std::move(fs_service)),
        config_work_queue_thread_(std::move(config_work_queue_thread)),
        configuration_service_(std::move(configuration_service)),
        sd_fs_service_(std::move(sd_fs_service)),
        time_service_(std::move(time_service)),
        sensor_readings_frame_(std::move(sensor_readings_frame)),
        sensors_configuration_manager_(std::move(sensors_configuration_manager)),
        canbus_com_service_(std::move(canbus_com_service)),
        display_controller_(std::move(display_controller)) {}

int LoggingController::Initialize() {
    auto cbor_logging_config_service = std::make_unique<config_services::CborConfigurationService<CborLoggingConfig>>(
        LOGGING_CONFIGURATION_NAME, fs_service_, config_work_queue_thread_);
    logging_configuration_manager_ = std::make_shared<LoggingConfigurationManager>(
        std::move(cbor_logging_config_service));

    if(configuration_service_ != nullptr)
        configuration_service_->RegisterCborConfigurationManager(
            ConfigurationService::Type::Logging, logging_configuration_manager_);

    log_writer_service_ = std::make_shared<LogWriterService>(
        sd_fs_service_,
        logging_configuration_manager_,
        time_service_,
        sensor_readings_frame_);
    if(!log_writer_service_->Initialize()) {
        LOG_ERR("Failed to initialize the log writer service.");
        return -1;
    }

    logger_ = std::make_shared<Mdf4LoggerSensorReading>(
        logging_configuration_manager_,
        *sensors_configuration_manager_->Get());
    log_writer_service_->SetLogger(logger_);

    canbus_com_service_->SetCommandHandler<CanbusComLoggingCommand>(
        CanbusComCommandCode::LOGGING,
        [this](std::optional<CanbusComLoggingCommand> command) {
            if(!command.has_value())
                return false;

            if(command.value().IsStart())
                return LogWriterStart();
            else
                return LogWriterStop();
        });

    return 0;
}

bool LoggingController::LogWriterStart() {
    if(log_writer_service_->IsRunning())
        return true;

    bool started = log_writer_service_->Start();

    if(started)
        display_controller_->AddStatus("log");

    return started;
}

bool LoggingController::LogWriterStop() {
    if(!log_writer_service_->IsRunning())
        return true;

    bool stopped = log_writer_service_->Stop();

    if(stopped)
        display_controller_->RemoveStatus("log");

    return stopped;
}

} // namespace eerie_leap::controllers
