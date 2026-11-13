#pragma once

#include <atomic>
#include <cstdint>

/**
 * @brief 子系统统一生命周期：待机、准备、运行、禁用以及跨任务状态查询。
 * @note 请求和查询接口用于任务上下文；periodic() 由一个固定的任务周期调用。
 *       请求异步执行，Ready 不允许输出，待机时按派生类策略保持输出。
 */
class Subsystem {
public:
    enum class StartupStatus : std::uint8_t {
        Idle,
        EnteringStandby,
        Standby,
        Preparing,
        Ready,
        Running,
        Disabling,
        Failed
    };

    virtual ~Subsystem() = default;
    Subsystem(const Subsystem&) = delete;
    Subsystem& operator=(const Subsystem&) = delete;
    Subsystem(Subsystem&&) = delete;
    Subsystem& operator=(Subsystem&&) = delete;

    // 请求待机，允许在整机等待其他设备上线时独立保持；重复请求不会重启待机。
    // 返回是否接受请求，Failed 时拒绝，故障恢复仍需重新准备。
    [[nodiscard]] bool requestStandby();

    // 总状态机启动、重启subsystem时调用这个接口进行初始化，会清空之前的运行状态
    // 子类通过重写虚函数 onPrepare() 来实现具体初始化逻辑
    void requestPrepare();

    // 准备完成后，总状态机调用接口这个启动。若准备未完成或失败，不会调用这个接口
    // 返回子系统是否进入 Run 模式，
    [[nodiscard]] bool requestRun();

    // 撤销当前请求、清空业务目标，并异步关闭输出；再次运行需要重新准备。
    void requestDisable();

    [[nodiscard]] StartupStatus startupStatus() const noexcept;
    [[nodiscard]] bool isStandby() const noexcept;
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool hasFailed() const noexcept;

    /**
     * @brief 本子系统所需设备是否在线，供整机在 prepare 前等待反馈。
     * @note 可由状态机任务查询；必须自行同步反馈读取，不得初始化设备或启动超时。
     *       不依赖外设的子系统可直接返回 true；在线不等于准备就绪。
     */
    [[nodiscard]] virtual bool isOnline() const = 0;

    /** @brief 推进一次生命周期，统一处理准备超时、故障保持和过期结果。 */
    void periodic();

protected:
    enum class Readiness : std::uint8_t { Pending, Ready, Failed };

    explicit Subsystem(std::uint32_t startup_timeout_ms = 5000U) noexcept;

    /** @brief 在请求方的短临界区内清空业务目标；不得操作硬件、阻塞或调用请求接口。 */
    virtual void onRequestReset() noexcept {}

    /** @brief 控制任务开始新一轮准备时调用一次，例如复位 PID；不得阻塞。 */
    virtual void onPrepare() {}

    /**
     * @brief 在线时由控制任务每周期执行待机，entering 仅在本轮首次执行时为 true。
     * @note 可独立于 prepare 执行，派生类须初始化待机所需的本地控制器并同步反馈。
     *       返回 Pending 表示仍在进入待机，Ready 表示已待机，Failed 表示失败。
     *       默认关闭输出并报告待机；需要主动保持的子系统覆盖此回调。
     */
    [[nodiscard]] virtual Readiness onStandby(bool entering);

    /**
     * @brief 控制任务检查准备进度/当前健康状况，可执行非阻塞的准备步骤。
     * @note 准备阶段 Pending 会继续等待，Failed 立即失败；就绪或运行后
     *       不再 Ready 则失败。设备反馈的同步由派生类负责。
     */
    [[nodiscard]] virtual Readiness onCheckReadiness() = 0;

    /**
     * @brief 每周期在临界区外处理业务及输出；enabled 为 true 时计算并应用输出。
     * @note enabled 为 false 时必须关闭并清空输出，不执行运行计算。
     *       运行失败时必须自行关闭输出并返回 false；设备保护及共享数据同步由派生类负责。
     *       执行期间的新请求在后续周期处理，基类只防止旧状态覆盖新请求状态。
     */
    [[nodiscard]] virtual bool onRun(bool enabled) = 0;

private:
    enum class Mode : std::uint8_t { Disabled, Standby, Prepare, Run };
    struct Request {
        Mode mode{Mode::Disabled};
        // 由于种种原因，子系统启动后可能会被 disable，然后被重新启动
        // 为了区分不同的启动周期，使用 generation 作为标识。
        std::uint32_t generation{0};
    };

    const std::uint32_t startup_timeout_ms_;
    Request request_{}; // 请求方和控制任务通过短临界区同步。
    std::atomic<StartupStatus> status_{StartupStatus::Idle};

    // 以下字段只由 periodic() 所在的控制任务访问。
    std::uint32_t active_generation_{0};
    std::uint32_t prepare_started_ms_{0};
    bool standby_started_{false};
    StartupStatus phase_{StartupStatus::Idle};
};
