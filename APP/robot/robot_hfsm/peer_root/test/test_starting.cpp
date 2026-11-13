#include "robot.hpp"
#include "states.hpp"

void robot::TestStarting::enter() {
    robot_context.recovery_mode = Robot_Context::RecoveryMode::Test;
    phase_ = Phase::Preparing;
    for (auto* subsystem : test_subsystems) {
        if (subsystem->state() == Subsystem::State::Disabled || subsystem->hasFailed()) {
            subsystem->requestPrepare();
        } else {
            (void)subsystem->requestStandby();
        }
    }
}

void robot::TestStarting::update(robot::Control& control) {
    bool all_standby = true;
    bool all_running = true;
    for (const auto* subsystem : test_subsystems) {
        const auto state = subsystem->state();
        if (state == Subsystem::State::Failed) {
            control.changeTo<Fault>();
            return;
        }
        all_standby &= state == Subsystem::State::Standby;
        all_running &= state == Subsystem::State::Running;
    }
    // 仅下发一次请求
    if (phase_ == Phase::Preparing) {
        if (!all_standby) return;
        for (auto* subsystem : test_subsystems) {
            if (!subsystem->requestRun()) {
                auto& context = control.context();
                context.pending_fault_source = subsystem;
                context.pending_fault_reason = Robot_Context::FaultReason::RunRejected;
                control.changeTo<Fault>();
                return;
            }
        }
        phase_ = Phase::WaitingRunning;
        return;
    }
    if (all_running) control.changeTo<TestRunning>();
}

void robot::TestStarting::exit() {}
