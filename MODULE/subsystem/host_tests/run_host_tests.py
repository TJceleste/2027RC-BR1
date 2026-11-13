"""Compile actual subsystem/HFSM sources against host device and RTOS substitutes."""

from pathlib import Path
import os
import shutil
import subprocess


def main():
    tests = Path(__file__).resolve().parent
    root = tests.parents[2]
    out = root / "build/subsystem_host"
    stubs = out / "stubs"
    stubs.mkdir(parents=True, exist_ok=True)
    headers = {
        "rtos_utility.hpp": r"""#pragma once
#include <cassert>
#include <cstdint>
inline int critical_depth = 0;
#define taskENTER_CRITICAL() (++critical_depth)
#define taskEXIT_CRITICAL() (--critical_depth)
namespace rtos {
inline std::uint32_t clock_ms = 0;
inline std::uint32_t now_ms() { return clock_ms; }
}
""",
        "c620.hpp": r"""#pragma once
#include "rtos_utility.hpp"
class C620Motor {
public:
    struct OutputCommand { float current_ma; };
    bool online = false;
    bool enabled = false;
    float speed = 0;
    float current = 0;
    bool is_online() const { assert(critical_depth > 0); return online; }
    float get_current_speed() const { assert(critical_depth > 0); return speed; }
    bool set_output_command(OutputCommand cmd) {
        assert(critical_depth > 0); current = cmd.current_ma; return true;
    }
    void enable() { assert(critical_depth > 0); enabled = true; }
    void disable() { assert(critical_depth > 0); enabled = false; current = 0; }
};
""",
        "pid_controller.hpp": r"""#pragma once
#include "rtos_utility.hpp"
#include <bitset>
#include <functional>
#include <limits>
inline std::function<void()> during_calculate;
inline bool invalid_output = false;
inline unsigned pid_init_count = 0;
class PidController {
public:
    enum Improvement { increment_of_out };
    struct Config { float kp, ki, kd, max_out; std::bitset<1> improvement; };
    PidController(const char*, const Config&) {}
    void init() { assert(critical_depth == 0); ++pid_init_count; }
    float calculate(float measure, float target) {
        assert(critical_depth == 0);
        if (during_calculate) {
            auto hook = during_calculate;
            during_calculate = {};
            hook();
        }
        return invalid_output ? std::numeric_limits<float>::quiet_NaN() : target - measure;
    }
};
""",
    }
    for name, content in headers.items():
        (stubs / name).write_text(content, encoding="utf-8")

    # Keep the actual context and HFSM tree, substituting devices and adding a
    # second subsystem to check that states depend only on the common interface.
    robot_source = (root / "APP/robot/robot.hpp").read_text(encoding="utf-8")
    context_and_tree = robot_source[robot_source.index("struct Robot_Context"):]
    (stubs / "robot.hpp").write_text(r"""#pragma once
#include "test_subsystem.hpp"
#include "states.hpp"
#include <array>
namespace robot {
class ExtraSubsystem final : public Subsystem {
public:
    bool online{false};
    bool ready{false};
    bool output{false};
    bool run_success{true};
    unsigned prepares{0};
    bool isOnline() const override { return online; }
private:
    void onPrepare() override { ++prepares; }
    Readiness onStandby(bool entering) override {
        assert(critical_depth == 0);
        output = run_success;
        return run_success ? Readiness::Ready : Readiness::Failed;
    }
    Readiness onCheckReadiness() override {
        return ready ? Readiness::Ready : Readiness::Pending;
    }
    bool onRun(bool enabled) override {
        assert(critical_depth == 0);
        output = enabled && run_success;
        return !enabled || run_success;
    }
};
inline C620Motor test_motor;
inline TestSubsystem test_subsystem{test_motor};
inline ExtraSubsystem extra_subsystem;
inline const std::array<Subsystem*, 2> subsystems{&test_subsystem, &extra_subsystem};
#ifdef TEST_MULTIPLE_SUBSYSTEMS
inline const std::array<Subsystem*, 2> test_subsystems{&test_subsystem, &extra_subsystem};
#else
inline const std::array<Subsystem*, 1> test_subsystems{&test_subsystem};
#endif
""" + context_and_tree, encoding="utf-8")

    compiler = shutil.which(os.environ.get("CXX", "g++"))
    if not compiler:
        raise SystemExit("A host C++23 compiler is required; set CXX to its executable.")
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    common = [
        compiler, "-std=c++23", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter",
        "-I" + str(stubs), "-I" + str(root / "MODULE/subsystem"),
        "-I" + str(root / "APP/subsystems/test_subsystem"),
        "-I" + str(root / "APP/robot/robot_hfsm"), "-I" + str(root / "Hfsm"),
        str(root / "MODULE/subsystem/subsystem.cpp"),
        str(root / "APP/subsystems/test_subsystem/test_subsystem.cpp"),
    ]
    for name in ("subsystem_test", "hfsm_test", "test_startup_test"):
        exe = out / (name + (".exe" if os.name == "nt" else ""))
        sources = [str(tests / (name + ".cc"))]
        if name in ("hfsm_test", "test_startup_test"):
            sources += sorted(map(str, (root / "APP/robot/robot_hfsm").rglob("*.cpp")))
        if name == "test_startup_test":
            sources += ["-DTEST_MULTIPLE_SUBSYSTEMS"]
        subprocess.run(common + sources + ["-o", str(exe)], check=True, env=env)
        subprocess.run([str(exe)], check=True, env=env)


if __name__ == "__main__":
    main()
