#include "domain/canbus_com_domain/commands/canbus_com_logging_command.h"

#include "logging_controller.h"

namespace eerie_leap::controllers {

using namespace eerie_leap::domain::canbus_com_domain::commands;

LoggingController::LoggingController(
    std::shared_ptr<LogWriterService> log_writer_service,
    std::shared_ptr<SensorsConfigurationManager> sensors_configuration_manager,
    std::shared_ptr<LoggingConfigurationManager> logging_configuration_manager,
    std::shared_ptr<CanbusComService> canbus_com_service,
    std::shared_ptr<DisplayController> display_controller)
        : log_writer_service_(std::move(log_writer_service)),
        sensors_configuration_manager_(std::move(sensors_configuration_manager)),
        logging_configuration_manager_(std::move(logging_configuration_manager)),
        canbus_com_service_(std::move(canbus_com_service)),
        display_controller_(std::move(display_controller)) {

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
