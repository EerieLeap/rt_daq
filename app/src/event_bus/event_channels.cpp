#include "subsys/event_bus/event_channel_registry.h"

#include "app_event_bus.h"
#include "event_channels.h"

namespace eerie_leap::event_bus {

using eerie_leap::subsys::event_bus::EventChannelId;
using eerie_leap::subsys::event_bus::EventChannelRegistry;

void InitializeEventChannels() {
    AppEventBus::GetInstance();

    auto& registry = EventChannelRegistry::GetInstance();

    // The ids logging triggers name in their stored configuration.
    registry.Register(EventChannelId::Sensors, SensorEventsChannel::GetInstance());
    registry.Register(EventChannelId::Logging, LoggingEventsChannel::GetInstance());
}

} // namespace eerie_leap::event_bus
