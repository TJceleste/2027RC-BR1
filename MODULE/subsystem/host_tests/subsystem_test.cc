#include "test_subsystem.hpp"
#include "rtos_utility.hpp"

#include <cassert>
#include <functional>
#include <iostream>
#include <limits>
#include <type_traits>

class ProbeSubsystem final : public Subsystem {
public:
    ProbeSubsystem() : Subsystem(10) {}
    bool isOnline() const override { return online; }
    bool online{true};
    bool ready{false};
    bool failed{false};
    bool run_success{true};
    bool output{false};
    unsigned prepares{0};
    unsigned standby_entries{0};
    bool standby_ready{true};
    bool standby_failed{false};
    std::function<void()> during_run;

private:
    void onRequestReset() noexcept override { assert(critical_depth > 0); }
    void onPrepare() override { assert(critical_depth == 0); ++prepares; }
    Readiness onStandby(bool entering) override {
        assert(critical_depth == 0);
        if (entering) ++standby_entries;
        output = true;
        return standby_failed ? Readiness::Failed
                              : standby_ready ? Readiness::Ready : Readiness::Pending;
    }
    Readiness onCheckReadiness() override {
        assert(critical_depth == 0);
        return failed ? Readiness::Failed : ready ? Readiness::Ready : Readiness::Pending;
    }
    bool onRun(bool enabled) override {
        assert(critical_depth == 0);
        if (!enabled) {
            output = false;
            return true;
        }
        if (during_run) {
            auto hook = during_run;
            during_run = {};
            hook();
        }
        output = run_success;
        return run_success;
    }
};

int main() {
    using Status = Subsystem::StartupStatus;
    static_assert(std::is_base_of_v<Subsystem, robot::TestSubsystem>);
    ProbeSubsystem probe;
    Subsystem& base = probe;
    assert(base.startupStatus() == Status::Idle);
    assert(!base.requestRun());
    base.requestPrepare();
    base.periodic();
    assert(probe.prepares == 1);
    rtos::clock_ms = 9;
    base.periodic();
    assert(base.startupStatus() == Status::Preparing && probe.prepares == 1);
    rtos::clock_ms = 10;
    base.periodic();
    assert(base.hasFailed() && !probe.output);
    probe.ready = true;
    base.periodic();
    assert(base.hasFailed());
    base.requestPrepare();
    assert(!base.isReady());
    base.periodic();
    assert(base.isReady() && !probe.output);
    assert(base.requestRun());
    base.periodic();
    assert(base.isRunning() && !base.isReady() && probe.output);
    probe.ready = false;
    base.periodic();
    assert(base.hasFailed() && !probe.output);
    std::cout << "PASS: common lifecycle, custom timeout, health loss and failure latch\n";

    base.requestPrepare();
    probe.failed = true;
    base.periodic();
    assert(base.hasFailed());
    probe.failed = false;
    probe.ready = true;
    base.requestPrepare();
    base.periodic();
    assert(base.requestRun());
    probe.run_success = false;
    base.periodic();
    assert(base.hasFailed() && !probe.output);
    probe.run_success = true;
    base.requestPrepare();
    base.periodic();
    assert(base.requestRun());
    probe.during_run = [&] { base.requestDisable(); };
    base.periodic();
    assert(base.startupStatus() == Status::Disabling && probe.output);
    base.periodic();
    assert(base.startupStatus() == Status::Idle && !probe.output);
    base.requestPrepare();
    base.periodic();
    assert(base.requestRun());
    probe.during_run = [&] { base.requestPrepare(); };
    base.periodic();
    assert(base.startupStatus() == Status::Preparing && probe.output);
    assert(!base.requestRun());
    base.periodic();
    assert(base.isReady() && !probe.output);
    std::cout << "PASS: explicit preparation/run failures and deferred requests during computation\n";

    probe.ready = false;
    rtos::clock_ms = 0xFFFFFFFCU;
    base.requestPrepare();
    base.periodic();
    rtos::clock_ms += 9;
    base.periodic();
    assert(!base.hasFailed());
    ++rtos::clock_ms;
    base.periodic();
    assert(base.hasFailed());
    base.requestDisable();
    base.requestPrepare();
    probe.ready = true;
    base.periodic();
    assert(base.isReady());
    std::cout << "PASS: clock wrap and disable/prepare before next control cycle\n";

    ProbeSubsystem standby_probe;
    standby_probe.online = false;
    standby_probe.standby_ready = false;
    assert(standby_probe.requestStandby());
    standby_probe.periodic();
    assert(standby_probe.startupStatus() == Status::EnteringStandby);
    assert(!standby_probe.output && standby_probe.standby_entries == 0);
    standby_probe.online = true;
    standby_probe.periodic();
    assert(standby_probe.startupStatus() == Status::EnteringStandby);
    assert(standby_probe.output && standby_probe.standby_entries == 1);
    assert(!standby_probe.requestRun());
    assert(standby_probe.requestStandby());
    standby_probe.periodic();
    assert(standby_probe.standby_entries == 1);
    standby_probe.standby_ready = true;
    standby_probe.periodic();
    assert(standby_probe.isStandby());
    standby_probe.online = false;
    standby_probe.periodic();
    rtos::clock_ms += 60000;
    standby_probe.periodic();
    assert(!standby_probe.hasFailed() && !standby_probe.output);
    standby_probe.online = true;
    standby_probe.periodic();
    assert(standby_probe.isStandby() && standby_probe.standby_entries == 2);
    assert(standby_probe.prepares == 0);
    standby_probe.standby_failed = true;
    standby_probe.periodic();
    assert(standby_probe.hasFailed() && !standby_probe.output);
    standby_probe.standby_failed = false;
    assert(!standby_probe.requestStandby());
    standby_probe.periodic();
    assert(standby_probe.hasFailed() && !standby_probe.output);
    std::cout << "PASS: standby progress, idempotence, offline retry and latched failure\n";

    C620Motor motor;
    robot::TestSubsystem subsystem(motor);
    Subsystem& motor_base = subsystem;
    assert(!subsystem.setTargetSpeed(10));
    motor_base.requestPrepare();
    motor_base.periodic();
    motor.online = true;
    motor_base.periodic();
    assert(motor_base.isReady() && !motor.enabled);
    assert(motor_base.requestRun());
    motor_base.periodic();
    assert(motor_base.isRunning() && motor.enabled && motor.current == 0);
    assert(!subsystem.setTargetSpeed(std::numeric_limits<float>::infinity()));
    assert(subsystem.setTargetSpeed(10));
    motor_base.periodic();
    assert(motor.current == 10);
    during_calculate = [&] { motor_base.requestPrepare(); };
    motor_base.periodic();
    assert(motor_base.startupStatus() == Status::Preparing && motor.enabled && motor.current == 10);
    assert(!motor_base.requestRun());
    motor_base.periodic();
    assert(motor_base.isReady() && !motor.enabled && motor.current == 0);
    assert(motor_base.requestRun());
    motor_base.periodic();
    assert(motor.current == 0);
    invalid_output = true;
    motor_base.periodic();
    assert(motor_base.hasFailed() && !motor.enabled);
    invalid_output = false;
    motor_base.requestPrepare();
    motor_base.periodic();
    assert(motor_base.requestRun());
    motor_base.periodic();
    motor.online = false;
    motor_base.periodic();
    assert(motor_base.hasFailed() && !motor.enabled);
    motor.online = true;
    motor_base.periodic();
    assert(motor_base.hasFailed());
    motor_base.requestDisable();
    motor_base.periodic();
    assert(!subsystem.setTargetSpeed(10));
    assert(motor_base.requestStandby());
    motor.speed = 3;
    const auto standby_pid_inits = pid_init_count;
    motor_base.periodic();
    assert(motor_base.isStandby() && motor.enabled && motor.current == -3);
    assert(pid_init_count == standby_pid_inits + 1);
    assert(!subsystem.setTargetSpeed(10) && !motor_base.requestRun());
    assert(motor_base.requestStandby());
    motor_base.periodic();
    assert(pid_init_count == standby_pid_inits + 1 && motor.current == -3);
    motor.online = false;
    motor_base.periodic();
    assert(!motor.enabled && !motor_base.hasFailed());
    motor.online = true;
    motor_base.periodic();
    assert(motor_base.isStandby() && motor.enabled);
    assert(pid_init_count == standby_pid_inits + 2);
    during_calculate = [&] { motor_base.requestDisable(); };
    motor_base.periodic();
    assert(motor_base.startupStatus() == Status::Disabling);
    motor_base.periodic();
    assert(motor_base.startupStatus() == Status::Idle && !motor.enabled);
    assert(motor_base.requestStandby());
    invalid_output = true;
    motor_base.periodic();
    invalid_output = false;
    assert(motor_base.hasFailed() && !motor.enabled);
    assert(!motor_base.requestStandby());
    assert(critical_depth == 0);
    std::cout << "PASS: real TestSubsystem through base API, cleared targets, invalid PID and offline motor\n";
    std::cout << "PASS: real motor standby brakes to zero, resets once and honors disable/failure\n";
}
