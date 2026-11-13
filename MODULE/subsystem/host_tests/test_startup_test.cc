#include "robot.hpp"

#include <cassert>
#include <iostream>

using namespace robot;
using SubsystemState = Subsystem::State;
using Reason = Robot_Context::FaultReason;

void tickAll() {
    for (auto* subsystem : subsystems) subsystem->periodic();
}

template<class T> void reach() {
    for (unsigned i = 0; i < 12 && !robot_machine.isActive<T>(); ++i) {
        tickAll();
        robot_machine.update();
    }
    assert(robot_machine.isActive<T>());
}

int main() {
    extra_subsystem.online = true;
    extra_subsystem.ready = true;
    robot_machine.update();
    tickAll();
    rtos::clock_ms = 60000;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<TestStarting>());
    assert(extra_subsystem.isStandby() && test_subsystem.state() == SubsystemState::Preparing);
    assert(extra_subsystem.prepares == 1 && pid_init_count == 0);
    test_motor.online = true;
    tickAll();
    robot_machine.update();
    test_subsystem.periodic();
    for (unsigned i = 0; i < 3; ++i) robot_machine.update();
    assert(robot_machine.isActive<TestStarting>() && extra_subsystem.isStandby());
    assert(robot_machine.immediateChangeTo<TestRunning>() == M::TransitionResult::RejectedByEntryGuard);
    extra_subsystem.periodic();
    robot_machine.update();
    assert(robot_machine.isActive<TestRunning>());
    std::cout << "PASS: multiple test participants prepare independently and wait for actual run\n";

    robot_machine.immediateChangeTo<TestStarting>();
    tickAll();
    assert(extra_subsystem.requestRun());
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    assert(robot_context.faults[1] == Reason::RunRejected);
    tickAll();
    assert(test_subsystem.isStandby() && extra_subsystem.state() == SubsystemState::Disabled);
    assert(!robot_context.recovery_requested.load());
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    const auto healthy_inits = pid_init_count;
    reach<TestRunning>();
    assert(pid_init_count == healthy_inits);
    std::cout << "PASS: test startup refusal isolates the rejected subject and preserves healthy preparation\n";

    extra_subsystem.ready = false;
    tickAll();
    robot_machine.update();
    tickAll();
    assert(robot_machine.isActive<Fault>());
    extra_subsystem.online = false;
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    rtos::clock_ms += 60000;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Recovering>() && !extra_subsystem.hasFailed());
    extra_subsystem.online = true;
    tickAll();
    rtos::clock_ms += 60000;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Recovering>() && extra_subsystem.state() == SubsystemState::Preparing);
    assert(!robot_context.recovery_requested.load());
    extra_subsystem.ready = true;
    extra_subsystem.run_success = false;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>() && !robot_context.recovery_requested.load());
    extra_subsystem.run_success = true;
    tickAll();
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    reach<TestRunning>();
    assert(test_subsystem.isRunning() && extra_subsystem.isRunning());
    std::cout << "PASS: recovery preparation has no timeout, callback failure requires a fresh recovery flag\n";

    robot_machine.immediateChangeTo<TestStarting>();
    tickAll();
    assert(test_subsystem.isStandby() && extra_subsystem.isStandby());
    extra_subsystem.online = false;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    assert(robot_context.faults[1] == Reason::SubsystemFailed);
    tickAll();
    assert(test_subsystem.isStandby() && extra_subsystem.state() == SubsystemState::Disabled);
    std::cout << "PASS: loss of a prepared standby participant enters robot Fault before running\n";
}
