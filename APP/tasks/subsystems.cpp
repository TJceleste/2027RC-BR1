#include "robot.hpp"
#include "rtos_task.hpp"
#include "rtos_utility.hpp"

rtos::Task<"test_subsystem", ([] {
    while (true) {
        const auto now = rtos::now_tick();
        robot::test_subsystem.periodic();
        rtos::delay_until(now + 1);
    }
}), 1024 * 4, rtos::TaskPriority::High> test_subsystem_task;
