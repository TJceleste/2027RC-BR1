#include "robot.hpp"

#include <cassert>
#include <iostream>

int main() {
    using namespace robot;
    using Status = Subsystem::StartupStatus;
    const auto tick_all = [] {
        for (auto* subsystem : test_subsystems) subsystem->periodic();
    };
    const auto prepare_all = [&] {
        robot_machine.immediateChangeTo<TestStarting>();
        robot_machine.update();
        assert(robot_machine.isActive<PrepareTestSubsystems>());
        tick_all();
        assert(test_subsystem.isReady() && extra_subsystem.isReady());
    };

    // 测试等待同样遍历整个测试集合，已在线机构独立待机。
    extra_subsystem.online = true;
    extra_subsystem.ready = true;
    robot_machine.update();
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<TestWaitPower>());
    assert(extra_subsystem.isStandby() && extra_subsystem.output);
    assert(test_subsystem.startupStatus() == Status::Idle);
    assert(extra_subsystem.prepares == 0 && pid_init_count == 0);
    rtos::clock_ms += 60000;
    robot_machine.update();
    tick_all();
    assert(extra_subsystem.isStandby() && !extra_subsystem.hasFailed());
    test_motor.online = true;
    prepare_all();
    robot_machine.update(); // 同时提交两份运行请求。
    test_subsystem.periodic(); // 第二个子系统的控制任务尚未运行。
    for (unsigned i = 0; i < 3; ++i) {
        robot_machine.update();
        assert(robot_machine.isActive<PrepareTestSubsystems>());
    }
    assert(test_subsystem.isRunning() && extra_subsystem.isReady());
    assert(test_motor.current == 0);
    assert(robot_machine.immediateChangeTo<TestRunning>() ==
           M::TransitionResult::RejectedByEntryGuard);
    assert(extra_subsystem.isReady()); // guard 没有请求运行或禁用。
    extra_subsystem.periodic();
    robot_machine.update();
    assert(robot_machine.isActive<TestRunning>());
    assert(extra_subsystem.isRunning() && test_motor.current == 0);
    robot_machine.update();
    tick_all();
    assert(test_motor.current == 10);
    std::cout << "PASS: wait for every participant to run, without repeated run requests or guard side effects\n";

    prepare_all();
    // 第二个子系统已有待执行请求，其状态仍为 Ready，但再次请求会被拒绝。
    assert(extra_subsystem.requestRun());
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    assert(test_subsystem.startupStatus() == Status::Disabling);
    assert(extra_subsystem.startupStatus() == Status::Disabling);
    tick_all();
    assert(!test_motor.enabled && !extra_subsystem.output);
    assert(test_subsystem.startupStatus() == Status::Idle);
    assert(extra_subsystem.startupStatus() == Status::Idle);
    std::cout << "PASS: a rejected participant rolls back requests already accepted by others\n";

    prepare_all();
    robot_machine.update();
    extra_subsystem.run_success = false;
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    tick_all();
    assert(!test_motor.enabled && !extra_subsystem.output);
    std::cout << "PASS: failure while confirming startup disables the whole test group\n";

    robot_machine.immediateChangeTo<TestStarting>();
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    tick_all();
    assert(!test_motor.enabled && !extra_subsystem.output);
    std::cout << "PASS: standby output failure during power wait enters Fault\n";
}
