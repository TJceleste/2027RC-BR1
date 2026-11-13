#pragma once

#include <atomic>
#include <cstdint>

/**
 * @brief 子系统独立等待上线、准备到待机，再接收业务运行请求。
 * @note 请求用于任务上下文，periodic() 由固定控制任务调用；输出切换异步执行。
 */
class Subsystem {
public:
    enum class State : std::uint8_t { Disabled, Preparing, Standby, Running, Failed };

    virtual ~Subsystem() = default;
    Subsystem(const Subsystem&) = delete;
    Subsystem& operator=(const Subsystem&) = delete;
    Subsystem(Subsystem&&) = delete;
    Subsystem& operator=(Subsystem&&) = delete;

    // 显式开始新一轮准备，也用于故障恢复；基类不限制准备时长。
    void requestPrepare();
    // 返回待机，复用有效准备结果；重复请求不重置，Failed 时拒绝。
    [[nodiscard]] bool requestStandby();
    // 只在待机且没有其他未处理请求时接受，返回是否接受。
    [[nodiscard]] bool requestRun();
    // 撤销业务目标和运行授权；控制任务关闭输出后才发布 Disabled。
    void requestDisable();

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool isStandby() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool hasFailed() const noexcept;
    void periodic();

protected:
    enum class Readiness : std::uint8_t { Pending, Ready, Failed };
    Subsystem() noexcept = default;

    // 子系统内部的设备事实；整机不检查在线，派生类须同步读取反馈。
    [[nodiscard]] virtual bool isOnline() const = 0;
    // 请求方短临界区内清空旧目标；不得阻塞、操作硬件或调用生命周期请求。
    virtual void onRequestReset() noexcept {}
    // 上线后开始实际准备时调用一次；正常返回待机不重复初始化。
    virtual void onPrepare() {}
    // 非阻塞准备步骤及运行条件检查；可执行准备所需的受控动作。
    [[nodiscard]] virtual Readiness onCheckReadiness() = 0;
    // 运行条件建立后，每周期进入/保持待机；默认和正常运行一样。
    [[nodiscard]] virtual Readiness onStandby(bool entering);
    // 执行业务输出；enabled=false 必须关闭并清空输出。所有回调须及时返回。
    [[nodiscard]] virtual bool onRun(bool enabled) = 0;
    // 业务目标写入的授权检查；调用方必须在同一个短临界区内检查并写入。
    [[nodiscard]] bool acceptsCommands() const noexcept;

private:
    enum class Mode : std::uint8_t { Disabled, Prepare, Standby, Run };
    struct Request {
        Mode mode{Mode::Disabled};
        std::uint32_t generation{0};
        bool reset_preparation{true};
    };

    Request latest_request_{}; // 请求方和控制任务通过短临界区同步。
    std::atomic<State> state_{State::Disabled};

    // 以下字段仅由控制任务访问。
    std::uint32_t active_generation_{0};
    bool preparation_started_{false};
    bool prepared_{false};
    bool standby_started_{false};
    State phase_{State::Disabled};
};
