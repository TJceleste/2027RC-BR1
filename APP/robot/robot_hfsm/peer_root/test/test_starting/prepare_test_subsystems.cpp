#include "robot.hpp"
#include "states.hpp"

void robot::PrepareTestSubsystems::enter() {
    phase_ = Phase::Preparing;
    for (auto* subsystem : test_subsystems) {
        subsystem->requestPrepare();
    }
}

void robot::PrepareTestSubsystems::update(robot::Control& control) {
    bool all_ready = true;
    bool all_running = true;
    for (const auto* subsystem : test_subsystems) {
        const auto status = subsystem->startupStatus();
        if (status == Subsystem::StartupStatus::Failed) {
            control.changeTo<Fault>();
            return;
        }
        all_ready &= status == Subsystem::StartupStatus::Ready;
        all_running &= status == Subsystem::StartupStatus::Running;
    }

    if (phase_ == Phase::Preparing) {
        if (!all_ready) return;

        // 只提交一次运行请求；任意请求被拒绝就进入 Fault 统一撤销，不重试。
        for (auto* subsystem : test_subsystems) {
            if (!subsystem->requestRun()) {
                control.changeTo<Fault>();
                return;
            }
        }
        phase_ = Phase::WaitingRunning;
        return;
    }

    // 请求由各控制任务异步执行，全部实际运行后才进入测试业务状态。
    if (all_running) control.changeTo<TestRunning>();
}

void robot::PrepareTestSubsystems::exit() {}
