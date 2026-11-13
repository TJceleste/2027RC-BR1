#include "robot.hpp"

#include <cassert>
#include <iostream>

int main() {
    using namespace robot;
    using Status = Subsystem::StartupStatus;
    const auto tick_all = [] {
        for (auto* subsystem : subsystems) subsystem->periodic();
    };
    assert(robot_machine.isActive<Disable>());
    assert(robot_machine.immediateChangeTo<UniversalManual>() ==
           M::TransitionResult::RejectedByEntryGuard);
    assert(robot_machine.immediateChangeTo<TestRunning>() ==
           M::TransitionResult::RejectedByEntryGuard);
    assert(robot_machine.isActive<Disable>());
    assert(test_subsystem.startupStatus() == Status::Idle);
    robot_machine.update();
    assert(robot_machine.isActive<TestWaitPower>());
    robot_machine.immediateChangeTo<Starting>();
    assert(robot_machine.isActive<Starting>() && robot_machine.isActive<WaitPower>());
    tick_all();
    assert(test_subsystem.startupStatus() == Status::Idle);
    assert(extra_subsystem.startupStatus() == Status::EnteringStandby);
    rtos::clock_ms = 60000;
    robot_machine.update();
    tick_all();
    assert(robot_machine.isActive<WaitPower>());
    assert(!test_subsystem.hasFailed() && !extra_subsystem.hasFailed());
    assert(pid_init_count == 0 && extra_subsystem.prepares == 0);
    assert(!test_motor.enabled && !extra_subsystem.output);
    // 第一个子系统离线不能阻止后面的在线子系统先待机。
    extra_subsystem.online = true;
    robot_machine.update();
    tick_all();
    assert(robot_machine.isActive<WaitPower>());
    assert(extra_subsystem.isStandby() && extra_subsystem.output);
    assert(test_subsystem.startupStatus() == Status::Idle && pid_init_count == 0);
    assert(extra_subsystem.prepares == 0);
    extra_subsystem.online = false;
    test_motor.online = true;
    robot_machine.update();
    tick_all();
    assert(robot_machine.isActive<WaitPower>());
    assert(test_subsystem.isStandby() && test_motor.enabled && test_motor.current == 0);
    assert(!extra_subsystem.output && !extra_subsystem.hasFailed());
    for (unsigned i = 0; i < 3; ++i) {
        rtos::clock_ms += 60000;
        robot_machine.update();
        tick_all();
    }
    assert(pid_init_count == 1 && extra_subsystem.prepares == 0);
    assert(test_subsystem.isStandby() && !extra_subsystem.hasFailed());
    extra_subsystem.online = true;
    robot_machine.update();
    assert(robot_machine.isActive<PrepareSubsystems>());
    assert(test_subsystem.startupStatus() == Status::Preparing);
    tick_all();
    assert(pid_init_count == 2 && extra_subsystem.prepares == 1);
    assert(test_subsystem.isReady());
    assert(extra_subsystem.startupStatus() == Status::Preparing);
    rtos::clock_ms += 4999;
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<PrepareSubsystems>() && !test_motor.enabled);
    assert(!extra_subsystem.hasFailed());
    std::cout << "PASS: waiting power exceeds 5 seconds without preparing or timing out\n";
    std::cout << "PASS: online subsystems hold standby while other devices remain offline\n";
    extra_subsystem.ready = true;
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<PrepareSubsystems>());
    assert(robot_machine.immediateChangeTo<UniversalManual>() ==
           M::TransitionResult::RejectedByEntryGuard);
    for (unsigned i = 0; i < 3; ++i) {
        robot_machine.update();
        assert(robot_machine.isActive<PrepareSubsystems>());
    }
    assert(test_subsystem.isReady() && extra_subsystem.isReady());
    test_subsystem.periodic();
    robot_machine.update();
    assert(robot_machine.isActive<PrepareSubsystems>() && extra_subsystem.isReady());
    assert(robot_machine.immediateChangeTo<UniversalManual>() ==
           M::TransitionResult::RejectedByEntryGuard);
    extra_subsystem.periodic();
    robot_machine.update();
    assert(robot_machine.isActive<UniversalManual>());
    tick_all();
    assert(test_subsystem.isRunning() && extra_subsystem.isRunning());
    assert(test_motor.enabled && extra_subsystem.output);
    robot_machine.update();
    tick_all();
    assert(test_motor.current == 0);
    rtos::clock_ms += 12000;
    robot_machine.update();
    tick_all();
    assert(test_motor.current == 0);
    std::cout << "PASS: all devices must be online before prepare, then all subsystems must be ready\n";
    std::cout << "PASS: UniversalManual does not generate test speed targets\n";

    // 正式模式切到测试模式：集合外机构待机，不能沿用正式模式的运行命令。
    assert(test_subsystem.setTargetSpeed(7));
    tick_all();
    assert(test_motor.current == 7);
    const auto background_prepares = extra_subsystem.prepares;
    robot_machine.immediateChangeTo<TestStarting>();
    assert(test_subsystem.startupStatus() == Status::EnteringStandby);
    assert(extra_subsystem.startupStatus() == Status::EnteringStandby);
    tick_all();
    assert(test_subsystem.isStandby() && test_motor.current == 0 && test_motor.enabled);
    assert(extra_subsystem.isStandby() && extra_subsystem.output);
    robot_machine.update();
    tick_all();
    robot_machine.update();
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<TestRunning>() && extra_subsystem.isStandby());
    assert(extra_subsystem.prepares == background_prepares);

    // 集合外机构掉线不阻塞测试，上线后由原待机请求重新保持。
    extra_subsystem.online = false;
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<TestRunning>() && !extra_subsystem.output);
    extra_subsystem.online = true;
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<TestRunning>() && extra_subsystem.isStandby());
    assert(extra_subsystem.prepares == background_prepares);

    // 返回正式启动也由目的状态接管，不经禁用。
    robot_machine.immediateChangeTo<Starting>();
    tick_all();
    assert(test_subsystem.isStandby() && extra_subsystem.isStandby());
    robot_machine.update();
    tick_all();
    robot_machine.update();
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<UniversalManual>());
    std::cout << "PASS: normal/test handoff keeps unused subsystems in standby without disabling\n";

    extra_subsystem.ready = false;
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    assert(test_subsystem.startupStatus() == Status::Disabling);
    assert(extra_subsystem.startupStatus() == Status::Disabling);
    tick_all();
    assert(!test_motor.enabled && !extra_subsystem.output);
    std::cout << "PASS: failure in second subsystem disables every subsystem\n";

    robot_machine.immediateChangeTo<Starting>();
    robot_machine.update();
    assert(robot_machine.isActive<PrepareSubsystems>());
    tick_all();
    rtos::clock_ms += 5000;
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    tick_all();
    assert(!test_motor.enabled);
    robot_machine.immediateChangeTo<Starting>();
    assert(robot_machine.isActive<WaitPower>());
    robot_machine.immediateChangeTo<Fault>();
    tick_all();
    assert(test_subsystem.startupStatus() == Status::Idle);
    assert(extra_subsystem.startupStatus() == Status::Idle);
    std::cout << "PASS: startup timeout and cancellation through Fault disable the whole group\n";

    test_motor.online = false;
    extra_subsystem.online = false;
    const auto extra_prepares = extra_subsystem.prepares;
    robot_machine.immediateChangeTo<TestStarting>();
    assert(robot_machine.isActive<TestWaitPower>());
    tick_all();
    rtos::clock_ms += 60000;
    robot_machine.update();
    tick_all();
    assert(robot_machine.isActive<TestWaitPower>());
    assert(test_subsystem.startupStatus() == Status::Idle);
    test_motor.online = true;
    robot_machine.update();
    assert(robot_machine.isActive<PrepareTestSubsystems>());
    tick_all();
    assert(test_subsystem.isReady());
    assert(robot_machine.immediateChangeTo<TestRunning>() ==
           M::TransitionResult::RejectedByEntryGuard);
    assert(robot_machine.isActive<PrepareTestSubsystems>() && !test_motor.enabled);
    robot_machine.update();
    // 接受请求不等于已经运行；重复更新不能重复提交 requestRun()。
    for (unsigned i = 0; i < 3; ++i) {
        assert(robot_machine.isActive<PrepareTestSubsystems>());
        robot_machine.update();
    }
    assert(test_subsystem.isReady() && !test_motor.enabled);
    tick_all();
    assert(test_subsystem.isRunning() && test_motor.enabled);
    assert(robot_machine.isActive<PrepareTestSubsystems>());
    robot_machine.update();
    assert(robot_machine.isActive<TestRunning>());
    assert(!extra_subsystem.isOnline() && !extra_subsystem.output);
    assert(extra_subsystem.startupStatus() == Status::EnteringStandby);
    assert(extra_subsystem.prepares == extra_prepares);
    std::cout << "PASS: TestStarting waits and prepares only the selected test subset\n";

    // The host PID substitute outputs target minus measured speed, exposing the
    // target commanded by the actual TestRunning state without a target getter.
    assert(test_motor.current == 0);
    rtos::clock_ms = 0xFFFFFF00U;
    robot_machine.update();
    tick_all();
    assert(test_motor.current == 10);
    rtos::clock_ms += 2999;
    robot_machine.update();
    tick_all();
    assert(test_motor.current == 10);
    ++rtos::clock_ms;
    robot_machine.update();
    tick_all();
    assert(test_motor.current == 0);
    for (float expected : {-10.0f, 0.0f, 10.0f, 0.0f, -10.0f}) {
        rtos::clock_ms += 3000;
        robot_machine.update();
        tick_all();
        assert(test_motor.current == expected);
    }
    std::cout << "PASS: TestRunning advances its 3-second speed sequence across clock wrap\n";

    robot_machine.immediateChangeTo<TestStarting>();
    tick_all();
    assert(test_subsystem.isStandby() && test_motor.enabled && test_motor.current == 0);
    robot_machine.update();
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<PrepareTestSubsystems>());
    // requestRun() has been accepted, but the control task has not handled it.
    robot_machine.update();
    assert(test_motor.current == 0 && !test_motor.enabled);
    tick_all();
    assert(test_subsystem.isRunning() && test_motor.current == 0);
    robot_machine.update();
    assert(robot_machine.isActive<TestRunning>() && test_motor.current == 0);
    robot_machine.update();
    tick_all();
    assert(test_motor.current == 10);
    rtos::clock_ms += 3000;
    robot_machine.update();
    tick_all();
    assert(test_motor.current == 0);
    std::cout << "PASS: re-entering TestRunning restarts at 10 after the subsystem is running\n";

    test_motor.online = false;
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    tick_all();
    assert(!test_motor.enabled && test_subsystem.startupStatus() == Status::Idle);
    robot_machine.immediateChangeTo<TestStarting>();
    robot_machine.immediateChangeTo<Fault>();
    tick_all();
    assert(test_subsystem.startupStatus() == Status::Idle);
    assert(extra_subsystem.startupStatus() == Status::Idle);
    std::cout << "PASS: test feedback loss and cancellation through Fault disable every subsystem\n";

    // 运行确认前取消，已接受的请求由 Fault::enter() 统一撤销。
    test_motor.online = true;
    robot_machine.immediateChangeTo<TestStarting>();
    robot_machine.update();
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<PrepareTestSubsystems>() && test_subsystem.isReady());
    robot_machine.immediateChangeTo<Fault>();
    tick_all();
    assert(test_subsystem.startupStatus() == Status::Idle && !test_motor.enabled);

    // 在准备完成后预先提交一次请求，模拟编排提交时 requestRun() 被拒绝。
    robot_machine.immediateChangeTo<TestStarting>();
    robot_machine.update();
    tick_all();
    assert(test_subsystem.requestRun());
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    tick_all();
    assert(test_subsystem.startupStatus() == Status::Idle && !test_motor.enabled);

    // 运行请求接受后、控制任务实际启动时失败，由准备状态处理故障。
    robot_machine.immediateChangeTo<TestStarting>();
    robot_machine.update();
    tick_all();
    robot_machine.update();
    test_motor.online = false;
    tick_all();
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    tick_all();
    assert(!test_motor.enabled);
    std::cout << "PASS: guard rejects non-running participants, cancellation and start failure are handled before TestRunning\n";

    assert(robot_machine.immediateChangeTo<UniversalManual>() ==
           M::TransitionResult::RejectedByEntryGuard);
    assert(robot_machine.isActive<Fault>());
    tick_all();
    assert(!test_motor.enabled && !extra_subsystem.output);
    std::cout << "PASS: UniversalManual guard rejects entry without running participants\n";

    // 正式分支中第二个请求被拒绝，同一次状态机更新必须进入 Fault 撤销全部请求。
    test_motor.online = true;
    extra_subsystem.online = true;
    extra_subsystem.ready = true;
    robot_machine.immediateChangeTo<Starting>();
    robot_machine.update();
    tick_all();
    assert(test_subsystem.isReady() && extra_subsystem.isReady());
    assert(extra_subsystem.requestRun());
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    assert(test_subsystem.startupStatus() == Status::Disabling);
    assert(extra_subsystem.startupStatus() == Status::Disabling);
    tick_all();
    assert(!test_motor.enabled && !extra_subsystem.output);

    // 接受运行请求后异步失败，也不能先进入 UniversalManual。
    robot_machine.immediateChangeTo<Starting>();
    robot_machine.update();
    tick_all();
    robot_machine.update();
    extra_subsystem.run_success = false;
    tick_all();
    assert(test_subsystem.isRunning() && extra_subsystem.hasFailed());
    robot_machine.update();
    assert(robot_machine.isActive<Fault>());
    assert(test_subsystem.startupStatus() == Status::Disabling);
    tick_all();
    assert(!test_motor.enabled && !extra_subsystem.output);
    std::cout << "PASS: normal startup rejection and asynchronous failure enter Fault before business\n";
}
