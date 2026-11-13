#include "test_subsystem.hpp"
#include "rtos_utility.hpp"

#include <cassert>
#include <functional>
#include <iostream>
#include <limits>

class ProbeSubsystem final : public Subsystem {
public:
    bool online{false};
    bool ready{false};
    bool broken{false};
    bool standby_ready{true};
    bool run_success{true};
    bool output{false};
    unsigned prepares{0};
    unsigned standby_entries{0};
    std::function<void()> during_run;
    bool acceptsTarget() {
        taskENTER_CRITICAL();
        const bool accepted = acceptsCommands();
        taskEXIT_CRITICAL();
        return accepted;
    }
private:
    bool isOnline() const override { return online; }
    void onRequestReset() noexcept override { assert(critical_depth > 0); }
    void onPrepare() override { assert(critical_depth == 0); ++prepares; }
    Readiness onCheckReadiness() override {
        assert(critical_depth == 0);
        return broken ? Readiness::Failed : ready ? Readiness::Ready : Readiness::Pending;
    }
    Readiness onStandby(bool entering) override {
        assert(critical_depth == 0);
        if (entering) ++standby_entries;
        output = true;
        return standby_ready ? Readiness::Ready : Readiness::Pending;
    }
    bool onRun(bool enabled) override {
        assert(critical_depth == 0);
        output = enabled && run_success;
        if (enabled && during_run) {
            auto hook = during_run;
            during_run = {};
            hook();
        }
        return !enabled || run_success;
    }
};

int main() {
    using State = Subsystem::State;
    ProbeSubsystem probe;
    assert(probe.state() == State::Disabled && !probe.requestRun());
    probe.requestPrepare();
    probe.periodic();
    rtos::clock_ms = 60000;
    probe.periodic();
    assert(probe.state() == State::Preparing && probe.prepares == 0 && !probe.output);
    probe.online = true;
    probe.periodic();
    assert(probe.prepares == 1 && probe.state() == State::Preparing);
    rtos::clock_ms += 60000;
    probe.periodic();
    assert(probe.state() == State::Preparing && probe.prepares == 1 && !probe.output);
    probe.broken = true;
    probe.periodic();
    assert(probe.hasFailed() && !probe.output);
    probe.broken = false;
    probe.ready = true;
    assert(!probe.requestStandby());
    probe.periodic();
    assert(probe.hasFailed());
    probe.requestPrepare();
    probe.periodic();
    assert(probe.isStandby() && probe.output && probe.prepares == 2);
    assert(probe.requestRun() && probe.isStandby());
    assert(!probe.requestRun());
    probe.periodic();
    assert(probe.isRunning() && probe.acceptsTarget());
    assert(probe.requestStandby() && !probe.acceptsTarget());
    probe.periodic();
    assert(probe.isStandby() && probe.prepares == 2);
    const auto entries = probe.standby_entries;
    assert(probe.requestStandby());
    probe.periodic();
    assert(probe.standby_entries == entries && probe.prepares == 2);
    std::cout << "PASS: independent online wait, untimed preparation, explicit failure and run acknowledgement\n";

    probe.online = false;
    probe.periodic();
    rtos::clock_ms += 60000;
    probe.periodic();
    assert(probe.hasFailed() && !probe.output);
    probe.online = true;
    probe.periodic();
    assert(probe.hasFailed() && !probe.requestStandby() && probe.prepares == 2);
    probe.requestPrepare();
    probe.periodic();
    assert(probe.isStandby() && probe.prepares == 3);
    assert(probe.requestRun());
    probe.periodic();
    probe.online = false;
    probe.periodic();
    assert(probe.hasFailed());
    probe.online = true;
    probe.periodic();
    assert(probe.hasFailed());
    probe.requestPrepare();
    probe.periodic();
    assert(probe.requestRun());
    probe.periodic();
    probe.during_run = [&] { probe.requestDisable(); };
    probe.periodic();
    assert(probe.isRunning() && !probe.acceptsTarget());
    probe.periodic();
    assert(probe.state() == State::Disabled && !probe.output);
    assert(probe.requestStandby());
    probe.periodic();
    assert(probe.isStandby());
    const auto prepared = probe.prepares;
    assert(probe.requestRun());
    probe.periodic();
    probe.during_run = [&] { probe.requestPrepare(); };
    probe.periodic();
    assert(probe.state() == State::Preparing && !probe.acceptsTarget());
    probe.periodic();
    assert(probe.isStandby() && probe.prepares == prepared + 1);
    // Disable and standby before the next control cycle must invalidate old preparation.
    probe.requestDisable();
    assert(probe.requestStandby());
    probe.periodic();
    assert(probe.isStandby() && probe.prepares == prepared + 2);
    std::cout << "PASS: healthy standby reuse, standby offline fault latch and concurrent requests\n";

    // Prepare 模式完成后也会处于 Standby，不能只按请求模式判断掉线。
    ProbeSubsystem prepare_to_standby;
    prepare_to_standby.requestPrepare();
    rtos::clock_ms += 60000;
    prepare_to_standby.periodic();
    assert(!prepare_to_standby.hasFailed() && prepare_to_standby.prepares == 0);
    prepare_to_standby.online = true;
    prepare_to_standby.ready = true;
    prepare_to_standby.periodic();
    assert(prepare_to_standby.isStandby());
    prepare_to_standby.online = false;
    prepare_to_standby.periodic();
    assert(prepare_to_standby.hasFailed() && !prepare_to_standby.output);

    ProbeSubsystem entering_standby;
    assert(entering_standby.requestStandby());
    entering_standby.periodic();
    assert(!entering_standby.hasFailed() && entering_standby.prepares == 0);
    entering_standby.online = true;
    entering_standby.ready = true;
    entering_standby.standby_ready = false;
    entering_standby.periodic();
    assert(entering_standby.state() == State::Preparing && entering_standby.output);
    entering_standby.online = false;
    entering_standby.periodic();
    assert(entering_standby.hasFailed() && !entering_standby.output);
    std::cout << "PASS: both request modes wait initially and latch offline faults after preparation\n";

    probe.standby_ready = false;
    rtos::clock_ms = 0xFFFFFFFCU;
    probe.requestPrepare();
    probe.periodic();
    assert(probe.state() == State::Preparing && probe.output);
    const auto pending_entries = probe.standby_entries;
    const auto pending_prepares = probe.prepares;
    rtos::clock_ms += 60000;
    probe.periodic();
    assert(probe.state() == State::Preparing && probe.output);
    assert(probe.standby_entries == pending_entries && probe.prepares == pending_prepares);
    probe.standby_ready = true;
    probe.periodic();
    assert(probe.isStandby());
    rtos::clock_ms += 60000;
    probe.standby_ready = false;
    probe.periodic();
    assert(probe.state() == State::Preparing && probe.output);
    rtos::clock_ms += 60000;
    probe.periodic();
    assert(probe.state() == State::Preparing && probe.output);
    assert(probe.standby_entries == pending_entries && probe.prepares == pending_prepares);
    probe.standby_ready = true;
    probe.periodic();
    assert(probe.isStandby());
    assert(probe.requestRun());
    probe.run_success = false;
    probe.periodic();
    assert(probe.hasFailed() && !probe.output);
    std::cout << "PASS: standby pending has no timeout before or after ready, output failures still latch\n";

    C620Motor motor;
    robot::TestSubsystem subsystem(motor);
    subsystem.requestPrepare();
    const auto init_before_online = pid_init_count;
    subsystem.periodic();
    assert(subsystem.state() == State::Preparing && pid_init_count == init_before_online);
    motor.online = true;
    motor.speed = 3;
    subsystem.periodic();
    assert(subsystem.isStandby() && motor.enabled && motor.current == -3);
    const auto initialized = pid_init_count;
    assert(!subsystem.setTargetSpeed(10));
    assert(subsystem.requestRun());
    subsystem.periodic();
    assert(subsystem.setTargetSpeed(10));
    subsystem.periodic();
    assert(motor.current == 7);
    assert(!subsystem.setTargetSpeed(std::numeric_limits<float>::infinity()));
    assert(subsystem.requestStandby());
    subsystem.periodic();
    assert(subsystem.isStandby() && motor.current == -3 && pid_init_count == initialized);
    assert(subsystem.requestRun());
    subsystem.periodic();
    subsystem.requestDisable();
    assert(!subsystem.setTargetSpeed(10));
    subsystem.periodic();
    assert(subsystem.state() == State::Disabled && !motor.enabled);
    subsystem.requestPrepare();
    subsystem.periodic();
    assert(subsystem.isStandby() && pid_init_count == initialized + 1);
    invalid_output = true;
    subsystem.periodic();
    invalid_output = false;
    assert(subsystem.hasFailed() && !motor.enabled);
    assert(!subsystem.requestStandby());
    assert(critical_depth == 0);
    std::cout << "PASS: real test motor zero-speed standby, target revocation and explicit recovery\n";
}
