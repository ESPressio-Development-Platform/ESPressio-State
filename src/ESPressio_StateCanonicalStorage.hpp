#pragma once
#include "ESPressio_StateTypes.hpp"
namespace ESPressio::State {
template<class TValue>
class StateCanonicalStorage final {
    TValue _value{};
public:
    static_assert(StateStorageTraits<TValue>::Supported,
                  "State Value requires deterministic no-fail canonical storage or a StateStorageTraits specialization");
    bool Prepare(const TValue& candidate,TValue& prepared) const noexcept {
        return StateStorageTraits<TValue>::Prepare(candidate,prepared);
    }
    void CommitPrepared(const TValue& prepared) noexcept { StateStorageTraits<TValue>::Commit(_value,prepared); }
    void CopyOut(TValue& output) const noexcept { StateStorageTraits<TValue>::CopyOut(_value,output); }
};
}
