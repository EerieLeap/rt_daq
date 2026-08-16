#include "domain/canbus_domain/processors/script_processor.h"
#include "domain/script_domain/utilities/global_fuctions_registry.h"

#include "canbus_scheduler_service.h"

namespace eerie_leap::domain::canbus_domain::services {

using namespace eerie_leap::subsys::threading;
using namespace eerie_leap::domain::sensor_domain::models;
using namespace eerie_leap::domain::script_domain::utilities;
using namespace eerie_leap::domain::canbus_domain::processors;

LOG_MODULE_REGISTER(canbus_scheduler_logger);

CanbusSchedulerService::CanbusSchedulerService(
    std::shared_ptr<CanbusConfigurationManager> canbus_configuration_manager,
    std::shared_ptr<CanbusService> canbus_service,
    std::shared_ptr<SensorReadingsFrame> sensor_readings_frame)
        : work_queue_thread_(nullptr),
        canbus_configuration_manager_(std::move(canbus_configuration_manager)),
        canbus_service_(std::move(canbus_service)),
        sensor_readings_frame_(std::move(sensor_readings_frame)),
        can_frame_processors_(std::make_shared<std::vector<std::shared_ptr<ICanFrameProcessor>>>()) {

    can_frame_dbc_builder_ = std::make_shared<CanFrameDbcBuilder>(canbus_service_, sensor_readings_frame_);
    can_frame_processors_->push_back(std::make_shared<ScriptProcessor>(sensor_readings_frame_, "process_can_frame"));
};

bool CanbusSchedulerService::DoInitialize() {
    work_queue_thread_ = std::make_unique<WorkQueueThread>(
        "canbus_scheduler_service",
        thread_stack_size_,
        thread_priority_);

    return work_queue_thread_->Initialize();
}

WorkQueueTaskResult CanbusSchedulerService::ProcessCanbusWorkTask(CanbusTask* task) {
    try {
        if(!task->canbus->IsValid()) {
            LOG_ERR("Canbus is not valid");

            return {
                .reschedule = false,
                .delay = K_NO_WAIT
            };
        }

        auto can_frame_data = task->can_frame_dbc_builder->Build(
            task->bus_channel, task->message_configuration->frame_id);

        for(const auto& processor : *task->can_frame_processors) {
            auto processed_data = processor->Process(*task->message_configuration, can_frame_data);

            if(!processed_data.empty())
                can_frame_data = processed_data;
        }

        if(can_frame_data.size() > 0)
            (*task->canbus)->SendFrame(task->message_configuration->frame_id, can_frame_data);

        LOG_HEXDUMP_DBG(
            can_frame_data.data(),
            can_frame_data.size(),
            ("Can frame " + std::to_string(task->message_configuration->frame_id)).c_str());
    } catch (const std::exception& e) {
        LOG_DBG("Error processing Frame ID: %d, Error: %s", task->message_configuration->frame_id, e.what());
    }

    return {
        .reschedule = true,
        .delay = task->send_interval_ms
    };
}

std::unique_ptr<CanbusTask> CanbusSchedulerService::CreateTask(uint8_t bus_channel, std::shared_ptr<CanMessageConfiguration> message_configuration) {
    auto canbus = canbus_service_->GetCanbus(bus_channel);
    if(canbus == nullptr) {
        LOG_ERR("Failed to create task for Frame ID: %d", message_configuration->frame_id);
        return nullptr;
    }

    auto dbc = canbus_service_->GetChannelConfiguration(bus_channel)->dbc;
    if(dbc == nullptr || !dbc->HasMessage(message_configuration->frame_id)) {
        LOG_ERR("Failed to create task for Frame ID: %d", message_configuration->frame_id);
        return nullptr;
    }

    if(!message_configuration->send_interval_ms.has_value())
        return nullptr;

    InitializeScript(*message_configuration);

    auto task = std::make_unique<CanbusTask>();
    task->send_interval_ms = K_MSEC(message_configuration->send_interval_ms.value());
    task->bus_channel = bus_channel;
    task->message_configuration = std::move(message_configuration);
    task->canbus = canbus;
    task->can_frame_dbc_builder = can_frame_dbc_builder_;
    task->can_frame_processors = can_frame_processors_;

    return task;
}

void CanbusSchedulerService::StartTasks() {
    for(auto& work_queue_task : work_queue_tasks_)
        work_queue_task.Schedule();

    k_sleep(K_MSEC(1));
}

void CanbusSchedulerService::CancelTasks() {
    for(auto& work_queue_task : work_queue_tasks_) {
        LOG_INF("Canceling task for Frame ID: %d", work_queue_task.GetUserdata()->message_configuration->frame_id);

        while(work_queue_task.Cancel())
            k_sleep(K_MSEC(1));
    }
}

bool CanbusSchedulerService::DoStart() {
    auto canbus_configuration = canbus_configuration_manager_->Get();

    for(const auto& [bus_channel, channel_configuration] : canbus_configuration->channel_configurations) {
        for(const auto& message_configuration : channel_configuration.message_configurations) {
            auto task = CreateTask(bus_channel, message_configuration);
            if(task == nullptr)
                continue;

            work_queue_tasks_.push_back(
                work_queue_thread_->CreateTask(ProcessCanbusWorkTask, std::move(task)));
            LOG_INF("Created task for Frame ID: %d", message_configuration->frame_id);
        }
    }

    StartTasks();

    LOG_INF("CANBus Scheduler Service started");

    return true;
}

bool CanbusSchedulerService::DoStop() {
    CancelTasks();
    work_queue_tasks_.clear();

    LOG_INF("CANBus Scheduler Service stopped.");

    return true;
}

bool CanbusSchedulerService::Restart() {
    Stop();
    sensor_readings_frame_->ClearReadings();

    return Start();
}

bool CanbusSchedulerService::DoPause() {
    CancelTasks();

    LOG_INF("CANBus Scheduler Service paused.");

    return true;
}

bool CanbusSchedulerService::DoResume() {
    StartTasks();

    LOG_INF("CANBus Scheduler Service resumed.");

    return true;
}

void CanbusSchedulerService::InitializeScript(const CanMessageConfiguration& message_configuration) {
    auto lua_script = message_configuration.lua_script;

    if(lua_script == nullptr)
        return;

    GlobalFunctionsRegistry::RegisterGetSensorValue(*lua_script, *sensor_readings_frame_);
    GlobalFunctionsRegistry::RegisterUpdateSensorValue(*lua_script, *sensor_readings_frame_);
}

} // namespace eerie_leap::domain::canbus_domain::services
