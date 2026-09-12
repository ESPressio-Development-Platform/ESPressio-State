#pragma once
#include <cstddef>
namespace ESPressio::State {
template<std::size_t N> struct MaximumRemoteOwners { static constexpr std::size_t Value=N; };
template<std::size_t N> struct MaximumSubscribers { static constexpr std::size_t Value=N; };
template<class TState,class TRemoteOwners=MaximumRemoteOwners<0>,class TSubscribers=MaximumSubscribers<0>>
struct TypeConfiguration final {
    using StateType=TState;
    static constexpr std::size_t RemoteOwnerCapacity=TRemoteOwners::Value;
    static constexpr std::size_t SubscriberCapacity=TSubscribers::Value;
};
}
