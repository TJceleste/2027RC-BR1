#include "robot.hpp"
#include "states.hpp"

void robot::PrepareSubsystems::enter() {
    phase_ = Phase::Preparing;
    for (auto* subsystem : subsystems) {
        subsystem->requestPrepare();
    }
}

void robot::PrepareSubsystems::update(robot::Control& control) {
    bool all_ready = true;
    bool all_running = true;
    for (const auto* subsystem : subsystems) {
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

        // 只提交一次运行请求；任何请求被拒绝都由 Fault 统一撤销。
        for (auto* subsystem : subsystems) {
            if (!subsystem->requestRun()) {
                control.changeTo<Fault>();
                return;
            }
        }
        phase_ = Phase::WaitingRunning;
        return;
    }

    if (all_running) control.changeTo<UniversalManual>();
}

void robot::PrepareSubsystems::exit() {}
