#include "robot.hpp"
#include "states.hpp"

void robot::UniversalManual::entryGuard(robot::GuardControl& control) {
    for (const auto* subsystem : subsystems) {
        if (!subsystem->isRunning()) {
            control.cancelPendingTransitions();
            return;
        }
    }
}

void robot::UniversalManual::enter() {
    robot_context.faults.fill(Robot_Context::FaultReason::None);
}

void robot::UniversalManual::update(robot::Control& control) {
    for (const auto* subsystem : subsystems) {
        if (subsystem->hasFailed()) {
            control.changeTo<Fault>();
            return;
        }
    }
}

void robot::UniversalManual::exit() {}
