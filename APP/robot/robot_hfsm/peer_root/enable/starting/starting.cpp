#include "robot.hpp"
#include "states.hpp"

void robot::Starting::enter() {
    robot_context.recovery_mode = Robot_Context::RecoveryMode::Manual;
    phase_ = Phase::Preparing;
    for (auto* subsystem : subsystems) {
        if (subsystem->state() == Subsystem::State::Disabled || subsystem->hasFailed()) {
            subsystem->requestPrepare();
        } else {
            (void)subsystem->requestStandby();
        }
    }
}

void robot::Starting::update(robot::Control& control) {
    bool all_standby = true;
    bool all_running = true;
    for (const auto* subsystem : subsystems) {
        const auto state = subsystem->state();
        if (state == Subsystem::State::Failed) {
            control.changeTo<Fault>();
            return;
        }
        all_standby &= state == Subsystem::State::Standby;
        all_running &= state == Subsystem::State::Running;
    }
    if (phase_ == Phase::Preparing) {
        if (!all_standby) return;
        for (auto* subsystem : subsystems) {
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
    if (all_running) control.changeTo<UniversalManual>();
}

void robot::Starting::exit() {}
