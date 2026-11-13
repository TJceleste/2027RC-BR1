#include "robot.hpp"
#include "states.hpp"

void robot::TestWaitPower::enter() {
    // 已上线的子系统先待机
    for (auto* subsystem : test_subsystems) {
        if (subsystem->isOnline()) {
            (void)subsystem->requestStandby();
        }
    }
}

void robot::TestWaitPower::update(robot::Control& control) {
    // 等待列入 test_subsystems 的子系统全部上线
    bool all_online = true;
    for (auto* subsystem : test_subsystems) {
        if (subsystem->hasFailed()) {
            control.changeTo<Fault>();
            return;
        }
        if (subsystem->isOnline()) {
            (void)subsystem->requestStandby();
        } else {
            all_online = false;
        }
    }
    if (all_online) {
        control.changeTo<PrepareTestSubsystems>();
    }
}

void robot::TestWaitPower::exit() {}
