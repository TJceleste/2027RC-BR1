#pragma once

#include "c620.hpp"
#include "pid_controller.hpp"
#include "subsystem.hpp"

namespace robot {

// EB: 这里声明要写的机构类 begin
// EB: 只需要把这个TestSubsystem改成你的机构类名
class TestSubsystem final : public Subsystem {
public:
    // EB: 机构类的构造函数
    explicit TestSubsystem(C620Motor& motor);

    // EB: 声明业务函数，在test_running.cpp里调用，返回值表示是否接受请求，非阻塞
    // 一般通过写入目标值，由onRun()实现具体的执行逻辑来完成业务
    // 返回值的一种写法是检查目标值是否合理，无脑返回true也不是不行
    [[nodiscard]] bool setTargetSpeed(float speed);

private:
    // EB: 以下是继承自 Subsystem 的回调函数，会被自动调用。
    // 都要在cpp中实现，实在用不到就写空实现，需要返回值的返回正常值
    // 以下提到的请求由总状态机和Periodic()自动发起。不需要研究具体发起的机制。
    // Periodic()见robot/tasks/subsystems.cpp

    // EB: 这个是检查外设是否在线的函数，这个例子里是检查c620电机是否在线
    [[nodiscard]] bool isOnline() const override;

    // EB: 这个是处理重置请求的函数，处理准备请求和待机请求时也会调用这个函数
    // 用于清除残留的业务目标，和运行状态。比如把目标速度清零
    // 不一定需要，一时半会想不到先写空实现
    void onRequestReset() noexcept override;

    // EB: 这个是处理准备请求的函数，这里写初始化，比如初始化PID控制器，注意不要阻塞
    // 准备指的是机构在所有设备都上线后，第一次工作前需要做的初始化设置。
    // 一般是进行一些赋值，比如重置计数器，初始化pid这类一下子就能完成的
    // 假如这个机构的设备掉线了，重新上线后发起重启会调用这个函数，重新初始化。
    // 可以理解为每次上电后才要做的初始化
    void onPrepare() override;

    // EB: 这个是处理待机请求的函数。这里的待机指的是机构默认的工作状态，
    // 比如处于什么姿态，维持什么速度。对于底盘，待机可以是什么都不干，也可以是维持0速
    // 对于机械臂，待机可以是维持在某个具体的位置，也可以是维持在当前位置

    // 这个函数的内容是让机构进入待机状态，并查询是否进入待机状态成功并返回查询结果
    // 已进入待机返回 Readiness::Ready，失败返回 Readiness::Failed(如果你的机构不会失败就不要随意返回Failed)
    // 正在进入待机返回 Readiness::Pending
    // 典型的写法是{业务api1(); if(业务api1完成) return Readiness::Ready;
    //                          else return Readiness::Pending;}
    // 不需要专门的待机逻辑就删除这行使用默认的实现，默认实现是什么不用关心。
    // 但最好还是写一个简单的待机逻辑，避免出现不必要的错误。
    [[nodiscard]] Readiness onStandby(bool entering) override;

    // EB: 这个是检查机构是否准备好运行的函数，用于检查onPrepare()是否成功完成
    // 按照约定设计的onPrepare()函数应该是不会失败的，失败会进入错误处理，迟点介绍错误处理
    // 但是如果你的设计有可能失败，就需要在这里检查是否准备好运行
    // 准备好运行返回 Readiness::Ready，失败返回 Readiness::Failed
    // 不需要检查就直接返回 Readiness::Ready
    [[nodiscard]] Readiness onCheckReadiness() override;

    // EB: 这个是处理运行请求的函数
    // enabled == true表示进入运行状态，运行主要的控制逻辑
    // enabled == false表示退出运行状态，应该失能所有动力设备
    // 例如设置电机为0电流或者使用电机的失能函数
    // onRun()会在一个专门的任务周期运行的Periodic()中运行，
    // 是主要的控制函数。Periodic()见robot/tasks/subsystems.cpp
    [[nodiscard]] bool onRun(bool enabled) override;

    // EB: end

    // EB: 这里是机构类的成员变量
    C620Motor& motor_;
    PidController speed_pid_;
    float target_speed_{0};
    float measured_speed_{0};
    // EB: end
};

} // namespace robot
