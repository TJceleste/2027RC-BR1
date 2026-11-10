## 什么是 hfsm

hfsm (Hierarchical Finite State Machine) 是一种分层有限状态机，它允许在状态之间建立层次关系，从而实现更复杂的状态管理。

完全静态实现，状态机的所有状态在编译期就已经确定，状态机的结构在运行时不会发生变化。

## hfsm 中的回调

本实现为每个状态提供了以下回调：
- `entryGuard()`：进入状态前的检查，不满足条件就拒绝进入
- `onEnter()`：进入状态时的回调
- `onUpdate()`：状态机每次更新时的回调
- `exitGuard()`：退出状态前的检查，不满足条件就拒绝退出
- `onExit()`：退出状态时的回调

其中，用户必须实现`enter()`、`update()`和`exit()`（可以是空回调，但是必须**显式**写出来）。而`entryGuard()`和`exitGuard()`已由`State`基提供空回调，默认**不拒绝进入或退出**，可以不实现。

### 分层状态机示例：

```
--- ImplicitRoot // hfsm内部根，用户不需要关心，会被隐式配置
    |- StateA
    |   | - StateA1
    |   | - StateA2
    |- StateB
    |   | - StateB1
    |   | - StateB2
```

不同于一般的状态机实现，hfsm提供了状态树的概念，状态树的每个节点都是一个状态，状态可以是**叶子状态**（没有子状态）或者**复合状态**（有子状态）。复合状态可以包含其他状态，从而形成层次结构。

### 回调时序

本 hfsm 实现中的回调时序符合以下规律：

- 调用状态机的`update()`，时，沿着状态树从根到叶执行`entryGuard()`
- 状态只能发起转换请求(使用`control.changeTo<...>()`)，不能直接切换状态。切换请求统一在状态机更新结束时进行处理。
- 当一次更新中发起了多次请求，只处理第一个请求，其他请求会被忽略，哪怕这个请求会被拒绝
- 响应转换请求时，先沿着状态树从叶到根检查`exitGuard()`，再从根到叶检查`entryGuard()`，最后进行执行转换
- 执行转换时，沿状态树从叶到根先执行`exit()`，再从根到叶执行`enter()`

这里以上述状态机分析时序。

## 使用 hfsm 设计状态机时需要认真考虑的地方

### 如何处理带有“初始化”，“启动”语义的行为

这里有几种写法

1. 单独定义一个**初始化状态**，作为状态机的第一个状态
2. 在状态的`enter()`方法中实现初始化逻辑
3. 在**外部实现**初始化逻辑

**!!!不要尝试在entryGuard()中进行初始化!!!**

大部分的“初始化”语义都可以用**方法1**完成，其他方法可以不加以研究。而方法2适合**对某个状态自己**进行初始化。

#### 方法1
> 单独定义一个**初始化状态**，作为状态机的第一个状态

最推荐的写法是**方法1**，单独定义一个初始化状态。hfsm为状态提供了`enter()`、`entryGuard()`、`update()`、`exit()`和`exitGuard()`，功能完善。完成初始化后的状态切换在`update()`中**主动进行**，提供了切换前**检查初始化**是否完成的操作空间，也推荐切换前进行检查。

#### 方法2
> 在状态的和`enter()`方法中实现初始化逻辑

编写`enter()`方法应该多加考量，该方法有些特性需要注意：

1. 所有状态机回调的共同特征，见...
2. `enter()`执行完毕立刻进入状态的`update()`方法。
3. 每次进入其状态时，都会执行`enter()`方法，其中的操作会被重复执行。

受限于这些特性，`enter()`中的初始化应该是：
- **幂等的**，即多次执行不会产生额外副作用，执行效果完全相同。
- **快速的**，不阻塞状态机的运行，需要等待的操作应该使用方法1

通常，`enter()`是**对某个状态自己**进行初始化。如重置状态要使用的计数器，这类需要在状态每次进入时保持一致的量都应该在于`enter()`，而不是仅在状态的结构体创建时初始化一次。

#### 方法3
> 在**外部实现**初始化逻辑

这是最简单的写法，在外部确定完成初始化后再启动状态机，这样状态机的第一个状态就可以直接进入运行状态。但是这样就没有很好的利用状态机的能力了，也不能达成大部分逻辑均在状态机内部的设计期望。

#### 关于entryGuard()

`entryGuard()`只应被用于转换前检查，不能包含任何运行逻辑，包括初始化。原因是虽然`entryGuard()`提供了一个检查的机会，似乎可以在这里写“如果初始化未完成，就拒绝进入”这样的逻辑。但是，通过`entryGuard()`了不一定会进入它所属的状态。就算用户已经非常了解本hfsm实现，确保了时序不会出差错，这种写法也是非常违反`entryGuard()`的语义，十分影响可读性的。

## todo
日志开发，实际调试测试



``` c++
// 声明全局上下文，这里省略
struct Context;
// 声明状态，这里省略实现
struct StateA, StateA1, StateA2, StateB, StateB1, StateB2;
// 配置状态机
using StateList = hfsm::TypeList<StateA, StateA1, StateA2, StateB, StateB1, StateB2>;  //列出所有状态
using M = hfsm::MachineT<Context, StateList>;
using Control = M::Control;
using GuardControl = M::GuardControl;
using State = M::State;

using Tree = M::PeerRoot<
    M::Composite<StateA,
        M::Leaf<StateA1>,
        M::Leaf<StateA2>
    >,
    M::Composite<StateB,
        M::Leaf<StateB1>,
        M::Leaf<StateB2>
    >
>;

using Machine = M::Instance<Tree>;

int main() {
    Context context;
    Machine machine(context, M::initial<StateA1>()); // 启动状态机，进入StateA1
}

struct StateA : State {
    void enter() {...}
    void update() {...}
    void exit() {...}
};

struct StateA1 : State {...};

```