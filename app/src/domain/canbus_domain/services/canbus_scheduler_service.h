#pragma once

#include <memory>
#include <vector>

#include <zephyr/kernel.h>

#include "subsys/threading/service_base.h"
#include "subsys/threading/work_queue_thread.h"
#include "domain/sensor_domain/utilities/sensor_readings_frame.hpp"
#include "domain/canbus_domain/configuration/canbus_configuration_manager.h"
#include "domain/canbus_domain/models/can_message_configuration.h"
#include "domain/canbus_domain/services/canbus_service.h"

#include "canbus_task.hpp"

namespace eerie_leap::domain::canbus_domain::services {

namespace threading = eerie_leap::subsys::threading;

using threading::ServiceBase;
using threading::ServiceState;
using threading::WorkQueueThread;
using threading::WorkQueueTaskResult;
using eerie_leap::domain::sensor_domain::utilities::SensorReadingsFrame;
using eerie_leap::domain::canbus_domain::configuration::CanbusConfigurationManager;
using eerie_leap::domain::canbus_domain::models::CanMessageConfiguration;
using eerie_leap::domain::canbus_domain::services::CanbusService;

class CanbusSchedulerService final : public ServiceBase<> {
private:
    static constexpr int thread_stack_size_ = 8192;
    static constexpr int thread_priority_ = 6;
    std::unique_ptr<WorkQueueThread> work_queue_thread_;

    std::shared_ptr<CanbusConfigurationManager> canbus_configuration_manager_;
    std::shared_ptr<CanbusService> canbus_service_;
    std::shared_ptr<SensorReadingsFrame> sensor_readings_frame_;

    std::vector<threading::WorkQueueTask<CanbusTask>> work_queue_tasks_;
    std::shared_ptr<CanFrameDbcBuilder> can_frame_dbc_builder_;
    std::shared_ptr<std::vector<std::shared_ptr<ICanFrameProcessor>>> can_frame_processors_;

    void StartTasks();
    void CancelTasks();
    std::unique_ptr<CanbusTask> CreateTask(uint8_t bus_channel, std::shared_ptr<CanMessageConfiguration> message_configuration);
    static WorkQueueTaskResult ProcessCanbusWorkTask(CanbusTask* task);

    void InitializeScript(const CanMessageConfiguration& message_configuration);

    bool DoInitialize() override;
    bool DoStart() override;
    bool DoStop() override;
    bool DoPause() override;
    bool DoResume() override;

public:
    CanbusSchedulerService(
        std::shared_ptr<CanbusConfigurationManager> canbus_configuration_manager,
        std::shared_ptr<CanbusService> canbus_service,
        std::shared_ptr<SensorReadingsFrame> sensor_readings_frame);
    ~CanbusSchedulerService() override = default;

    [[nodiscard]] bool IsPausable() const noexcept override { return true; }

    bool Restart();
};

} // namespace eerie_leap::domain::canbus_domain::services
