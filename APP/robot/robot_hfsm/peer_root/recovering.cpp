#include "robot.hpp"
#include "states.hpp"

#include <algorithm>
#include <span>

void robot::Recovering::entryGuard(robot::GuardControl& control) {
    if (!control.isActive<Fault>() || !control.context().recovery_requested.load()) {
        control.cancelPendingTransitions();
    }
}

void robot::Recovering::enter() {
    phase_ = Phase::Preparing;
    for (auto* subsystem : subsystems) {
        if (subsystem->state() == Subsystem::State::Disabled || subsystem->hasFailed()) {
            subsystem->requestPrepare();
        } else {
            (void)subsystem->requestStandby();
        }
    }
}

void robot::Recovering::update(robot::Control& control) {
    auto& context = control.context();
    const auto active = context.recovery_mode == Robot_Context::RecoveryMode::Test
                            ? std::span<Subsystem* const>{test_subsystems}
                            : std::span<Subsystem* const>{subsystems};
    bool all_standby = true;
    bool all_running = true;
    for (std::size_t i = 0; i < subsystems.size(); ++i) {
        const bool participating = std::find(active.begin(), active.end(), subsystems[i]) !=
                                   active.end();
        if (!participating && context.faults[i] == Robot_Context::FaultReason::None) continue;
        const auto state = subsystems[i]->state();
        if (state == Subsystem::State::Failed) {
            control.changeTo<Fault>();
            return;
        }
        all_standby &= state == Subsystem::State::Standby;
        all_running &= participating ? state == Subsystem::State::Running
                                     : state == Subsystem::State::Standby;
    }

    if (phase_ == Phase::Preparing) {
        if (!all_standby) return;
        for (auto* subsystem : active) {
            if (!subsystem->requestRun()) {
                context.pending_fault_source = subsystem;
                context.pending_fault_reason = Robot_Context::FaultReason::RunRejected;
                control.changeTo<Fault>();
                return;
            }
        }
        phase_ = Phase::WaitingRunning;
        return;
    }
    if (all_running) {
        if (context.recovery_mode == Robot_Context::RecoveryMode::Test) {
            control.changeTo<TestRunning>();
        } else {
            control.changeTo<UniversalManual>();
        }
    }
}

void robot::Recovering::exit() {}
