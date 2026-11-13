#include "subsystem.hpp"

#include "rtos_utility.hpp"

Subsystem::Subsystem(std::uint32_t startup_timeout_ms) noexcept
    : startup_timeout_ms_(startup_timeout_ms) {}

bool Subsystem::requestStandby() {
    taskENTER_CRITICAL();
    const bool accepted = !hasFailed();
    if (accepted && request_.mode != Mode::Standby) {
        ++request_.generation;
        request_.mode = Mode::Standby;
        onRequestReset();
        status_.store(StartupStatus::EnteringStandby);
    }
    taskEXIT_CRITICAL();
    return accepted;
}

void Subsystem::requestPrepare() {
    taskENTER_CRITICAL();
    ++request_.generation;
    request_.mode = Mode::Prepare;
    onRequestReset();
    status_.store(StartupStatus::Preparing);
    taskEXIT_CRITICAL();
}

bool Subsystem::requestRun() {
    taskENTER_CRITICAL();
    const bool ready = request_.mode == Mode::Prepare && isReady();
    if (ready) {
        request_.mode = Mode::Run;
    }
    taskEXIT_CRITICAL();
    return ready;
}

void Subsystem::requestDisable() {
    taskENTER_CRITICAL();
    ++request_.generation;
    request_.mode = Mode::Disabled;
    onRequestReset();
    status_.store(StartupStatus::Disabling);
    taskEXIT_CRITICAL();
}

Subsystem::StartupStatus Subsystem::startupStatus() const noexcept {
    return status_.load();
}

bool Subsystem::isStandby() const noexcept {
    return startupStatus() == StartupStatus::Standby;
}

bool Subsystem::isReady() const noexcept {
    return startupStatus() == StartupStatus::Ready;
}

bool Subsystem::isRunning() const noexcept {
    return startupStatus() == StartupStatus::Running;
}

bool Subsystem::hasFailed() const noexcept {
    return startupStatus() == StartupStatus::Failed;
}

Subsystem::Readiness Subsystem::onStandby(bool) {
    return onRun(false) ? Readiness::Ready : Readiness::Failed;
}

void Subsystem::periodic() {
    // 拷贝最后一次请求
    Request request;
    taskENTER_CRITICAL();
    request = request_;
    taskEXIT_CRITICAL();

    const auto now = rtos::now_ms();
    
    // 新请求开始独立的一轮待机、准备或禁用。
    if (request.generation != active_generation_) {
        active_generation_ = request.generation;
        prepare_started_ms_ = now;
        standby_started_ = false;
        if (request.mode == Mode::Disabled) {
            phase_ = StartupStatus::Idle;
        } else if (request.mode == Mode::Standby) {
            phase_ = StartupStatus::EnteringStandby;
        } else {
            phase_ = StartupStatus::Preparing;
        }
        if (phase_ == StartupStatus::Preparing) {
            onPrepare();
        }
    }
    // 如果当前处于准备、就绪或运行状态
    if (phase_ == StartupStatus::Preparing || phase_ == StartupStatus::Ready ||
        phase_ == StartupStatus::Running) {
        // 读取准备状态
        const auto readiness = onCheckReadiness();
        // 如果当前是准备状态，检查是否已就绪或失败（包括超时）
        if (phase_ == StartupStatus::Preparing) {
            if (readiness == Readiness::Ready) {
                phase_ = StartupStatus::Ready;
            } else if (readiness == Readiness::Failed ||
                       static_cast<std::uint32_t>(now - prepare_started_ms_) >=
                           startup_timeout_ms_) {
                phase_ = StartupStatus::Failed;
            }
        } else if (readiness != Readiness::Ready) {
            // 失败保持到下一次 prepare/disable，不在设备恢复后自行允许输出。
            phase_ = StartupStatus::Failed;
        }
    }

    // 按本周期的请求快照执行业务和输出；期间的新请求留到后续周期处理。
    if (request.mode == Mode::Standby && phase_ != StartupStatus::Failed) {
        if (isOnline()) {
            const auto readiness = onStandby(!standby_started_);
            standby_started_ = true;
            if (readiness == Readiness::Failed) {
                phase_ = StartupStatus::Failed;
                (void)onRun(false);
            } else {
                phase_ = readiness == Readiness::Ready
                             ? StartupStatus::Standby : StartupStatus::EnteringStandby;
            }
        } else {
            // 等待大电期间掉线不记为准备失败，关闭输出并等待重新上线。
            standby_started_ = false;
            phase_ = StartupStatus::EnteringStandby;
            (void)onRun(false);
        }
    } else {
        const bool enable_output = request.mode == Mode::Run &&
                                   (phase_ == StartupStatus::Ready ||
                                    phase_ == StartupStatus::Running);
        const bool success = onRun(enable_output);
        if (enable_output) {
            phase_ = success ? StartupStatus::Running : StartupStatus::Failed;
        }
    }

    // 只保护状态发布，避免旧周期覆盖新请求公布的状态。
    taskENTER_CRITICAL();
    if (request_.generation == request.generation &&
        request_.mode == request.mode) {
        status_.store(phase_);
    }
    taskEXIT_CRITICAL();
}
