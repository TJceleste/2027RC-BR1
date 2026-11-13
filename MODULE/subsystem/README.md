# Subsystem

`Subsystem` 管理五个状态：`Disabled / Preparing / Standby / Running / Failed`。每个子系统由自己的控制任务调用 `periodic()`，独立等待设备上线、建立运行条件并进入待机；整机只协调所需集合的待机及运行结果。

| 接口 | 含义 |
| --- | --- |
| `requestPrepare()` | 显式开始一轮准备，也用于修复后的恢复；清除旧业务目标 |
| `requestStandby()` | 返回待机，复用有效准备结果；未准备时自行等待上线并准备；重复请求不重置，Failed 时拒绝 |
| `requestRun()` | 仅在 Standby 且无其他未处理请求时接受运行，返回是否接受 |
| `requestDisable()` | 立即撤销业务写入权限，异步关闭输出并使准备结果失效 |
| `state()` | 原子读取当前状态 |
| `isStandby() / isRunning() / hasFailed()` | 对当前状态的便捷查询 |

```text
Disabled --requestPrepare/requestStandby--> Preparing --准备及待机完成--> Standby
Running  --requestStandby---------------> Preparing --返回待机完成----> Standby
Standby  --requestRun（接受后由控制任务执行）--------------------------> Running
准备/待机/运行失败 -------------------------------------------------> Failed
任意状态 --requestDisable（控制任务执行关闭）------------------------> Disabled
Failed   --requestPrepare-------------------------------------------> Preparing
```

Preparing 同时表示等待上线、实际准备和进入待机。基类不设置准备或进入待机的超时，准备步骤或待机步骤持续返回 Pending 时继续等待。已经建立运行条件后，onCheckReadiness 必须持续返回 Ready；条件失效、回调报告失败，或进入待机、保持待机及运行时掉线，都会关闭输出并保持 Failed，重新上线不会自动恢复；需要显式 requestPrepare 重新准备。判定依据是实际准备结果，不依赖请求模式是否已改为 Standby。

请求非阻塞，返回接受不代表执行完成。requestRun 接受后仍可能暂时查询到 Standby；requestDisable 发出后也可能暂时查询到 Running，直到控制任务关闭输出后才发布 Disabled。业务目标写入必须在短临界区内通过受保护的 `acceptsCommands()` 检查授权，避免把未处理的禁用请求当成仍可写目标。

## 派生类回调

| 回调 | 职责 |
| --- | --- |
| `isOnline()` | 受保护的设备反馈查询；由子系统内部使用，整机不检查在线 |
| `onRequestReset()` | 请求方短临界区内清除旧目标；不得阻塞、操作硬件或调用生命周期请求 |
| `onPrepare()` | 上线后开始实际准备时调用一次；正常返回待机不重复初始化 |
| `onCheckReadiness()` | 推进必要准备步骤、检查运行条件，返回 Pending / Ready / Failed |
| `onStandby(entering)` | 条件建立后推进进入/保持待机；首次调用 entering=true；默认关闭输出并完成待机 |
| `onRun(enabled)` | 执行业务控制；enabled=false 时必须关闭并清空输出 |

所有回调须及时返回。准备步骤可执行所需受控动作，待机步骤可持续保持输出；派生类负责反馈、控制目标及硬件输出的同步。控制任务负责初始化、计算和输出，请求方不得执行这些工作。计算期间到达新请求时，本轮仍可能产生一次旧输出；请求轮次和模式核对防止旧状态覆盖新请求状态。

`TestSubsystem` 在上电前保持 Preparing，上线后初始化一次 PID，以零速度闭环进入 Standby；正常运行后返回待机不会重复初始化 PID。此示例的 Standby 表示零速度保持策略已执行，不额外等待实测速度阈值。

## 整机集成及恢复

`robot::subsystems` 是完整注册表，`test_subsystems` 必须是它的子集。Starting / TestStarting 对所需集合发起一次准备或返回待机请求，等待全部 Standby，再提交一次运行请求并等待实际 Running。不存在整机等待全部上线后才统一初始化的屏障。

Fault 先记录来源，再禁用故障子系统并请求健康子系统待机；无明确来源的整机故障默认全部禁用。Fault 是整机唯一调用 requestDisable 的位置。恢复标志位授权后进入同级 Recovering，重新准备 Disabled / Failed 子系统，复用健康子系统的待机条件；修复对象及目标业务集合满足条件后，再请求业务集合运行。恢复失败返回 Fault，需重新授权。

测试集合之外尚未建立运行条件的子系统可以自行等待上线，不阻塞测试启动或恢复。已准备好的集合外机构掉线同样报告 Failed；测试分支当前仍只监控参与集合的故障，但整机进入 Fault 时会一并记录已经失败的机构，并在恢复时等待这些故障对象重新准备完成。自动任务的检查点和进度恢复仍属于业务层，当前恢复目标为 UniversalManual 或重新开始速度序列的 TestRunning。

## 主机验证

在项目根目录执行：

```sh
python MODULE/subsystem/host_tests/run_host_tests.py
```

可用 CXX 指定主机 C++23 编译器。测试编译实际子系统、状态树及状态实现，使用模拟反馈和时钟验证独立启动、准备及待机无超时等待、模式交接、选择性故障处置、恢复授权和恢复失败；不替代上板验证。
