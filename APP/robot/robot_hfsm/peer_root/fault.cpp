#include "robot.hpp"
#include "states.hpp"

void robot::Fault::enter() {
    reentering_ = false;
    auto& context = robot_context;
    context.recovery_requested.store(false);
    // 本轮恢复成功前保留已有故障，重入 Fault 不能使先前禁用的机构自动待机。
    bool identified = false;
    // 必须先保存故障来源，再禁用；禁用完成后子系统不再处于 Failed。
    for (std::size_t i = 0; i < subsystems.size(); ++i) {
        // 所有子系统运行后的故障都归类为 SubsystemFailed，进else
        // 只有启动阶段的故障才进 if，记录来源和原因。
        if (subsystems[i] == context.pending_fault_source) {
            context.faults[i] = context.pending_fault_reason;
        } else if (subsystems[i]->hasFailed()) {
            context.faults[i] = Robot_Context::FaultReason::SubsystemFailed;
        }
        identified |= context.faults[i] != Robot_Context::FaultReason::None;
    }
    context.pending_fault_source = nullptr;
    context.pending_fault_reason = Robot_Context::FaultReason::None;
    
    // 没有明确来源的故障采用禁用所有子系统的策略
    if (!identified) context.faults.fill(Robot_Context::FaultReason::Unspecified);

    for (std::size_t i = 0; i < subsystems.size(); ++i) {
        if (context.faults[i] != Robot_Context::FaultReason::None) {
            subsystems[i]->requestDisable();
        } else if (!subsystems[i]->requestStandby()) {
            context.faults[i] = Robot_Context::FaultReason::SubsystemFailed;
            subsystems[i]->requestDisable();
        }
    }
}

void robot::Fault::update(robot::Control& control) {
    auto& context = control.context();
    for (std::size_t i = 0; i < subsystems.size(); ++i) {
        if (context.faults[i] == Robot_Context::FaultReason::None &&
            subsystems[i]->hasFailed()) {
            // 待机机构出现新故障时重新执行 Fault::enter() 的处置策略。
            reentering_ = true;
            control.restart<Fault>();
            return;
        }
        if (context.faults[i] != Robot_Context::FaultReason::None &&
            subsystems[i]->state() != Subsystem::State::Disabled) return;
    }
    // 恢复输入预留：只有显式置位才开始恢复，设备上线本身不会使整机离开 Fault。
    if (context.recovery_requested.load()) control.changeTo<Recovering>();
}

void robot::Fault::exitGuard(robot::GuardControl& control) {
    // 故障扩大时仅重入当前 Fault，不属于离开故障状态，无需恢复授权。
    if (reentering_) return;
    const auto& context = control.context();
    if (!context.recovery_requested.load()) {
        control.cancelPendingTransitions();
        return;
    }
    for (std::size_t i = 0; i < subsystems.size(); ++i) {
        // 如果仍有fault未被处理，拒绝退出
        if (subsystems[i]->hasFailed() ||
            (context.faults[i] != Robot_Context::FaultReason::None &&
             subsystems[i]->state() != Subsystem::State::Disabled)) {
            control.cancelPendingTransitions();
            return;
        }
    }
}

void robot::Fault::exit() {
    robot_context.recovery_requested.store(false);
}
