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
    assert(robot_machine.isActive<Disable>());
    assert(robot_machine.immediateChangeTo<UniversalManual>() == M::TransitionResult::RejectedByEntryGuard);
    assert(robot_machine.immediateChangeTo<TestRunning>() == M::TransitionResult::RejectedByEntryGuard);
    assert(robot_machine.immediateChangeTo<Recovering>() == M::TransitionResult::RejectedByEntryGuard);
    robot_machine.update();
    assert(robot_machine.isActive<TestStarting>());
    robot_machine.immediateChangeTo<Starting>();
    tickAll();
    rtos::clock_ms = 60000;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Starting>());
    assert(test_subsystem.state() == SubsystemState::Preparing && extra_subsystem.state() == SubsystemState::Preparing);
    assert(pid_init_count == 0 && extra_subsystem.prepares == 0);
    test_motor.online = true;
    tickAll();
    robot_machine.update();
    assert(test_subsystem.isStandby() && test_motor.enabled && test_motor.current == 0);
    assert(extra_subsystem.state() == SubsystemState::Preparing && !extra_subsystem.output);
    assert(pid_init_count == 1 && robot_machine.isActive<Starting>());
    extra_subsystem.online = true;
    tickAll();
    rtos::clock_ms += 4999;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Starting>() && !extra_subsystem.hasFailed());
    assert(test_subsystem.isStandby() && pid_init_count == 1);
    extra_subsystem.ready = true;
    tickAll();
    robot_machine.update(); // Submit run requests exactly once.
    assert(robot_machine.isActive<Starting>() && test_subsystem.isStandby());
    for (unsigned i = 0; i < 3; ++i) robot_machine.update();
    assert(robot_machine.isActive<Starting>() && extra_subsystem.isStandby());
    test_subsystem.periodic();
    robot_machine.update();
    assert(robot_machine.isActive<Starting>());
    assert(robot_machine.immediateChangeTo<UniversalManual>() == M::TransitionResult::RejectedByEntryGuard);
    reach<UniversalManual>();
    assert(test_subsystem.isRunning() && extra_subsystem.isRunning());
    assert(pid_init_count == 1 && extra_subsystem.prepares == 1);
    std::cout << "PASS: independent preparation, staggered power and actual run confirmation\n";

    assert(test_subsystem.setTargetSpeed(7));
    tickAll();
    assert(test_motor.current == 7);
    robot_machine.immediateChangeTo<TestStarting>();
    tickAll();
    assert(test_subsystem.isStandby() && extra_subsystem.isStandby());
    assert(test_motor.current == 0 && test_motor.enabled && extra_subsystem.output);
    reach<TestRunning>();
    assert(pid_init_count == 1 && extra_subsystem.prepares == 1);
    rtos::clock_ms = 0xFFFFFF00U;
    robot_machine.update();
    tickAll();
    assert(test_motor.current == 10);
    for (float expected : {0.0f, -10.0f, 0.0f, 10.0f}) {
        rtos::clock_ms += 3000;
        robot_machine.update();
        tickAll();
        assert(test_motor.current == expected);
    }
    extra_subsystem.online = false;
    tickAll();
    rtos::clock_ms += 60000;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<TestRunning>() && !extra_subsystem.output);
    assert(extra_subsystem.hasFailed());
    extra_subsystem.online = true;
    tickAll();
    assert(extra_subsystem.hasFailed());
    // 集合外失败暂不改变整机测试策略；显式重新准备后才能回到待机。
    extra_subsystem.requestPrepare();
    tickAll();
    assert(extra_subsystem.isStandby());
    std::cout << "PASS: mode handoff reuses preparation, test sequence wraps and unused standby faults latch\n";

    const auto healthy_prepares = extra_subsystem.prepares;
    robot_context.recovery_requested.store(true); // A stale request must not authorize a new fault.
    test_motor.online = false;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>() && !robot_context.recovery_requested.load());
    assert(robot_context.faults[0] == Reason::SubsystemFailed && robot_context.faults[1] == Reason::None);
    robot_context.recovery_requested.store(true);
    assert(robot_machine.immediateChangeTo<Recovering>() == M::TransitionResult::RejectedByExitGuard);
    robot_context.recovery_requested.store(false);
    tickAll();
    assert(test_subsystem.state() == SubsystemState::Disabled && extra_subsystem.isStandby());
    test_motor.online = true;
    for (unsigned i = 0; i < 3; ++i) {
        tickAll();
        robot_machine.update();
        assert(robot_machine.isActive<Fault>() && test_subsystem.state() == SubsystemState::Disabled);
    }
    assert(robot_machine.immediateChangeTo<Starting>() == M::TransitionResult::RejectedByExitGuard);
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    assert(robot_machine.isActive<Recovering>() && !robot_context.recovery_requested.load());
    reach<TestRunning>();
    assert(extra_subsystem.prepares == healthy_prepares && extra_subsystem.isStandby());
    assert(robot_context.faults[0] == Reason::None);
    std::cout << "PASS: selective fault isolation, explicit exit flag and recovery of only failed devices\n";

    // 已经待机的集合外设备掉线属于真实故障，也需要修复后才能解除本轮 Fault。
    extra_subsystem.online = false;
    test_motor.online = false;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    tickAll();
    assert(robot_context.faults[1] == Reason::SubsystemFailed);
    test_motor.online = true;
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Recovering>());
    assert(extra_subsystem.state() == SubsystemState::Preparing);
    extra_subsystem.online = true;
    reach<TestRunning>();
    assert(extra_subsystem.isStandby());
    std::cout << "PASS: standby offline faults outside the test subset must also finish fault recovery\n";

    // Recovery failure must re-enter Fault and require a fresh request.
    test_motor.online = false;
    tickAll();
    robot_machine.update();
    tickAll();
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    assert(robot_machine.isActive<Recovering>());
    rtos::clock_ms += 60000;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Recovering>() && !test_subsystem.hasFailed());
    test_motor.online = true;
    invalid_output = true;
    tickAll();
    invalid_output = false;
    robot_machine.update();
    assert(robot_machine.isActive<Fault>() && !robot_context.recovery_requested.load());
    tickAll();
    for (unsigned i = 0; i < 3; ++i) robot_machine.update();
    assert(robot_machine.isActive<Fault>() && test_subsystem.state() == SubsystemState::Disabled);
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    reach<TestRunning>();
    std::cout << "PASS: offline recovery waits, failed recovery resets authorization and latches Fault\n";

    extra_subsystem.online = true;
    robot_machine.immediateChangeTo<Starting>();
    reach<UniversalManual>();
    const auto healthy_pid_inits = pid_init_count;
    robot_machine.immediateChangeTo<Starting>();
    tickAll();
    assert(test_subsystem.isStandby() && extra_subsystem.isStandby());
    assert(extra_subsystem.requestRun()); // Simulate a refused second run submission.
    robot_machine.update();
    assert(robot_machine.isActive<Fault>() && robot_context.faults[1] == Reason::RunRejected);
    tickAll();
    assert(test_subsystem.isStandby() && extra_subsystem.state() == SubsystemState::Disabled);
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    tickAll();
    robot_machine.update(); // Both run requests accepted, not yet executed.
    assert(robot_machine.isActive<Recovering>());
    extra_subsystem.run_success = false;
    tickAll();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>() && !robot_context.recovery_requested.load());
    assert(robot_context.faults[1] != Reason::None);
    extra_subsystem.run_success = true;
    tickAll();
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    reach<UniversalManual>();
    assert(pid_init_count == healthy_pid_inits);
    std::cout << "PASS: rejected run and asynchronous recovery failure return to Fault without resetting healthy peers\n";

    // A new failure while waiting in Fault must retain the earlier disabled subject.
    test_motor.online = false;
    tickAll();
    robot_machine.update();
    tickAll();
    extra_subsystem.run_success = false;
    tickAll();
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    assert(robot_machine.isActive<Fault>() && !robot_context.recovery_requested.load());
    tickAll();
    assert(test_subsystem.state() == SubsystemState::Disabled && extra_subsystem.state() == SubsystemState::Disabled);
    assert(robot_context.faults[0] != Reason::None && robot_context.faults[1] != Reason::None);
    test_motor.online = true;
    extra_subsystem.run_success = true;
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    reach<UniversalManual>();
    robot_machine.immediateChangeTo<Fault>(); // Unknown whole-robot fault defaults to all disabled.
    tickAll();
    assert(test_subsystem.state() == SubsystemState::Disabled && extra_subsystem.state() == SubsystemState::Disabled);
    assert(robot_context.faults[0] == Reason::Unspecified && robot_context.faults[1] == Reason::Unspecified);
    robot_context.recovery_requested.store(true);
    robot_machine.update();
    reach<UniversalManual>();
    std::cout << "PASS: new Fault subjects remain isolated until recovery, unknown faults disable all\n";
}
