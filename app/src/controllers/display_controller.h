#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "subsys/cfb/cfb.h"
#include "subsys/event_bus/dispatch_guard.h"
#include "subsys/event_bus/i_scoped_subscription.h"
#include "subsys/threading/work_queue_task.h"
#include "subsys/threading/work_queue_thread.h"
#include "views/animations/i_animation.h"

namespace eerie_leap::controllers {

using eerie_leap::subsys::cfb::Cfb;
using eerie_leap::subsys::event_bus::AnySubscription;
using eerie_leap::subsys::event_bus::DispatchGuard;
using eerie_leap::subsys::threading::WorkQueueTask;
using eerie_leap::subsys::threading::WorkQueueThread;
using eerie_leap::views::animations::IAnimation;

class DisplayController {
private:
    std::shared_ptr<Cfb> cfb_;
    std::unique_ptr<IAnimation> animation_;
    int current_animation_index_ = 0;
    bool is_animation_in_progress_ = false;
    std::set<std::string> statuses_;
    bool is_initialized_ = false;

    // The logging status. The queue is declared before the task, so it outlives it.
    std::shared_ptr<WorkQueueThread> logging_status_work_queue_thread_;
    std::optional<WorkQueueTask<DisplayController>> logging_status_task_;
    // The handler's way back to this; the destructor detaches it first.
    std::shared_ptr<DispatchGuard> guard_;
    AnySubscription logging_status_subscription_;
    // The last StatusUpdated said active; written on the bus thread.
    std::atomic<bool> is_logging_active_ = false;
    // Whether "log" is shown; the logging status task only.
    bool is_logging_status_shown_ = false;

    void UpdateStatuses();
    void UpdateLoggingStatus();

public:
    explicit DisplayController(std::shared_ptr<Cfb> cfb);
    // Withdraws the subscription and the task, and stops the animation. Not on the queue given to
    // FollowLoggingStatus(): it waits for a task in progress.
    ~DisplayController();

    // The animation handler, the task and the subscription refer to it.
    DisplayController(const DisplayController&) = delete;
    DisplayController& operator=(const DisplayController&) = delete;

    void Initialize();

    // Shows "log" while the last StatusUpdated on logging_events said active. The display is not
    // thread-safe, so the status is applied on @p work_queue_thread, the configuration work queue,
    // and only on a change: removing the last status restarts the idle animation. Call it once,
    // after Initialize() and before anything publishes the logging status; without a display it does
    // nothing.
    void FollowLoggingStatus(std::shared_ptr<WorkQueueThread> work_queue_thread);

    void StartAnimation(int animation_index = -1);
    void StopAnimation();

    void PrintStringLine(const std::string& str);

    void AddStatus(const std::string& status);
    void RemoveStatus(const std::string& status);
    void ClearStatuses();
};

} // namespace eerie_leap::controllers
