#include "test_subsystem.hpp"

#include "rtos_utility.hpp"

#include <cmath>

namespace robot {

TestSubsystem::TestSubsystem(C620Motor& motor)
    : motor_(motor),
      speed_pid_("test_motor_speed", {
          .kp = 400.0f,
          .ki = 3000.0f,
          .kd = 0.0f,
          .max_out = 3000.0f,
          .improvement = {1U << PidController::Improvement::increment_of_out}
      }) {}

bool TestSubsystem::isOnline() const {
    taskENTER_CRITICAL();
    const bool online = motor_.is_online();
    taskEXIT_CRITICAL();
    return online;
}

bool TestSubsystem::setTargetSpeed(float speed) {
    if (!std::isfinite(speed)) return false;

    // 为子系统业务功能要用到的数据添加临界区保护
    taskENTER_CRITICAL();
    // 待机、禁用和准备期间不接受测试任务遗留的速度目标。
    const bool running = isRunning();
    if (running) {
        target_speed_ = speed;
    }
    taskEXIT_CRITICAL();
    return running;
}

void TestSubsystem::onRequestReset() noexcept {
    target_speed_ = 0.0f;
}

void TestSubsystem::onPrepare() {
    speed_pid_.init();
}

Subsystem::Readiness TestSubsystem::onStandby(bool entering) {
    if (entering) {
        speed_pid_.init();
    }
    if (onCheckReadiness() != Readiness::Ready) {
        (void)onRun(false);
        return Readiness::Pending;
    }
    return onRun(true) ? Readiness::Ready : Readiness::Failed;
}

Subsystem::Readiness TestSubsystem::onCheckReadiness() {
    bool online;
    // FDCAN 接收中断优先级为 5，在此临界区内不会改写反馈。
    taskENTER_CRITICAL();
    online = motor_.is_online();
    measured_speed_ = motor_.get_current_speed();
    taskEXIT_CRITICAL();
    return online ? Readiness::Ready : Readiness::Pending;
}

// 子系统统一处理业务计算、输出应用和关闭。
bool TestSubsystem::onRun(bool enabled) {
    float current_ma = 0.0f;
    bool success = true;
    if (enabled) {
        float target_speed_snap;
        taskENTER_CRITICAL();
        target_speed_snap = target_speed_;
        taskEXIT_CRITICAL();
        current_ma = speed_pid_.calculate(measured_speed_, target_speed_snap);
        success = std::isfinite(current_ma);
    }

    // 电机命令也由 CAN 通信任务读取；这里只同步命令，不计算 PID 或检查生命周期请求。
    taskENTER_CRITICAL();
    if (enabled && success) {
        success = motor_.set_output_command({.current_ma = current_ma});
        if (success) {
            motor_.enable();
        } else {
            motor_.disable();
        }
    } else {
        motor_.disable();
    }
    taskEXIT_CRITICAL();
    return success;
}

} // namespace robot
