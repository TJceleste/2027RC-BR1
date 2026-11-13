#include "asserter.hpp"
#include "robot.hpp"
#include "rtos_task.hpp"

rtos::Task<"robot_machine_task", [] {
    const auto result = robot::robot_machine.initialResult();

    RUNTIME_ASSERT(result == robot::M::TransitionResult::Changed);

    while (true) {
        robot::robot_machine.update();
        rtos::delay(20);
    }

}, 4096, rtos::TaskPriority::AboveNormal> robot_machine_task;
