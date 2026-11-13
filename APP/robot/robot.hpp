#pragma once

#include "asserter.hpp"
#include "main.h"
#include "memory_map.h"
#include "robot_hfsm/config.hpp"
#include "rtos_utility.hpp"
#include "hfsm.hpp"
#include "robot_hfsm/states.hpp"
#include "usart.h"
#include "can_bus.hpp"
#include "bsp_dwt.hpp"
#include "rs485_bus.hpp"
#include "rtos_task.hpp"
#include "uart_port.hpp"
#include "c620.hpp"
#include "test_subsystem.hpp"

#include <array>
#include <atomic>

namespace robot {

inline bool initialized{false};

inline CanBus can1_bus{hfdcan1};
inline CanBus can2_bus{hfdcan2};
inline CanBus can3_bus{hfdcan3};

inline auto can_communication_entry{([] {
    while (true) {
        (void) robot::can1_bus.periodic();
        rtos::delay(1);
    }
})};
RAM_D1_BSS inline rtos::Task<
    "can_communication",
    can_communication_entry,
    2048 * 4,
    rtos::TaskPriority::Realtime
> can_communication_task;

inline C620 can1_c620{can1_bus};
inline C620Motor test_motor{can1_c620, 1};
inline TestSubsystem test_subsystem{test_motor};

// 全部子系统的注册表，也是正式启动集合；Fault 按故障记录选择禁用对象。
// 各控制任务仍负责调用 periodic()。
inline const std::array<Subsystem*, 1> subsystems{&test_subsystem};

// 必须是 subsystems 的子集；测试只等待并准备此集合，其余子系统保持待机。
inline const std::array<Subsystem*, 1> test_subsystems{&test_subsystem};

DMA_BUFFER_BSS inline UartPortDma<32, 32, 32 * 2> uart2_port{huart2, {}};
inline Rs485Bus rs4851_bus{uart2_port};

/**
 * @brief 初始化 DWT 并启动三路 CAN；CAN 设备已在构造时自动注册。
 * @note 由 MX_FREERTOS_Init() 在 rtos_resource_init() 完成后、调度器启动前调用；
 *       此时外设、队列、流缓冲区和任务已创建，业务任务尚未开始执行。
 * @note 不得调用任务延时或阻塞等待等依赖调度器运行的操作。
 * @note 初始化完成后设置 initialized，重复调用直接返回。
 */
inline void init() {
    if (initialized) return;

    // CYCCNT 使用 CPU 内核时钟，不能使用二分频
    // 后的 HCLK。
    Dwt::instance().init(SystemCoreClock / 1'000'000U);

    // 各总线依次建立映射、配置过滤器并启动，保留构造阶段注册的设备。
    for (auto *bus : {&can1_bus, &can2_bus, &can3_bus}) {
        const bool ready = bus->init();
        RUNTIME_ASSERT(ready);
    }

    initialized = true;
}

struct Robot_Context {
    enum class RecoveryMode { Manual, Test };
    enum class FaultReason { None, SubsystemFailed, RunRejected, Unspecified };

    RecoveryMode recovery_mode{RecoveryMode::Manual};
    // 进入 Fault 前记录请求拒绝等不会直接改变子系统状态的故障。
    Subsystem* pending_fault_source{nullptr};
    FaultReason pending_fault_reason{FaultReason::None};
    std::array<FaultReason, subsystems.size()> faults{};
    // 预留给操作员/上位机恢复命令。唯一可由其他任务直接修改的上下文字段。
    // 每次进入 Fault 清零；显式置 true 后才允许离开 Fault，离开时消费。
    std::atomic<bool> recovery_requested{false};
};

using Tree = M::PeerRoot<
    M::Leaf< Disable >,
    M::Composite< Enable,
        M::Leaf< Starting >,
        M::Composite< Manual,
            M::Leaf< UniversalManual >
        >,
        M::Composite< Auto,
            M::Leaf< WaitUpperComputer >,
            M::Leaf< DevelopingAuto >,
            M::Leaf< Paused >
        >
    >,
    M::Composite< Test,
        M::Leaf< TestStarting >,
        M::Leaf< TestRunning >
    >,
    M::Leaf< Fault >,
    M::Leaf< Recovering >
>;

inline Robot_Context robot_context{};
using Machine = M::Instance<Tree>;
inline Machine robot_machine(robot_context, M::Initial<Disable>());
}; //namespace Robot
