#include <exception>
#include <string>
#include <variant>

#include <zephyr/logging/log.h>

#include "configuration/cbor/cbor_logging_config/cbor_logging_config.h"
#include "configuration/services/cbor_configuration_service.h"
#include "subsys/event_bus/event_channel.h"
#include "domain/canbus_com_domain/commands/canbus_com_logging_command.h"
#include "domain/logging_domain/event_bus/logging_event_bus.h"
#include "domain/logging_domain/event_bus/logging_events_channel.h"
#include "domain/system_domain/models/system_configuration.h"

#include "logging_controller.h"

namespace eerie_leap::controllers {

using namespace eerie_leap::domain::canbus_com_domain::commands;
using eerie_leap::subsys::event_bus::CreateScopedSubscription;
using eerie_leap::subsys::threading::ScopedMutex;
using eerie_leap::subsys::threading::ServiceState;
using eerie_leap::subsys::threading::WorkQueueTaskResult;
using eerie_leap::domain::logging_domain::event_bus::LoggingEventBus;
using eerie_leap::domain::logging_domain::event_bus::LoggingEventsChannel;
using eerie_leap::domain::logging_domain::event_bus::LoggingEventType;
using eerie_leap::domain::logging_domain::event_bus::LoggingPayloadType;
using eerie_leap::domain::logging_domain::event_bus::LoggingStatusReason;
using eerie_leap::domain::system_domain::models::SystemConfiguration;

namespace config_services = eerie_leap::configuration::services;

LOG_MODULE_REGISTER(logging_controller_logger);

static const char* GetReasonName(LoggingStatusReason reason) {
    switch(reason) {
        case LoggingStatusReason::Stopped:
            return "stopped";
        case LoggingStatusReason::WriteFailed:
            return "write failed";
        case LoggingStatusReason::NoSpace:
            return "no space";
        case LoggingStatusReason::RotationFailed:
            return "rotation failed";
    }

    return "unknown";
}

// The writer as the sensors controller restarts it, which would otherwise bypass the controller's
// lock and bring back a writer that the CDMP command or the shell stopped during the restart.
class LoggingController::DependentLogWriter final : public IService {
private:
    LoggingController& controller_;

public:
    explicit DependentLogWriter(LoggingController& controller) : controller_(controller) {}

    bool Initialize() override { return true; }
    bool Start() override { return controller_.StartAfterProcessingRestart(); }
    bool Stop() override { return controller_.StopForProcessingRestart(); }

    [[nodiscard]] ServiceState GetState() const noexcept override {
        return controller_.log_writer_service_->GetState();
    }
};

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
        display_controller_(std::move(display_controller)) {

    k_mutex_init(&start_stop_mutex_);
}

LoggingController::~LoggingController() {
    // The manager and the channel may outlive the handlers' captures.
    if(logging_configuration_manager_ != nullptr)
        logging_configuration_manager_->RegisterConfigurationUpdatedHandler(nullptr);

    status_subscription_.reset();

    if(restart_task_.has_value())
        restart_task_->Cancel();
    if(status_task_.has_value())
        status_task_->Cancel();
}

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

    // Every start builds the logger, also the one after a sensors restart, which stops and starts
    // the writer as a dependent of sensor processing.
    log_writer_service_->SetLoggerFactory([this] { return CreateLogger(); });
    dependent_log_writer_ = std::make_shared<DependentLogWriter>(*this);

    restart_task_ = config_work_queue_thread_->CreateTask(
        [](LoggingController* controller) {
            controller->RestartLogWriter();
            return WorkQueueTaskResult{};
        }, this);

    status_task_ = config_work_queue_thread_->CreateTask(
        [](LoggingController* controller) {
            controller->UpdateStatus();
            return WorkQueueTaskResult{};
        }, this);

    // The channel drops every publish until a bus carries it. The writer publishes asynchronously, so
    // this handler runs on the bus thread; what it triggers runs on the configuration work queue,
    // because Stop() waits for the writer's tick and the display is not thread-safe.
    LoggingEventBus::GetInstance();
    status_subscription_ = CreateScopedSubscription(
        LoggingEventsChannel::GetInstance(),
        LoggingEventType::StatusUpdated,
        [this](const LoggingEventsChannel::EventMessage& event) {
            const auto is_active = event.payload.find(LoggingPayloadType::IsActive);
            is_logging_active_ = is_active != event.payload.end()
                && std::holds_alternative<bool>(is_active->second)
                && std::get<bool>(is_active->second);

            const auto reason = event.payload.find(LoggingPayloadType::Reason);
            if(reason != event.payload.end() && std::holds_alternative<uint32_t>(reason->second))
                last_inactive_reason_ = std::get<uint32_t>(reason->second);

            status_task_->Reschedule(K_NO_WAIT);
        });
    if(status_subscription_ == nullptr)
        LOG_ERR("Failed to subscribe to the log writer status.");

    // Runs on the thread that stored the configuration (an SMP apply runs on the configuration work
    // queue while it holds its lock), so the restart is queued behind it.
    logging_configuration_manager_->RegisterConfigurationUpdatedHandler([this] {
        restart_task_->Reschedule(K_NO_WAIT);
    });

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

LoggerMetadata LoggingController::CreateLoggerMetadata() const {
    LoggerMetadata metadata;

    metadata.device_name = CONFIG_BOARD;
    metadata.tool_id = "rt_daq";
    // The same version as the sw_version property, so a file names one software version.
    metadata.tool_version = std::to_string(CONFIG_EERIE_LEAP_SW_VERSION_MAJOR)
        + "." + std::to_string(CONFIG_EERIE_LEAP_SW_VERSION_MINOR)
        + "." + std::to_string(CONFIG_EERIE_LEAP_SW_VERSION_PATCH);
    metadata.product_id = CONFIG_EERIE_LEAP_PRODUCT_ID;
    metadata.hw_version = SystemConfiguration::hw_version;
    metadata.sw_version = SystemConfiguration::sw_version;

    // Unassigned (0) until the unit has claimed an ID on the CDMP network.
    const auto network_info = canbus_com_service_ != nullptr ? canbus_com_service_->GetNetworkInfo() : nullptr;
    if(network_info != nullptr)
        metadata.device_id = network_info->GetDeviceInfo().device_id;

    // The CRC of the stored CBOR, so a log can be matched with the configuration that produced it.
    // A stored configuration that was rejected at boot is not what runs: 0, unknown, instead.
    const auto configuration_info = sensors_configuration_manager_->GetCborConfigurationInfo();
    metadata.sensors_configuration_hash = configuration_info.is_applied ? configuration_info.crc : 0;

    return metadata;
}

std::shared_ptr<Mdf4LoggerSensorReading> LoggingController::CreateLogger() const {
    const auto sensors = sensors_configuration_manager_->Get();
    const auto logging_configuration = logging_configuration_manager_->Get();

    if(sensors == nullptr || logging_configuration == nullptr)
        return nullptr;

    return std::make_shared<Mdf4LoggerSensorReading>(*logging_configuration, *sensors, CreateLoggerMetadata());
}

bool LoggingController::LogWriterStart() {
    ScopedMutex guard(start_stop_mutex_);

    // A failed session keeps the service RUNNING, without a tick, until it is stopped.
    if(log_writer_service_->HasFailed() && !log_writer_service_->Stop())
        return false;

    const bool is_started = log_writer_service_->Start();
    is_logging_requested_ = is_started;

    return is_started;
}

bool LoggingController::LogWriterStop() {
    ScopedMutex guard(start_stop_mutex_);

    is_logging_requested_ = false;

    return log_writer_service_->Stop();
}

std::shared_ptr<IService> LoggingController::GetDependentService() const {
    return dependent_log_writer_;
}

bool LoggingController::StopForProcessingRestart() {
    ScopedMutex guard(start_stop_mutex_);

    return log_writer_service_->Stop();
}

bool LoggingController::StartAfterProcessingRestart() {
    ScopedMutex guard(start_stop_mutex_);

    // Stopped or failed meanwhile: the next start is the user's.
    if(!is_logging_requested_ || log_writer_service_->HasFailed())
        return true;

    is_logging_requested_ = log_writer_service_->Start();

    return is_logging_requested_;
}

// A stored logging configuration takes effect at the next start, so a running writer is restarted:
// the file it was writing is finalized and the next one is laid out for the new configuration.
void LoggingController::RestartLogWriter() {
    ScopedMutex guard(start_stop_mutex_);

    if(!log_writer_service_->IsRunning() || log_writer_service_->HasFailed())
        return;

    try {
        if(!log_writer_service_->Stop()) {
            LOG_ERR("Failed to stop logging for the new configuration.");
            return;
        }

        is_logging_requested_ = log_writer_service_->Start();
        if(!is_logging_requested_)
            LOG_ERR("Failed to restart logging with the new configuration.");
    } catch(const std::exception& e) {
        LOG_ERR("Failed to restart logging with the new configuration: %s", e.what());
    }
}

// A session that failed (WriteFailed, NoSpace or RotationFailed) is stopped here, off the writer
// thread: the stop finalizes what it can and closes the file. The service's own failure flag
// decides, under the lock, rather than the event's reason, so a failure event that arrives after
// a new start stops nothing, and a start in progress is waited for.
void LoggingController::StopIfFailed() {
    ScopedMutex guard(start_stop_mutex_);

    if(!log_writer_service_->HasFailed() || !log_writer_service_->IsRunning())
        return;

    LOG_ERR("Logging failed (%s); stopping the log writer.",
        GetReasonName(static_cast<LoggingStatusReason>(last_inactive_reason_.load())));

    is_logging_requested_ = false;
    log_writer_service_->Stop();
}

// Follows the writer's status events, on the configuration work queue.
void LoggingController::UpdateStatus() {
    try {
        StopIfFailed();
    } catch(const std::exception& e) {
        LOG_ERR("Failed to stop the log writer: %s", e.what());
    }

    // Only on a change: removing the last status restarts the idle animation.
    const bool is_active = is_logging_active_.load();
    if(is_active == is_log_status_shown_)
        return;

    if(is_active)
        display_controller_->AddStatus("log");
    else
        display_controller_->RemoveStatus("log");

    is_log_status_shown_ = is_active;
}

} // namespace eerie_leap::controllers
