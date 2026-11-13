# 启动和故障恢复

```text
PeerRoot
├─ Disable                         开发入口，自动进入 TestStarting
├─ Enable
│  ├─ Starting                     独立准备、等待全部待机、请求运行并确认
│  ├─ Manual / UniversalManual      正式业务
│  └─ Auto                         保留原有自动分支
├─ Test
│  ├─ TestStarting                 仅准备并启动 test_subsystems
│  └─ TestRunning                  每 3 秒切换测试电机速度
├─ Fault                           记录故障、选择性禁用、等待显式恢复请求
└─ Recovering                      修复对象重新准备，确认运行后返回业务
```

Starting 和 TestStarting 均包含“等待待机”和“等待实际运行”两个阶段。进入时，Disabled / Failed 子系统收到 requestPrepare，其余子系统收到 requestStandby，以复用仍有效的运行条件。各控制任务独立等待上线、准备到 Standby；先完成的机构持续待机。全部待机后只提交一次 requestRun，全部实际 Running 后进入业务状态。准备、运行请求拒绝或异步运行失败都在业务开始前转入 Fault。

原 WaitPower、TestWaitPower 和两组准备子状态已移除。设备反馈在线属于子系统内部事实，等待人工接大电不计准备超时；上线开始实际准备后才计时。准备包含进入待机，允许执行所需受控动作。

UniversalManual 和 TestRunning 的 entryGuard 只确认所需集合全部 Running，enter 不再发起运行请求。TestRunning 保持原有 10 → 0 → -10 → 0 rad/s 的三秒速度序列，重新进入会从头开始。模式及父状态退出均不禁用。

subsystems 是全部子系统注册表，test_subsystems 必须是子集。Test::enter 为集合外子系统请求待机，撤销原模式目标；尚未建立运行条件的机构可等待上线并自行准备，不阻塞测试。已建立条件后，进入或保持待机期间掉线会保持 Failed，上线不会自动恢复。测试分支仍只监控参与集合，整机进入 Fault 时则会一并记录集合外已经失败的机构。正常模式切换分别经 Starting / TestStarting 接管。

## Fault 和恢复入口

Robot_Context 保存恢复目标、故障对象对应的 faults 记录，以及不会直接改变子系统状态的运行请求拒绝来源。Fault::enter 首先保存来源，再禁用故障对象；健康对象收到待机请求。不明来源的整机故障默认禁用全部。故障记录在本轮业务成功进入前保留，新增故障重入 Fault 不会让之前禁用的机构自动启动。

离开 Fault 的输入预留为：

```cpp
robot::robot_context.recovery_requested.store(true);
```

该原子标志位默认 false。Fault::update 检查显式请求及故障对象已完成禁用后，才转入 Recovering；Fault::exitGuard 同时阻止未授权的直接状态切换。每次进入 Fault 都清零标志，离开时消费标志；恢复失败后必须重新置位。设备重新在线不会自动离开 Fault。其他上下文字段仅由状态机所属任务访问，未来可从遥控、上位机或调试入口设置这个标志。

Recovering 是 Fault 的同级状态，只接受从已授权 Fault 进入。进入时对 Disabled / Failed 对象请求准备，健康对象继续待机。恢复对象和目标业务集合都到 Standby 后，只请求目标业务集合运行，等待全部 Running 后返回 UniversalManual 或 TestRunning；集合外健康离线对象不作为恢复屏障。运行拒绝或恢复失败重新进入 Fault，执行故障处置并清除恢复授权。

当前不实现自动任务检查点继续执行；正式恢复回到 UniversalManual，测试恢复重新开始速度序列。整机 Disable 仍是开发启动入口，不是故障解除或急停入口。

## 验证

主机测试覆盖独立上线、准备和待机超时、真实 Running 确认、正式与测试模式交接、故障对象隔离、健康对象不重复准备、显式恢复授权、恢复重试及未知故障全部禁用。
