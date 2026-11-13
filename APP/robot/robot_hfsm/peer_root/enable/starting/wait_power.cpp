#include "robot.hpp"
#include "states.hpp"

void robot::WaitPower::enter() {
    for (auto* subsystem : subsystems) {
        if (subsystem->isOnline()) {
            (void)subsystem->requestStandby();
        }
    }
}

void robot::WaitPower::update(robot::Control& control) {
    // 人工接大电所需时间不限；已在线的机构先待机，不启动 prepare 超时。
    bool all_online = true;
    for (auto* subsystem : subsystems) {
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
        control.changeTo<PrepareSubsystems>();
    }
}

void robot::WaitPower::exit() {}
