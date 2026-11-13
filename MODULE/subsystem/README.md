# Subsystem

`Subsystem` 是所有业务子系统共用的生命周期基类。状态机通过 `Subsystem*` 统一发出待机、准备、运行和禁用请求，控制任务通过 `periodic()` 推进各自子系统；设备与 PID 细节由派生类实现。

## 公共接口

| 接口 | 含义 |
| --- | --- |
| `requestStandby()` | 请求独立待机并清除旧业务目标；重复请求不重启待机，`Failed` 时拒绝，返回是否接受 |
| `requestPrepare()` | 开始新一轮准备，立即清除旧就绪结果和业务目标 |
| `requestRun()` | 仅在 `Ready` 时接受运行请求，返回是否接受 |
| `requestDisable()` | 撤销当前请求、清空业务目标，异步关闭输出；再次运行需重新准备 |
| `startupStatus()` | 原子读取当前生命周期状态 |
| `isStandby()` / `isReady()` / `isRunning()` / `hasFailed()` | 分别判断是否处于 `Standby` / `Ready` / `Running` / `Failed` |
| `isOnline()` | 查询所需设备的反馈是否在线，不发起准备、不启动超时；由派生类实现同步读取 |
| `periodic()` | 由一个固定控制任务周期调用，推进准备和运行，提交本轮输出 |

```text
Idle --requestPrepare--> Preparing --就绪--> Ready --requestRun--> Running
                              |               |                      |
                           超时/失败        失去就绪条件             运行失败
                              +---------------+----------------------+--> Failed

任意状态 --requestDisable--> Disabling --periodic--> Idle
Failed --requestPrepare--> Preparing

非 Failed 状态 --requestStandby--> EnteringStandby --待机完成--> Standby
                                      ^                         |
                                      +-------- 离线 -----------+
```

`Ready` 表示可开始运行，实际输出仍禁用。准备超时默认 5000 ms，派生类可通过 `Subsystem(timeout_ms)` 调整。超时从控制任务开始处理该轮准备时计时；准备完成与超时在同一轮发生时，优先接受就绪。失败不会自动恢复，需要重新准备。

禁用不是暂停，不提供直接恢复运行的路径。`requestDisable()` 保留原有的 `onRequestReset()` 调用，默认回调不处理；派生类可清空业务目标。控制任务处理禁用后进入 `Idle`，再次运行必须经过 `requestPrepare()` 和 `requestRun()`。

待机独立于准备，可在其他子系统尚未上线时执行，不计准备超时。待机前仍检查本子系统的 `isOnline()`；离线时关闭输出并等待，上线后重新开始待机。待机回调失败会保持 `Failed`，不能用重复待机请求清除故障。`Standby` 表示已达到派生类定义的待机条件，不表示 `Ready` 或 `Running`；正式运行仍需重新准备。

## 派生类实现

| 回调 | 所在线程与同步 | 职责 |
| --- | --- | --- |
| `onRequestReset()` | 请求方，短临界区内 | 清空业务目标；默认不处理 |
| `onPrepare()` | 控制任务，临界区外 | 每轮准备调用一次，复位本地控制器；默认不处理 |
| `onStandby(entering)` | 控制任务，临界区外，每个在线周期调用 | 执行待机策略，返回 `Pending` / `Ready` / `Failed`；首次执行 `entering` 为 true，可初始化待机控制器；默认关闭输出并报告待机 |
| `onCheckReadiness()` | 控制任务，临界区外 | 返回 `Pending` / `Ready` / `Failed`；必要时推进非阻塞准备步骤 |
| `onRun(enabled)` | 控制任务，临界区外，每周期调用 | 允许时计算并应用输出，否则关闭并清空输出；运行失败时自行关闭输出并返回 `false` |

`onCheckReadiness()` 在准备、就绪和运行期间调用。准备期间 `Pending` 表示继续等待，`Failed` 可立即终止；就绪或运行期间必须保持 `Ready`，否则报告失败。

所有回调必须及时返回。运行计算和输出放在 `onRun(enabled)`；待机输出由 `onStandby(entering)` 管理，可复用运行输出逻辑。局部设备保护由派生类维护；临界区回调不得阻塞、操作耗时设备或调用请求接口。准备与就绪检查期间保持输出禁用，不能在这些回调里直接使能输出。主动待机必须独立初始化所需控制器，不能依赖先执行 `onPrepare()`。派生类自行同步设备反馈、业务目标等共享数据。对象生命周期必须覆盖所有访问任务，不支持运行期间销毁。

请求轮次与模式由基类统一管理。计算期间到达禁用或重新准备请求时，本轮仍可能应用旧请求的输出，后续周期读取新请求后关闭输出；基类只在状态发布前核对请求，避免旧状态覆盖新请求发布的状态。生命周期接口为非虚函数，派生类只覆盖回调，不重写公共流程。

## 整机接入

在 `robot.hpp` 的 `robot::subsystems` 注册表中加入派生类对象指针后，`Starting` 首先进入 `WaitPower`，遍历全部子系统，为已在线的子系统请求待机，继续等待其他子系统上线。按当前约定，等待大电即等待所需电机反馈上线；这一阶段没有超时，也不调用 `requestPrepare()`，待机失败则进入 `Fault`。全部在线后进入 `PrepareSubsystems`，发起准备并检查全部就绪，再提交一次运行请求并等待全部实际 `Running`。`UniversalManual` 的入口保护确认运行条件，进入与退出均不发起生命周期请求。

`TestStarting` 只等待并启动 `robot::test_subsystems`，该集合必须是 `robot::subsystems` 的子集。对应路径为 `TestWaitPower → PrepareTestSubsystems → TestRunning`：`TestWaitPower` 同样为已在线的子系统请求待机；全部上线后，在 `PrepareTestSubsystems` 中完成准备、提交一次运行请求并等待全部实际运行，再由入口保护确认 `Running` 后进入测试业务。进入 `Test` 时，集合外的子系统统一收到待机请求，防止继续执行原模式目标；它们无需在线，也不参与测试准备和运行确认，离线后上线会自行待机。两个等待状态以各自集合全部在线为切换条件，不等待全部 `isStandby()`，进一步的待机到位协调应由整机准备流程负责。

`Fault::enter()` 是整机层唯一提交禁用请求的位置，统一禁用 `robot::subsystems` 中的全部子系统。两个准备状态请求运行失败时，本轮就进入 `Fault` 撤销已接受的请求，异步启动失败同样在进入业务前转入故障。普通模式及父状态的退出回调不再禁用，正常模式切换经目的模式的启动流程接管。

`isOnline()` 与 `isReady()` 分别表达设备反馈可用和运行前准备完成。多设备子系统应检查全部必需设备；不依赖外设的子系统可返回 `true`。在线查询必须及时返回、不改变生命周期，必要的反馈接收或探测应由底层通信任务持续执行，不能依赖 `prepare` 才开始接收。

各子系统仍由自己的控制任务按所需频率调用 `periodic()`；列表不负责创建任务或决定调度频率。只有进入准备阶段后，子系统才开始自身准备超时。等待阶段始终无反馈会持续等待；开始准备后发生失败或超时，仍按原有逻辑进入 `Fault`。

`TestSubsystem` 是单电机示例：待机时初始化速度控制器并以零速度闭环保持，重复待机请求不重复初始化。业务接口为 `setTargetSpeed()`，只在 `Running` 接受目标。

## 主机测试

安装 Python 3 和主机 C++23 编译器后，在项目根目录执行：

```sh
python MODULE/subsystem/host_tests/run_host_tests.py
```

可用 `CXX` 环境变量指定编译器可执行文件。测试使用模拟时间、设备与 PID，直接编译实际基类、测试子系统及 HFSM 状态实现，检查生命周期、新请求的延后执行与状态保护，以及多子系统协调；不替代上板验证。
