#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

struct StateUpdateHeader {
    DeviceIdentifier Origin{};
    StateTypeId TypeId = 0;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
};

struct StateAcknowledgement {
    DeviceIdentifier Origin{};
    StateTypeId TypeId = 0;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
};

template<typename TValue>
struct StateUpdate final {
    StateUpdateHeader Header{};
    TValue Value{};
};

template<typename TValue>
struct PendingStateUpdate final {
    bool Pending = false;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    StateRevision LastAcknowledgedRevision = 0;
    TValue Value{};

    void Replace(StateEpoch epoch, StateRevision revision, const TValue& value) {
        if (revision == 0) return;
        if (Pending && epoch < Epoch) return;
        if (Pending && epoch == Epoch && revision <= Revision) return;

        Epoch = epoch;
        Revision = revision;
        Value = value;
        Pending = true;
    }

    bool Acknowledge(StateEpoch epoch, StateRevision revision) {
        if (epoch != Epoch || revision == 0) return false;
        if (revision < LastAcknowledgedRevision) return false;

        LastAcknowledgedRevision = revision;
        if (Pending && revision >= Revision) {
            Pending = false;
        }
        return true;
    }
};

}
}
