#pragma once

#include "c620.hpp"
#include "pid_controller.hpp"
#include "subsystem.hpp"

namespace robot {

/**
 * @brief 单电机测试子系统：跨任务接收请求，由控制任务推进准备和速度闭环。
 * @note 请求和查询接口仅在任务上下文使用；periodic() 只能由一个任务调用。
 *       Ready 时禁止输出；Standby 以零速度闭环保持，Running 接受业务速度目标。
 */
class TestSubsystem final : public Subsystem {
public:
    explicit TestSubsystem(C620Motor& motor);

    [[nodiscard]] bool isOnline() const override;

    // 业务函数
    [[nodiscard]] bool setTargetSpeed(float speed);

private:
    // 控制任务回调；待机独立初始化速度闭环，不要求整机已完成准备。
    void onRequestReset() noexcept override;
    void onPrepare() override;
    [[nodiscard]] Readiness onStandby(bool entering) override;
    [[nodiscard]] Readiness onCheckReadiness() override;
    [[nodiscard]] bool onRun(bool enabled) override;

    C620Motor& motor_;
    PidController speed_pid_;
    float target_speed_{0}; // 请求方与控制任务通过短临界区同步。

    // 以下字段只由控制任务访问。
    float measured_speed_{0};
};

} // namespace robot
