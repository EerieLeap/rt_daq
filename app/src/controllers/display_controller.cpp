#include <variant>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "domain/logging_domain/event_bus/logging_events_channel.h"
#include "subsys/event_bus/event_channel.h"
#include "views/animations/dot_path_animation.h"
#include "views/animations/particles_animation.h"
#include "views/animations/tapered_wave_animation.h"
#include "views/animations/wave_lines_animation.h"

#include "display_controller.h"

LOG_MODULE_REGISTER(display_controller_logger);

namespace eerie_leap::controllers {

using namespace eerie_leap::views::animations;

using eerie_leap::domain::logging_domain::event_bus::LoggingEventsChannel;
using eerie_leap::domain::logging_domain::event_bus::LoggingEventType;
using eerie_leap::domain::logging_domain::event_bus::LoggingPayloadType;
using eerie_leap::subsys::event_bus::CreateScopedSubscription;
using eerie_leap::subsys::threading::WorkQueueTaskResult;

DisplayController::DisplayController(std::shared_ptr<Cfb> cfb) :
    cfb_(std::move(cfb)),
    guard_(std::make_shared<DispatchGuard>(this)) {}

DisplayController::~DisplayController() {
    // Detach before the unsubscribe and the cancel: a handler in flight could otherwise reschedule
    // the task after the cancel.
    guard_->Detach();
    logging_status_subscription_.reset();

    if(logging_status_task_.has_value())
        logging_status_task_->Cancel();

    // The animation handler refers to this and the Cfb outlives the controller, so stop it here
    // rather than clear the handler: the Cfb divides by the handler's frame rate.
    StopAnimation();
}

void DisplayController::Initialize() {
    if(!cfb_ || is_initialized_)
        return;

    cfb_->SetFont(0);
    is_initialized_ = true;

    StartAnimation(0);
}

void DisplayController::FollowLoggingStatus(std::shared_ptr<WorkQueueThread> work_queue_thread) {
    if(!is_initialized_ || logging_status_subscription_ != nullptr)
        return;

    if(work_queue_thread == nullptr) {
        LOG_ERR("Failed to follow the logging status: no work queue.");
        return;
    }

    logging_status_work_queue_thread_ = std::move(work_queue_thread);

    // The task comes first, so a handler never finds it empty.
    logging_status_task_ = logging_status_work_queue_thread_->CreateTask(
        [](DisplayController* controller) {
            controller->UpdateLoggingStatus();
            return WorkQueueTaskResult{};
        },
        this
    );

    // The writer publishes asynchronously, so the handler runs on the bus thread; the display is
    // not thread-safe, so what it triggers runs on the work queue.
    logging_status_subscription_ = CreateScopedSubscription(
        LoggingEventsChannel::GetInstance(), LoggingEventType::StatusUpdated,
        [guard = guard_, this](const LoggingEventsChannel::EventMessage& event) {
            guard->Dispatch([&] {
                const auto* const is_active = event.payload.find(LoggingPayloadType::IsActive);
                is_logging_active_ = is_active != event.payload.end()
                    && std::holds_alternative<bool>(is_active->second)
                    && std::get<bool>(is_active->second);

                logging_status_task_->Reschedule(K_NO_WAIT);
            });
        }
    );
    if(logging_status_subscription_ == nullptr)
        LOG_ERR("Failed to follow the logging status.");
}

// On the work queue given to FollowLoggingStatus().
void DisplayController::UpdateLoggingStatus() {
    // Only on a change: removing the last status restarts the idle animation.
    const bool is_active = is_logging_active_.load();
    if(is_active == is_logging_status_shown_)
        return;

    if(is_active)
        AddStatus("log");
    else
        RemoveStatus("log");

    is_logging_status_shown_ = is_active;
    LOG_DBG("Logging status %s.", is_active ? "shown" : "hidden");
}

void DisplayController::StartAnimation(int animation_index) {
    if(!is_initialized_)
        return;

    if(animation_index < 0)
        animation_index = current_animation_index_;

    switch(animation_index) {
        case 0:
            animation_ = std::make_unique<WaveLinesAnimation>(cfb_);
            break;
        case 1:
            animation_ = std::make_unique<DotPathAnimation>(cfb_);
            break;
        case 2:
            animation_ = std::make_unique<ParticlesAnimation>(cfb_);
            break;
        case 3:
            animation_ = std::make_unique<TaperedWaveAnimation>(cfb_);
            break;
        default:
            return;
    }

    animation_->Initialize();

    cfb_->SetAnimationHandler(
        [this] {
            animation_->Animate();
        },
        30
    );
    cfb_->StartAnimation();

    is_animation_in_progress_ = true;
    current_animation_index_ = animation_index;
}

void DisplayController::StopAnimation() {
    if(!is_initialized_)
        return;

    if(!is_animation_in_progress_)
        return;

    cfb_->StopAnimation();

    animation_.reset();
    is_animation_in_progress_ = false;
}

void DisplayController::PrintStringLine(const std::string& str) {
    if(!is_initialized_)
        return;

    StopAnimation();

    cfb_->Clear();
    cfb_->PrintString(str.c_str(), { 0, 0 });
    cfb_->Flush();
}

void DisplayController::AddStatus(const std::string& status) {
    if(!is_initialized_)
        return;

    statuses_.insert(status);
    UpdateStatuses();
}

void DisplayController::RemoveStatus(const std::string& status) {
    if(!is_initialized_)
        return;

    statuses_.erase(status);
    UpdateStatuses();
}

void DisplayController::ClearStatuses() {
    if(!is_initialized_)
        return;

    statuses_.clear();
    UpdateStatuses();
}

void DisplayController::UpdateStatuses() {
    if(!is_initialized_)
        return;

    if(statuses_.empty()) {
        StartAnimation();

        return;
    }

    int length_chars = 0;
    for(const auto& s : statuses_)
        length_chars += static_cast<int>(s.size());

    const int padding = 1;

    const int length_px = length_chars * cfb_->GetFontWidth() + static_cast<int>(statuses_.size()) * padding * 2;

    int start_x_px = cfb_->GetXRes() - length_px;
    start_x_px = start_x_px < 0 ? 0 : start_x_px;
    const int y = cfb_->GetYRes() - cfb_->GetFontHeight() - padding;

    StopAnimation();
    cfb_->Clear();

    int current_x_px = start_x_px;
    int i = 0;
    for(const auto& s : statuses_) {
        cfb_->PrintStringLine(s.c_str(), { current_x_px + padding, y });
        const int current_length_px = static_cast<int>(s.size()) * cfb_->GetFontWidth() + padding * 2;

        if(i++ % 2 != 0) {
            cfb_->InvertArea(
                { current_x_px, y - padding }, { current_x_px + current_length_px, y + cfb_->GetFontHeight() + padding }
            );
        }

        current_x_px += current_length_px;
    }

    cfb_->Flush();
}

} // namespace eerie_leap::controllers
