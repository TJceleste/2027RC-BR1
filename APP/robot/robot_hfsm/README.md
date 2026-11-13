# 启动阶段与等待大电

当前以所需电机反馈在线作为大电就绪条件，没有接入电压采样或独立的电源确认输入。小电启动后，通信任务、上位机通信和状态机继续运行，等待过程不阻塞任务。

```text
Enable
├─ Starting
│  ├─ WaitPower             等待 subsystems 中全部设备在线
│  └─ PrepareSubsystems     准备全部子系统，请求运行并等待全部 Running
└─ Manual
   └─ UniversalManual       检查运行条件后进入，执行手动业务

Test
├─ TestStarting
│  ├─ TestWaitPower         只等待 test_subsystems 中设备在线
│  └─ PrepareTestSubsystems 准备该集合，请求运行并等待全部 Running
└─ TestRunning              检查运行条件后进入，周期设置测试速度目标
```

两个等待状态进入及更新时遍历对应集合，通过 `Subsystem::isOnline()` 判断设备上线，为已在线的子系统请求 `requestStandby()`，并继续检查其余子系统，不因前面的设备离线而提前返回。待机由各自控制任务异步执行，重复请求不重新初始化；`TestSubsystem` 待机为零速度闭环。未上线的设备持续等待，不发起准备、不累计设备准备超时；已经请求待机的设备掉线时关闭输出，重新上线后重新进入待机。待机失败则进入 `Fault`。

全部设备在线后即进入准备阶段，不额外等待全部 `isStandby()`；准备流程可以继续协调具体机构的待机到位动作。在线查询本身仍无初始化副作用，在线机构的待机控制器初始化只发生在控制任务处理待机请求时。

准备阶段继续使用子系统已有的超时与失败处理。正式分支在 `PrepareSubsystems` 内完成实际启动，测试分支在 `PrepareTestSubsystems` 内完成实际启动。待机、禁用和准备请求仍由各子系统的控制任务异步执行，通信任务持续收发反馈与当前输出命令。

`PrepareSubsystems` 和 `PrepareTestSubsystems` 都分为等待 `Ready` 与等待 `Running` 两阶段。各自集合全部 `Ready` 后只提交一次 `requestRun()`，任意请求被拒绝就在本轮状态机更新中进入 `Fault`，由其统一禁用；请求接受后等待各控制任务实际发布 `Running`，等待期间不重复请求。参与启动的任意子系统失败也进入 `Fault`。

`TestRunning::entryGuard()` 只检查全部测试子系统是否 `Running`，不发起启停操作；未满足条件时取消进入、保持原状态。`enter()` 只初始化业务，不再请求运行或保存启动失败标志。实际运行请求已在前一个状态中提交，进入测试业务前子系统可能已按零目标执行闭环。

`UniversalManual::entryGuard()` 同样只检查全部正式子系统是否 `Running`；`enter()` 不再发起运行请求，也不保存请求结果，运行期间在 `update()` 检查故障。直接进入业务状态不能绕过启动条件。

`TestRunning::update()` 持续检查运行故障，并以时间戳推进 `10 → 0 → -10 → 0 rad/s`，每段保持 3 秒；`exit()` 只停止速度序列，不请求禁用，也不改变当前速度目标。`UniversalManual::exit()`、`Test::exit()` 和 `Enable::exit()` 均不请求禁用，由目的状态接管业务目标。重新启动经 `TestStarting` 再走准备和启动确认流程，进入后重置速度序列。速度测试只在此状态执行，底层子系统任务继续每 1 tick 执行闭环，不再创建独立的速度切换任务。

`subsystems` 是在 `robot.hpp` 显式维护的全部子系统注册表，`test_subsystems` 必须是它的子集。进入 `Test` 时，`Test::enter()` 为测试集合之外的全部子系统提交待机请求，撤销原模式目标；离线机构上线后由各自控制任务进入待机，不作为测试启动条件，也不为它们发起准备或运行请求。测试集合内部仍由 `TestWaitPower` 和 `PrepareTestSubsystems` 接管。正式与测试模式之间的切换应分别经过 `Starting` 和 `TestStarting`。

整机层的 `requestDisable()` 只在 `Fault::enter()` 调用，统一覆盖 `subsystems` 注册表；启动失败不再在各状态中分散回滚。故障状态进入后即提交禁用请求，实际关闭由各控制任务异步完成。需要取消启动并撤销已提交请求时，切换到 `Fault`；正常切换不依赖退出回调禁用。

多电机子系统的 `isOnline()` 应覆盖本次所需电机；纯软件子系统可返回 `true`。等待阶段无法仅凭无反馈区分未接大电与设备异常，因此不会自行报准备超时。

当前保持 `Disable::update()` 自动进入 `TestStarting` 的开发流程，`Disable` 仍是开发启动入口。测试模式位于 `Enable` 外；这里没有调整整机模式层级、增加故障恢复或遥控使能策略。
