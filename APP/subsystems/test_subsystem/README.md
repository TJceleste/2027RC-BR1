# TestSubsystem

`robot::test_subsystem` 只管理 `robot::test_motor`，封装等待有效反馈、PID 复位、速度闭环和输出使能。PID 名称仍为 `test_motor_speed`，可继续通过原有 `debug_task` 调参。

`TestSubsystem` 继承 [`Subsystem`](../../../MODULE/subsystem/README.md)。统一状态、待机/准备/运行/禁用请求、超时和请求轮次保护均由基类实现；本类实现设备检查，在 `onRun(enabled)` 中统一计算 PID、应用或关闭输出，并提供 `setTargetSpeed()` 业务接口。待机由 `onStandby(entering)` 实现：首次执行时初始化 PID，读取当前反馈并复用速度闭环，以零速度保持，不进行位置回零。

`isOnline()` 在短临界区内读取 `test_motor.is_online()`，供整机等待大电阶段使用。这个查询不初始化 PID、不请求准备，也不启动超时；在线只表示近期有有效反馈，准备就绪仍通过 `isReady()` 查询。

## 接口与任务分工

- 状态机任务调用 `requestStandby()`、`requestPrepare()`、`requestRun()`、`requestDisable()`，并通过 `startupStatus()` 查询结果。
- `test_subsystem` 任务每 1 tick 调用一次 `periodic()`；只有这个任务执行 PID 初始化、计算和电机输出操作。
- `PrepareTestSubsystems` 等全部测试子系统 `Ready` 后提交一次 `requestRun()`，并等全部实际 `Running` 后才切换到 `TestRunning`。
- 正式分支的 `PrepareSubsystems` 同样完成准备、请求运行及运行确认；`UniversalManual` 只检查进入条件和运行故障。
- `TestRunning::entryGuard()` 检查运行条件，`enter()` 只初始化业务；`update()` 通过 `setTargetSpeed()` 每 3 秒循环设置 `10 → 0 → -10 → 0 rad/s`。时间戳推进，不阻塞状态机；重新启动经 `TestStarting`，进入业务后从 `10 rad/s` 开始，正式手动模式不产生该测试目标。
- CAN 通信任务一直运行，禁止输出时仍发送零电流帧。

所有接口只用于任务上下文。请求快照通过短临界区同步，反馈读取时屏蔽当前优先级为 5 的 FDCAN 接收中断；PID 计算在临界区外执行，电机命令写入仍使用短临界区同步 CAN 通信任务。准备结果通过原子状态查询，其他任务不直接访问状态机内部数据。

## 状态

| 状态 | 含义 |
| --- | --- |
| `Idle` | 已禁止输出 |
| `EnteringStandby` | 待机请求已接受，等待控制任务处理或设备重新上线 |
| `Standby` | 已启动零速度闭环；表示保持策略已执行，不代表实测速度已经归零 |
| `Preparing` | 正在等待有效反馈，输出保持禁用 |
| `Ready` | 反馈有效且 PID 已复位，输出仍禁用 |
| `Running` | 接受速度目标并执行闭环 |
| `Disabling` | 已请求禁用，等待控制任务执行 |
| `Failed` | 启动超过 5 秒、就绪或运行后掉线，或 PID 输出无效；输出禁用 |

`requestPrepare()` 会立即撤销旧的就绪结果、清零速度目标并开始新一轮准备。状态发布前会核对请求轮次，防止旧状态覆盖新请求状态。计算期间收到禁用或重新准备请求时，本轮仍可能应用一次旧输出，后续周期再处理新请求并关闭输出。失败后不会自行恢复输出，需要重新准备。

`requestRun()` 仅在 `Ready` 时成功；`setTargetSpeed()` 仅在 `Running` 且参数有限时成功。待机期间拒绝业务速度目标，重复 `requestStandby()` 不清理新一轮目标或重复初始化 PID；掉线会关闭输出，重新上线后重新初始化待机闭环。待机不调用 `onPrepare()`，不计准备超时；PID 输出失败仍报告 `Failed` 并关闭输出。禁用请求异步执行，通常在下一个 1 tick 控制周期内关闭输出；之后仍需 CAN 周期发送零电流帧。

## 状态机接入

状态机遍历 `robot::subsystems` 中的 `Subsystem*`，不直接依赖 `TestSubsystem`。`Starting` 的默认子状态 `WaitPower` 为已在线子系统请求待机，并持续等待全部设备上线；遍历不会因某个子系统离线而提前结束。全部在线后进入 `PrepareSubsystems` 才发起准备；全部 `Ready` 时提交一次运行请求，等待全部实际 `Running` 后才切换至 `UniversalManual`。运行请求被拒绝时本轮进入 `Fault`，异步启动失败也在准备状态中处理；`UniversalManual::entryGuard()` 只检查全部 `Running`，`enter()` 不再请求运行。业务状态和父状态退出均不请求禁用。

`TestStarting` 走 `TestWaitPower → PrepareTestSubsystems → TestRunning`，只等待并启动 `robot::test_subsystems` 中的子系统；`TestWaitPower` 同样使集合内已在线的子系统先待机。进入 `Test` 时，集合外的子系统收到待机请求，撤销原模式目标；离线机构上线后自行待机，不阻塞测试。两个等待状态发现参与启动的子系统待机失败时进入 `Fault`。`PrepareTestSubsystems` 同时负责准备与运行确认：等待全部 `Ready`，提交一次运行请求，再等待全部 `Running`；请求拒绝时本轮进入 `Fault`，等待期间不重试请求。`TestRunning` 不再执行启动操作，进入保护未通过只会取消切换，准备状态继续负责处理启动失败。

`robot::subsystems` 是全部子系统注册表，`test_subsystems` 必须是其子集。整机层仅在 `Fault::enter()` 遍历注册表统一禁用，包括测试集合之外正在待机的子系统；启动失败也由 Fault 统一撤销已接受的请求。正式与测试模式正常切换时，经目的模式的 `Starting` 或 `TestStarting` 接管，不调用禁用。

整机初始状态保持 `Disable`，当前 `Disable::update()` 自动进入 `TestStarting`，因此小电上电后会等待测试所需电机上线。正式启动路径由状态机所属任务进入 `Starting`。等待上线不限时，设备准备超时从后续处理 `requestPrepare()` 时开始计时。
