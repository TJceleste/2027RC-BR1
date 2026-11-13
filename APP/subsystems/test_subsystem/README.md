# TestSubsystem

TestSubsystem 只管理 robot::test_motor，以 PID test_motor_speed 执行速度闭环，继续支持 debug_task 调参。继承 Subsystem 的五状态生命周期，整机通过基类请求和状态查询协调，不直接操作电机。

| 状态 | 电机行为 |
| --- | --- |
| Disabled | 已关闭输出 |
| Preparing | 等待有效反馈、初始化控制器并进入零速度待机 |
| Standby | 零速度闭环保持，拒绝业务速度目标 |
| Running | 接受有限速度目标，执行速度闭环 |
| Failed | 保持故障，拒绝待机请求；显式重新准备后才恢复 |

isOnline 是内部反馈查询，在短临界区内读取电机在线信息。首次上线后 onPrepare 初始化 PID；onCheckReadiness 读取在线及速度反馈；onStandby 复用速度控制，以零目标保持。正常返回待机复用 PID；进入或保持待机期间，已经建立运行条件后掉线会关闭输出并保持 Failed，上线本身不能恢复。显式重新准备后才再次初始化 PID。Standby 表示保持策略已执行，不承诺实测速度已经归零。

setTargetSpeed 在短临界区内通过 acceptsCommands 同时检查实际 Running 和当前请求授权。禁用或待机请求发出后立即拒绝旧业务目标，实际输出由每 1 tick 调用的 periodic 异步处理。PID 计算在临界区外，反馈和 CAN 命令写入由派生类同步。

启动时先在线的机构独立准备到 Standby，其他机构继续等待；正式或测试启动状态等对应集合全部 Standby 后提交运行请求，再等待实际 Running。TestRunning 执行每三秒 10 → 0 → -10 → 0 rad/s 的速度测试，退出只结束序列，目的状态负责接管目标。

发生故障时，Fault 保存来源并禁用故障对象，健康对象转入待机。显式置位 robot_context.recovery_requested 后才能进入 Recovering；修复对象重新准备到待机，再请求目标集合运行。健康对象无需重复初始化，恢复失败则回到 Fault 等待新的授权。测试集合外尚未建立运行条件的机构可以等待上线，不阻塞测试恢复；已经因待机掉线而失败、并被整机记录的机构，需要一并修复。
