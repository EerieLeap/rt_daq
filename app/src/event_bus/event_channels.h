#pragma once

namespace eerie_leap::event_bus {

// Constructs the bus and fills the channel registry. Channels are inert until a bus registers
// them, so this has to run before anything publishes or subscribes.
void InitializeEventChannels();

} // namespace eerie_leap::event_bus
