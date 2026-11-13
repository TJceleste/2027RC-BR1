#include "robot.hpp"
#include "states.hpp"
#include "rtos_utility.hpp"

void robot::TestRunning::entryGuard(robot::GuardControl& control) {
    for (const auto* subsystem : test_subsystems) {
        if (!subsystem->isRunning()) {
            control.cancelPendingTransitions();
            return;
        }
    }
}

void robot::TestRunning::enter() {
    robot_context.faults.fill(Robot_Context::FaultReason::None);
    
    // EB: 这里初始化测试业务需要用的量 begin
    speed_sequence_active_ = false;
    speed_phase_ = 0;
    speed_phase_started_ms_ = 0;
    // EB: end
}

void robot::TestRunning::update(robot::Control& control) {
    for (const auto* subsystem : test_subsystems) {
        if (subsystem->hasFailed()) {
            control.changeTo<Fault>();
            return;
        }
    }

    // EB: 这里写测试业务逻辑
    constexpr float speeds[]{10.0f, 0.0f, -10.0f, 0.0f};
    const auto now = rtos::now_ms();
    if (!speed_sequence_active_) {
        speed_sequence_active_ = true;
        speed_phase_ = 0;
        speed_phase_started_ms_ = now;
        (void)test_subsystem.setTargetSpeed(speeds[speed_phase_]);
    } else if (static_cast<std::uint32_t>(now - speed_phase_started_ms_) >= 3000U) {
        speed_phase_ = (speed_phase_ + 1U) % 4U;
        speed_phase_started_ms_ = now;
        (void)test_subsystem.setTargetSpeed(speeds[speed_phase_]);
    }
    // EB: end
}

void robot::TestRunning::exit() {
}
