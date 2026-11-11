#pragma once
#include "hfsm.hpp"
#include "states.hpp"

namespace robot {

struct Robot_Context;

struct Disable;
struct Starting;
struct Enable;
struct Manual;
struct UniversalManual;
struct Auto;
struct WaitUpperComputer;
struct DevelopingAuto;
struct Paused;
struct Fault;
using StateList = hfsm::TypeList<Disable, Starting, Enable, Manual, UniversalManual,
                    Auto, WaitUpperComputer, DevelopingAuto, Paused, Fault>;
using M = hfsm::MachineT<Robot_Context, StateList>;
using Control = M::Control;
using GuardControl = M::GuardControl;
using State = M::State;

} // namespace robot