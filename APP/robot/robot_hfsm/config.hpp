#pragma once
#include "hfsm.hpp"

namespace robot {

struct Robot_Context;

struct Disable;
struct Starting;
struct WaitPower;
struct PrepareSubsystems;
struct Enable;
struct Manual;
struct UniversalManual;
struct Auto;
struct WaitUpperComputer;
struct DevelopingAuto;
struct Paused;
struct Test;
struct TestStarting;
struct TestWaitPower;
struct PrepareTestSubsystems;
struct TestRunning;
struct Fault;
using StateList = hfsm::TypeList<Disable, Starting, WaitPower, PrepareSubsystems,
                    Enable, Manual, UniversalManual, Auto, WaitUpperComputer,
                    DevelopingAuto, Paused, Test, TestStarting, TestWaitPower,
                    PrepareTestSubsystems, TestRunning, Fault>;
using M = hfsm::MachineT<Robot_Context, StateList>;
using Control = M::Control;
using GuardControl = M::GuardControl;
using State = M::State;

} // namespace robot
