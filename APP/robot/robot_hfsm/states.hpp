#pragma once
#include "config.hpp"
#include "hfsm.hpp"

namespace robot {

struct Starting : M::State {
    HFSM_STATE_CALLBACKS_WITH_BASE(M);
};
struct Disable : M::State {
    HFSM_STATE_CALLBACKS_WITH_BASE(M);
};
struct Enable : M::State {
    HFSM_STATE_CALLBACKS_WITH_BASE(M);
};
struct Manual : M::State {
    HFSM_STATE_CALLBACKS_WITH_BASE(M);
};
struct UniversalManual : M::State {
    HFSM_STATE_CALLBACKS_WITH_BASE(M);
};
struct Auto : M::State {
    HFSM_STATE_CALLBACKS_WITH_BASE(M);
};
struct WaitUpperComputer : M::State {
    HFSM_STATE_CALLBACKS_WITH_BASE(M);
};
struct DevelopingAuto : M::State {
    HFSM_STATE_CALLBACKS_WITH_BASE(M);
};
struct Paused : M::State {
    HFSM_STATE_CALLBACKS_WITH_BASE(M);
};
struct Fault : M::State {
    HFSM_STATE_CALLBACKS_WITH_BASE(M);
};

}
//无需保护的状态
