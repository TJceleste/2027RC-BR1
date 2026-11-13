#include "robot.hpp"
#include "states.hpp"

#include <algorithm>

void robot::Test::enter() {
    // 接管测试集合之外的机构，撤销原模式目标；离线机构上线后自行进入待机。
    for (auto* subsystem : subsystems) {
        if (std::find(test_subsystems.begin(), test_subsystems.end(), subsystem) ==
            test_subsystems.end()) {
            (void)subsystem->requestStandby();
        }
    }
}

void robot::Test::update(robot::Control& control) {

}

void robot::Test::exit() {}
