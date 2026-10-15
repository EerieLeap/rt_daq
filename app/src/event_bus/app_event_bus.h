#pragma once

#include "subsys/event_bus/event_bus.h"

#include "domain/logging_domain/event_bus/logging_events_channel.h"
#include "domain/sensor_domain/event_bus/sensor_events_channel.h"

namespace eerie_leap::event_bus {

using eerie_leap::subsys::event_bus::EventBus;
using eerie_leap::domain::logging_domain::event_bus::LoggingEventsChannel;
using eerie_leap::domain::sensor_domain::event_bus::SensorEventsChannel;

// The unit's one bus thread. It carries logging_events (the writer's status: the supervisor, the
// exporter and the display follow it) and sensor_events (the readings the logging triggers bind to).
// rt_core's SensorEventBus is never created here: it registers the same channel on a second thread,
// and a channel stays with the first bus that registers it while the other's registration fails with
// -EEXIST. The publisher sends only the sensors a subscriber accepts, which on this unit are the
// triggers', so the sensor channel stays light.
class AppEventBus : public EventBus {
private:
    static constexpr int k_stack_size = 4096;

    AppEventBus() : EventBus("app_event_bus", k_stack_size) {
        RegisterChannel(LoggingEventsChannel::GetInstance());
        RegisterChannel(SensorEventsChannel::GetInstance());
    }

public:
    static AppEventBus& GetInstance() {
        // Never destroyed: the destructor stops the work queue and waits for its thread, which a host
        // exit on native_sim (Ctrl-C, -stop_at) no longer schedules, so the process would hang there.
        // On a target the bus lives for the program anyway.
        static AppEventBus* bus = new AppEventBus();

        return *bus;
    }
};

} // namespace eerie_leap::event_bus
