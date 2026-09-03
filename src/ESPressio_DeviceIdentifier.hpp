#pragma once

#include <ESPressio_System.hpp>

namespace ESPressio {
namespace State {

/// <summary>Canonical permanent transport-independent ESPressio device identity.</summary>
/// <remarks>
/// DeviceIdentifier is owned by ESPressio-System. State uses that canonical
/// identity for StateAddress and provenance and owns no MAC/radio derivation.
/// </remarks>
using DeviceIdentifier = ESPressio::System::DeviceIdentifier;

} // namespace State
} // namespace ESPressio
