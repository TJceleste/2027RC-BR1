#include "robot.hpp"
#include "states.hpp"

void robot::Fault::enter() {
    for (auto* subsystem : subsystems) {
        subsystem->requestDisable();
    }
}

void robot::Fault::update(robot::Control&) {}

void robot::Fault::exit() {}
