#include "subsystem.hpp"

#include "rtos_utility.hpp"

Subsystem::Subsystem(std::uint32_t preparation_timeout_ms) noexcept
    : preparation_timeout_ms_(preparation_timeout_ms) {}

void Subsystem::requestPrepare() {
    taskENTER_CRITICAL();
    ++latest_request_.generation;
    latest_request_.mode = Mode::Prepare;
    latest_request_.reset_preparation = true;
    onRequestReset();
    state_.store(State::Preparing);
    taskEXIT_CRITICAL();
}

bool Subsystem::requestStandby() {
    taskENTER_CRITICAL();
    const bool accepted = !hasFailed();
    if (accepted && latest_request_.mode != Mode::Standby && latest_request_.mode != Mode::Prepare) {
        latest_request_.reset_preparation = latest_request_.mode == Mode::Disabled;
        ++latest_request_.generation;
        latest_request_.mode = Mode::Standby;
        onRequestReset();
        state_.store(State::Preparing);
    }
    taskEXIT_CRITICAL();
    return accepted;
    // 
}

bool Subsystem::requestRun() {
    taskENTER_CRITICAL();
    const bool accepted = (latest_request_.mode == Mode::Prepare || latest_request_.mode == Mode::Standby) &&
                          isStandby();
    if (accepted) latest_request_.mode = Mode::Run;
    taskEXIT_CRITICAL();
    return accepted;
}

void Subsystem::requestDisable() {
    taskENTER_CRITICAL();
    ++latest_request_.generation;
    latest_request_.mode = Mode::Disabled;
    latest_request_.reset_preparation = true;
    onRequestReset();
    taskEXIT_CRITICAL();
}

Subsystem::State Subsystem::state() const noexcept { return state_.load(); }
bool Subsystem::isStandby() const noexcept { return state() == State::Standby; }
bool Subsystem::isRunning() const noexcept { return state() == State::Running; }
bool Subsystem::hasFailed() const noexcept { return state() == State::Failed; }
// 判断现在是否接受业务目标。
bool Subsystem::acceptsCommands() const noexcept {
    return latest_request_.mode == Mode::Run && isRunning();
}
// 默认实现：和正常运行一样，进入待机时保持输出；
// 比如让调用业务接口让机构回到初始位置，判断动作完成后返回ready，未完成返回Pending
Subsystem::Readiness Subsystem::onStandby(bool) {
    return onRun(true) ? Readiness::Ready : Readiness::Failed;
}

void Subsystem::periodic() {
    // 请求的快照
    Request request;
    taskENTER_CRITICAL();
    request = latest_request_;
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
    } 
    else if (!isOnline()) {
        (void)onRun(false);
        if (request.mode == Mode::Run || prepared_) {
            // 已建立运行条件后，进入/保持待机时掉线同样需要显式恢复。
            phase_ = State::Failed;
        } else {
            // 尚未建立运行条件时允许等待上线，不计准备超时。
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
                // 进这说明mode是Prepare或Standby，且已准备好，这里进入待机
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
    if (latest_request_.generation == request.generation && latest_request_.mode == request.mode) {
        state_.store(phase_);
    }
    taskEXIT_CRITICAL();
}
