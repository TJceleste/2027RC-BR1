#include "robot.hpp"
#include "rtos_task.hpp"
#include "rtos_utility.hpp"

// EB: 这里创建子系统的周期任务，周期调用子系统的 periodic()，用于处理准备、待机和运行请求
// 这里的"test_subsystem"可以改
rtos::Task<"test_subsystem", ([] {
    while (true) {
        const auto now = rtos::now_tick();
        // 这里的test_subsystem可以改成你要测试的机构类的实例
        robot::test_subsystem.periodic();
        // 你也可以修改周期
        rtos::delay_until(now + 1);
    }
    // 优先级别动，这里的test_subsystem_task可以改
}), 1024 * 4, rtos::TaskPriority::High> test_subsystem_task;

// 如果要测试多个子系统，推荐再创建一个任务，免得被其他子系统阻塞，创建方式参照上面的例子，复制下来改改就行
// 只需要复制上面那一点就行，zhy框架简化了开任务的部分
// 要是实在不习惯，用老一套也不是不行（，不过很不推荐
