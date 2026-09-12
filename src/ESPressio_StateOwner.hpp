#pragma once
#include <utility>
#include "ESPressio_StateTypes.hpp"
namespace ESPressio::State {
template<class T> class StateTypeRuntime;

template<class TState>
class StateOwner final {
    StateTypeRuntime<TState>* _runtime=nullptr;
    std::uint64_t _token=0;
    friend class StateTypeRuntime<TState>;
    StateOwner(StateTypeRuntime<TState>* runtime,std::uint64_t token) noexcept:_runtime(runtime),_token(token){}
    void Release() noexcept;
public:
    StateOwner() noexcept=default;
    StateOwner(const StateOwner&)=delete;
    StateOwner& operator=(const StateOwner&)=delete;
    StateOwner(StateOwner&& other) noexcept:_runtime(std::exchange(other._runtime,nullptr)),_token(std::exchange(other._token,0)){}
    StateOwner& operator=(StateOwner&& other) noexcept {
        if(this!=&other){ Release();_runtime=std::exchange(other._runtime,nullptr);_token=std::exchange(other._token,0); }
        return *this;
    }
    ~StateOwner(){ Release(); }
    explicit operator bool() const noexcept { return _runtime && _token; }
    StateSetStatus Set(const typename TState::ValueType& candidate);
    StateSetStatus Set(const typename TState::ValueType& candidate,Timing::QualifiedTime truthTime);
};
}
