#include "robot.h"
#include "robot.hpp"

void robot_init(void) {
    robot::init();
    robot::robot_machine.update();
}
