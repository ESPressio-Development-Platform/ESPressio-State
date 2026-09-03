#pragma once

#include <ESPressio_DeviceIdentifier.hpp>

namespace ESPressio {
namespace State {

/// <summary>
/// Canonical permanent transport-independent ESPressio device identity.
/// </summary>
/// <remarks>
/// DeviceIdentifier is owned by ESPressio-System. State uses that canonical
/// identity when addressing authoritative and replicated State; it does not
/// define a transport-specific or MAC-derived identity of its own.
/// </remarks>
using DeviceIdentifier = System::DeviceIdentifier;

} // namespace State
} // namespace ESPressio
