#include "subsystem.hpp"

#include "rtos_utility.hpp"

Subsystem::Subsystem(std::uint32_t preparation_timeout_ms) noexcept
    : preparation_timeout_ms_(preparation_timeout_ms) {}

void Subsystem::requestPrepare() {
    taskENTER_CRITICAL();
    ++request_.generation;
    request_.mode = Mode::Prepare;
    request_.reset_preparation = true;
    onRequestReset();
    state_.store(State::Preparing);
    taskEXIT_CRITICAL();
}

bool Subsystem::requestStandby() {
    taskENTER_CRITICAL();
    const bool accepted = !hasFailed();
    if (accepted && request_.mode != Mode::Standby && request_.mode != Mode::Prepare) {
        request_.reset_preparation = request_.mode == Mode::Disabled;
        ++request_.generation;
        request_.mode = Mode::Standby;
        onRequestReset();
        state_.store(State::Preparing);
    }
    taskEXIT_CRITICAL();
    return accepted;
}

bool Subsystem::requestRun() {
    taskENTER_CRITICAL();
    const bool accepted = (request_.mode == Mode::Prepare || request_.mode == Mode::Standby) &&
                          isStandby();
    if (accepted) request_.mode = Mode::Run;
    taskEXIT_CRITICAL();
    return accepted;
}

void Subsystem::requestDisable() {
    taskENTER_CRITICAL();
    ++request_.generation;
    request_.mode = Mode::Disabled;
    request_.reset_preparation = true;
    onRequestReset();
    taskEXIT_CRITICAL();
}

Subsystem::State Subsystem::state() const noexcept { return state_.load(); }
bool Subsystem::isStandby() const noexcept { return state() == State::Standby; }
bool Subsystem::isRunning() const noexcept { return state() == State::Running; }
bool Subsystem::hasFailed() const noexcept { return state() == State::Failed; }
bool Subsystem::acceptsCommands() const noexcept {
    return request_.mode == Mode::Run && isRunning();
}

Subsystem::Readiness Subsystem::onStandby(bool) {
    return onRun(false) ? Readiness::Ready : Readiness::Failed;
}

void Subsystem::periodic() {
    Request request;
    taskENTER_CRITICAL();
    request = request_;
    taskEXIT_CRITICAL();
    const auto now = rtos::now_ms();

    if (request.generation != active_generation_) {
        active_generation_ = request.generation;
        preparation_started_ = false;
        standby_started_ = false;
        if (request.reset_preparation) {
            prepared_ = false;
            (void)onRun(false);
        }
        phase_ = request.mode == Mode::Disabled ? State::Disabled : State::Preparing;
    }

    if (request.mode == Mode::Disabled || phase_ == State::Failed) {
        (void)onRun(false);
    } else if (!isOnline()) {
        (void)onRun(false);
        if (request.mode == Mode::Run) {
            phase_ = State::Failed;
        } else {
            // 小电阶段只等待；待机机构重新上线后重建必要条件。
            phase_ = State::Preparing;
            prepared_ = false;
            preparation_started_ = false;
            standby_started_ = false;
        }
    } else {
        if (!preparation_started_) {
            preparation_started_ = true;
            preparation_started_ms_ = now;
            if (!prepared_) onPrepare();
        }

        const auto readiness = onCheckReadiness();
        if (readiness == Readiness::Failed ||
            (prepared_ && readiness != Readiness::Ready)) {
            phase_ = State::Failed;
        } else if (readiness == Readiness::Ready) {
            prepared_ = true;
            if (request.mode == Mode::Run) {
                phase_ = onRun(true) ? State::Running : State::Failed;
            } else {
                const auto standby = onStandby(!standby_started_);
                standby_started_ = true;
                if (standby == Readiness::Failed) phase_ = State::Failed;
                else phase_ = standby == Readiness::Ready ? State::Standby : State::Preparing;
            }
        }

        // 计时覆盖实际准备和进入待机；同周期完成时优先接受完成结果。
        if (phase_ == State::Preparing &&
            static_cast<std::uint32_t>(now - preparation_started_ms_) >=
                preparation_timeout_ms_) {
            phase_ = State::Failed;
        }
        if (phase_ == State::Failed) (void)onRun(false);
    }

    // 计算期间的新请求在后续周期执行；本轮可能产生一次旧输出，但不能覆盖新状态。
    taskENTER_CRITICAL();
    if (request_.generation == request.generation && request_.mode == request.mode) {
        state_.store(phase_);
    }
    taskEXIT_CRITICAL();
}
