#pragma once

#include <cstddef>
#include <type_traits>
#include <cstdint>
#include <cassert>

// 声明必需回调，函数体可放在 .cpp 中；用于需要额外成员或 guard 的状态。
// MachineType 应为 MachineT 的类型别名，调用后需加分号。
#define HFSM_STATE_CALLBACKS_WITH_BASE(MachineType) \
    void enter(); \
    void update(typename MachineType::Control&); \
    void exit()

namespace hfsm {

using StateId = std::size_t;

// 存储状态机的类型列表
template <typename... Ts>
struct TypeList { static constexpr std::size_t SIZE = sizeof...(Ts); };

// 获取状态的编号
template <typename T, typename TList>
struct IndexOf;

// IndexOf的特化，当状态为状态列表的第一个时，匹配这个特化，返回0
template <typename T, typename... TRest>
struct IndexOf<T, TypeList<T, TRest...>> {
    static constexpr StateId value = 0;
};

// IndexOf的特化，当状态不为状态列表的第一个时，匹配这个特化，使用递归找到状态的编号
template <typename T, typename TFirst, typename... TRest>
struct IndexOf<T, TypeList<TFirst, TRest...>> {
    static constexpr StateId value = 1 + IndexOf<T, TypeList<TRest...>>::value;
};

// IndexOf的特化，当状态不存在于列表中时，匹配这个特化
template <typename T>
struct IndexOf<T, TypeList<>> {
    // 依赖 T 的恒假表达式：只有真正查找未登记类型时才报错。
    static_assert(!std::is_same<T, T>::value, "出现了状态列表(TypeList)中没有的状态");
    static constexpr StateId value = 0;
};

// Contains是用于验证某个状态是否存在于某个列表中的工具
template <typename T, typename TList>
struct Contains;
template <typename T>
struct Contains<T, TypeList<>> : std::false_type {};
template <typename T, typename... TRest>
struct Contains<T, TypeList<T, TRest...>> : std::true_type {};
template <typename T, typename TFirst, typename... TRest>
struct Contains<T, TypeList<TFirst, TRest...>> : Contains<T, TypeList<TRest...>> {};

// Unique是用于验证某个列表中各成员是否唯一的工具
template <typename TList>
struct Unique;
template <>
struct Unique<TypeList<>> : std::true_type {};
template <typename TFirst, typename... TRest>
struct Unique<TypeList<TFirst, TRest...>> : std::integral_constant<bool,
    !Contains<TFirst, TypeList<TRest...>>::value && Unique<TypeList<TRest...>>::value> {};

// MachineT用于配置状态机
template <typename TContext, typename TStateList>
struct MachineT {
    using Context = TContext;
    using StateList = TStateList;
    static constexpr std::size_t STATE_COUNT = StateList::SIZE;
    static constexpr StateId INVALID_STATE = STATE_COUNT; // 代表无效状态或者隐式根状态
    static constexpr StateId DEFAULT_ENTRY = STATE_COUNT; // 代表切换时未指定非头状态的Leaf，使用Composite的默认进入

    static_assert(STATE_COUNT > 0, "状态列表不能为空");
    static_assert(Unique<StateList>::value, "状态列表不能重复");

    template <typename T>
    static constexpr StateId stateId() { return IndexOf<T, StateList>::value; }

    template <typename TTree>
    class Instance;
    // 类型化初始入口；数值入口仍保留，但必须经过运行时检查。
    // 检查初始状态是否在状态列表中
    template <typename T> struct Initial {};
    template <typename T>
    static Initial<T> initial() {
        static_assert(Contains<T, StateList>::value, "初始状态未注册");
        return {}; // 返回一个空的Initial<T>对象
    }

    // 状态转换可能的结果
    enum class TransitionResult {
        NoRequest,              // 没有请求
        Changed,                // 完成转换/状态已转换
        Unchanged,              // 不需要转换/状态未转换
        RejectedByExitGuard,    // 被某个退出保护拒绝
        RejectedByEntryGuard,   // 被某个进入保护拒绝
        InvalidTarget           // 转换目标无效
    };
    // 状态转换的三个种类
    enum class TransitionType { Change, Restart, Resume };

    // 切换状态请求，目标只有一个槽位
    struct Request {
        bool pending = false;   // 是否即将转换
        StateId target = 0;
        TransitionType type = TransitionType::Change;
    };
    
    // 状态机的状态
    struct Core {
        explicit Core(Context& context_) : context(context_) {}
        Context& context;   // 全局上下文
        bool active[STATE_COUNT] = {};  // 活跃的状态
        Request request;
    };

    // 提供发起状态转换请求的操作和全局上下文
    class Control {
    public:
        Context& context() { return core_.context; }

        template <typename T>
        bool isActive() const { return core_.active[stateId<T>()]; }

        template <typename T> void changeTo() { request<T>(TransitionType::Change); }
        template <typename T> void restart()  { request<T>(TransitionType::Restart); }
        template <typename T> void resume()   { request<T>(TransitionType::Resume); }

    private:
    // 只登记请求，不进行转换，转换统一在更新结束时进行
    template <typename T>
        void request(const TransitionType type) {
            if(!core_.request.pending) {    
                core_.request.target = stateId<T>();
                core_.request.type = type;
                core_.request.pending = true;
            }
        }

        template <typename> friend class Instance;
        explicit Control(Core& core) : core_(core) {}
        Core& core_;
    };

    // 为状态切换的保护接口提供上下文，提供取消转换的接口
    class GuardControl {
    public:
        Context& context() { return core_.context; }

        template <typename T>
        bool isActive() const { return core_.active[stateId<T>()]; }
        // 在guard中调用即可取消当前转换请求
        void cancelPendingTransitions() { cancelled_ = true; }
        bool isCancelled() const { return cancelled_; }

    private:
        template <typename> friend class Instance;
        explicit GuardControl(Core& core) : core_(core) {}
        Core& core_;
        bool cancelled_ = false;
    };

    // 为状态切换的保护提供空回调
    struct State {
        void entryGuard(GuardControl&) {}
        void exitGuard(GuardControl&) {}
    };

    // 为 visit 调用各种类型的回调的操作对象而不是虚函数。主要供Composite使用，它的子状态可以是Leaf，也可以是Composite
    // 进入这个状态本身
    struct Enter {
        StateId target;
        TransitionType type;
        Core& core;
        template <typename T> void operator()(T& node) const { node.deepEnter(target, type, core); }
    };
    // 退出这个状态
    struct Exit {
        Core& core;
        template <typename T> void operator()(T& node) const { node.deepExit(core); }
    };
    // 更新这个状态
    struct Update {
        Control& control;
        template <typename T> void operator()(T& node) const { node.deepUpdate(control); }
    };
    // 进入保护
    struct EntryGuard {
        StateId target;
        TransitionType type;
        GuardControl& control;
        template <typename T> void operator()(T& node) const { node.deepEntryGuard(target, type, control); }
    };
    // 退出保护
    struct ExitGuard {
        GuardControl& control;
        template <typename T> void operator()(T& node) const { node.deepExitGuard(control); }
    };
    // 调用其他状态的保护接口，进入或退出
    struct ForwardGuard {
        StateId target;
        TransitionType type;
        GuardControl& control;
        bool entering;
        template <typename T> void operator()(T& node) const {
            if (entering)
                node.forwardEntryGuard(target, type, control);
            else
                node.forwardExitGuard(target, type, control);
        }
    };
    // 切换到子状态
    struct ChangeToRequested {
        StateId target;
        TransitionType type;
        Core& core;
        bool& changed;
        template <typename T> void operator()(T& node) const { changed = node.changeToRequested(target, type, core); }
    };

private:    

    // 隐式根状态。状态机对外的最外层状态都是它的子状态
    struct ImplicitRoot : State {
        void enter() {}
        void update(Control&) {}
        void exit() {}
    };

public:

    // 普通状态查清单，隐式根状态使用无效编号。
    struct InvalidStateInfo { static constexpr StateId value = INVALID_STATE; };

    // 获得包括隐式根状态在内的状态信息(编号)
    template <typename T>
    struct StateInfo {
        using Selected = typename std::conditional<
            std::is_same<T, ImplicitRoot>::value,
            InvalidStateInfo,
            IndexOf<T, StateList>
        >::type;
        static constexpr StateId ID = Selected::value;
    };

    //没有子状态的状态
    template <typename TState>
    struct Leaf {
        using Owner = MachineT;
        static constexpr bool IS_ROOT = false; // 根必须是Composite，不能是Leaf。
        static constexpr bool HAS_IMPLICIT_ROOT = std::is_same<TState, ImplicitRoot>::value;
        static constexpr bool ALL_REGISTERED = HAS_IMPLICIT_ROOT || Contains<TState, StateList>::value;
        static constexpr std::size_t NODE_COUNT = HAS_IMPLICIT_ROOT ? 0 : 1;
        
        //统计子状态中 T 的数量
        template <typename T>
        static constexpr std::size_t count() {
            return !HAS_IMPLICIT_ROOT && std::is_same<T, TState>::value ? 1 : 0;
        }
        
        TState state; // 拥有state

        //Leaf没有子状态，一些接口主要是为了模板递归的统一
        // 判断状态是否包含目标状态。
        bool contains(const StateId target) const {
            return StateInfo<TState>::ID != INVALID_STATE && target == StateInfo<TState>::ID;
        }
        // 递归进入，Leaf没有子状态，因而StateId参数没有意义
        void deepEnter(StateId, TransitionType, Core& core) {
            // 隐式根状态没有编号，不需要登记活跃状态，正常使用hfsm也不会调用隐式根状态的deepEnter，兴许可以忽略这里
            if (StateInfo<TState>::ID != INVALID_STATE)
                core.active[StateInfo<TState>::ID] = true;
            state.enter();
        }
        // 递归退出
        void deepExit(Core& core) {
            // 设计预期就是exit期间state依旧活跃。按照规定使用hfsm不会重入deepExit，但是不要在exit()里操作状态机，可能会出现递归退出等异常行为
            state.exit();
            // 隐式根状态没有编号，不需要登记活跃状态。但隐式根的deepExit在析构时会被调用，不能忽略这里
            if (StateInfo<TState>::ID != INVALID_STATE)
                core.active[StateInfo<TState>::ID] = false;
        }
        // 递归更新
        void deepUpdate(Control& control) { state.update(control); }
        // 递归进入保护
        void deepEntryGuard(StateId, TransitionType, GuardControl& control) { state.entryGuard(control); }
        // 递归退出保护
        void deepExitGuard(GuardControl& control) { state.exitGuard(control); }

        // 递归寻找切换的地方，执行退出保护，Leaf是递归的终点
        void forwardExitGuard(StateId, const TransitionType type, GuardControl& control) {
            // Leaf只响应Restart，Change/Resume不做事
            if (type == TransitionType::Restart)
                deepExitGuard(control);
        }
        // 递归寻找切换的地方，执行进入保护，Leaf是递归的终点
        void forwardEntryGuard(const StateId target, const TransitionType type, GuardControl& control) {
            // Leaf只响应Restart，Change/Resume不做事
            if (type == TransitionType::Restart)
                deepEntryGuard(target, type, control);
        }

        // 切换子状态，Leaf只响应Restart，Change/Resume不做事
        bool changeToRequested(const StateId target, const TransitionType type, Core& core) {
            if (type == TransitionType::Restart) {
                deepExit(core);
                deepEnter(target, type, core);
                return true;
            }
            return false;
        }
    };
    
    using Branch = std::size_t; // composite的直接子状态的编号
    static constexpr Branch INVALID_BRANCH = static_cast<Branch>(-1);
    static constexpr Branch NO_ACTIVE_BRANCH = INVALID_BRANCH; // 代表没有活跃的子状态

    // Children 是 Composite 内部的容器，只负责拥有子状态，提供子状态相关的操作，管理交由 Composite 进行
    template <Branch N, typename... TNodes>
    struct Children;

    // Children的特化，
    template <Branch N, typename TFirst, typename... TRest>
    struct Children<N, TFirst, TRest...> {
        // 递归检查传入的状态
        // 检查是否属于同一个状态机
        static constexpr bool SAME_OWNER = std::is_same<typename TFirst::Owner, MachineT>::value &&
            Children<N + 1, TRest...>::SAME_OWNER;
        // 检查是否含有隐式根状态
        static constexpr bool HAS_IMPLICIT_ROOT = TFirst::HAS_IMPLICIT_ROOT ||
            Children<N + 1, TRest...>::HAS_IMPLICIT_ROOT;
        // 检查是否加入状态列表
        static constexpr bool ALL_REGISTERED = TFirst::ALL_REGISTERED &&
            Children<N + 1, TRest...>::ALL_REGISTERED;
        // 统计总数量
        static constexpr std::size_t NODE_COUNT = TFirst::NODE_COUNT + Children<N + 1, TRest...>::NODE_COUNT;
        
        // 统计特定状态 T 的数量
        template <typename T>
        static constexpr std::size_t count() {
            return TFirst::template count<T>() + Children<N + 1, TRest...>::template count<T>();
        }

        TFirst first;   //拥有这个子状态，Leaf或者Composite，不是State
        Children<N + 1, TRest...> rest; // 递归数据结构

        // 递归查找子状态的编号，返回分支编号
        Branch branchFor(const StateId target) const {
            return first.contains(target) ? N : rest.branchFor(target);
        }

        // 提供某个Branch的操作接口，action是操作结构体
        template <typename TAction>
        void visit(const Branch branch, const TAction& action) {
            if (branch == N)
                action(first);
            else
                rest.visit(branch, action);
        }
    };

    // 用于截止递归
    template <Branch N>
    struct Children<N> {
        static constexpr bool SAME_OWNER = true;
        static constexpr bool HAS_IMPLICIT_ROOT = false;
        static constexpr bool ALL_REGISTERED = true;
        static constexpr std::size_t NODE_COUNT = 0;
        template <typename T> static constexpr std::size_t count() { return 0; }
        Branch branchFor(StateId) const { return INVALID_BRANCH; }
        template <typename TAction>
        void visit(Branch, const TAction&) { assert(false && "操作了无效的分支"); }
    };

    // 层次状态机的核心
    template <typename THead, typename... TChildren>
    class Composite {
        static_assert(sizeof...(TChildren) > 0, "Composite不能没有子状态");

    public:
        using Owner = MachineT;
        // 检查
        static constexpr bool IS_ROOT = std::is_same<THead, ImplicitRoot>::value;
        static constexpr bool HAS_IMPLICIT_ROOT = IS_ROOT || Children<0, TChildren...>::HAS_IMPLICIT_ROOT;
        static constexpr bool ALL_REGISTERED = Leaf<THead>::ALL_REGISTERED &&
            Children<0, TChildren...>::ALL_REGISTERED;
        static constexpr std::size_t NODE_COUNT = Leaf<THead>::NODE_COUNT + Children<0, TChildren...>::NODE_COUNT;
        static_assert(Children<0, TChildren...>::SAME_OWNER, "状态树中不能存在不同状态机的状态");
        // 正常使用不会触发这个断言
        static_assert(!Children<0, TChildren...>::HAS_IMPLICIT_ROOT, "状态树中不能存在隐式根状态");
        
        // 统计特定状态 T 的数量
        template <typename T>
        static constexpr std::size_t count() {
            return Leaf<THead>::template count<T>() + Children<0, TChildren...>::template count<T>();
        }

        // 检查Composite是否在运行
        bool isRunning() const { return active_ != NO_ACTIVE_BRANCH; }
        
        // 判断target是否属于这个Composite，可以是头状态也可以是子状态
        bool contains(const StateId target) const {
            return head_.contains(target) || children_.branchFor(target) != INVALID_BRANCH;
        }

        // 递归进入
        void deepEnter(const StateId target, const TransitionType type, Core& core) {
            // 先进头状态
            head_.deepEnter(DEFAULT_ENTRY, type, core);
            // 查找并记录活跃分支，切换模式会影响这里查找的结果
            active_ = requestedBranch(target, type);
            // 递归进入 target。如果是头状态，按照切换模式进入
            children_.visit(active_, Enter{childTarget(target), type, core});
        }

        // 递归退出
        void deepExit(Core& core) {
            // 如果没有活跃分支，说明这个Composite没有进入过，直接返回
            if (active_ == NO_ACTIVE_BRANCH)
                return;
            // 先向下退出
            children_.visit(active_, Exit{core});
            head_.deepExit(core);
            // 记录应该被恢复的状态
            resumable_ = active_;
            active_ = INVALID_BRANCH;
        }

        // 递归更新
        void deepUpdate(Control& control) {
            // 如果没有活跃分支，说明这个Composite没有进入过，直接返回
            if (active_ == NO_ACTIVE_BRANCH)
                return;
            head_.deepUpdate(control);
            children_.visit(active_, Update{control});
        }

        // 递归进入保护，未通过保护会通过GuardControl取消转换
        void deepEntryGuard(const StateId target, const TransitionType type, GuardControl& control) {
            // 先执行头状态的进入保护
            head_.deepEntryGuard(INVALID_STATE, type, control);
            // 递归进入子状态的进入保护
            if (!control.isCancelled())
                children_.visit(requestedBranch(target, type), EntryGuard{childTarget(target), type, control});
        }

        // 递归退出保护，未通过保护会通过GuardControl取消转换
        void deepExitGuard(GuardControl& control) {
            if (active_ == NO_ACTIVE_BRANCH)
                return;
            // 先向下查找，查找完递归执行头状态的退出保护，实现从叶到根的检查顺序
            children_.visit(active_, ExitGuard{control});
            if (!control.isCancelled())
                head_.deepExitGuard(control);
        }

        // 递归寻找切换的地方，执行退出保护
        void forwardExitGuard(const StateId target, const TransitionType type, GuardControl& control) {
            if (active_ == NO_ACTIVE_BRANCH)
                return;
            if (type == TransitionType::Restart && head_.contains(target)) {
                deepExitGuard(control);
            } else if (active_ == requestedBranch(target, type)) {
                // false 是退出检查
                children_.visit(active_, ForwardGuard{childTarget(target), type, control, false});
            } 
            // 本Composite需要切换到新的子状态，判断退出保护，截止递归
            else {
                children_.visit(active_, ExitGuard{control});
            }
        }

        // 递归寻找切换的地方，执行进入检查
        void forwardEntryGuard(const StateId target, const TransitionType type, GuardControl& control) {
            if (type == TransitionType::Restart && head_.contains(target)) {
                deepEntryGuard(target, type, control);
                return;
            }
            const Branch requested = requestedBranch(target, type);
            // 本Composite不用切换，向下查找切换的地方
            if (active_ == requested) {
                // true 是进入检查
                children_.visit(active_, ForwardGuard{childTarget(target), type, control, true});
            }
            // 本Composite需要切换到新的子状态，判断进入保护，截止递归
            else
                children_.visit(requested, EntryGuard{childTarget(target), type, control});
        }

        // 切换子状态，返回是否切换成功
        bool changeToRequested(const StateId target, const TransitionType type, Core& core) {
            // 重启整个Composite
            if (type == TransitionType::Restart && head_.contains(target)) {
                deepExit(core);
                deepEnter(target, type, core); // 重新进入头状态，孩子从默认入口开始。
                return true;
            }
            const Branch requested = requestedBranch(target, type);
            
            // 本Composite不用切换，向下查找切换的地方
            if (active_ == requested) {
                bool changed = false;
                children_.visit(active_, ChangeToRequested{childTarget(target), type, core, changed});
                return changed;
            } else {
            // 本Composite需要切换到新的子状态
                // 先退出当前活跃的子状态
                if (active_ != NO_ACTIVE_BRANCH) {
                    children_.visit(active_, Exit{core});
                    // 记录切换前的活跃分支，以便在Resume时恢复
                    resumable_ = active_;
                }
                // 切换到新的子状态
                active_ = requested;
                children_.visit(active_, Enter{childTarget(target), type, core});
                return true;
            }
        }

    private:
        // 查询 target 的Branch，如果没有指定 target 或者 target 是头状态，就基于 type 来决定返回哪个分支
        Branch requestedBranch(const StateId target, const TransitionType type) const {
            if (target == DEFAULT_ENTRY || head_.contains(target)) {
                if (type == TransitionType::Resume) {
                    if (active_ != NO_ACTIVE_BRANCH)
                        return active_; // 本轮约定：恢复已活跃区域时保留当前分支。
                    if (resumable_ != INVALID_BRANCH)
                        return resumable_;
                }
                return 0; // Change/Restart以及没有历史的Resume，都选择默认第一个孩子。
            }
            const Branch branch = children_.branchFor(target);
            // assert(branch != INVALID_BRANCH);
            return branch;
        }

        // 判断 target 是不是头状态
        StateId childTarget(const StateId target) const {
            return head_.contains(target) ? DEFAULT_ENTRY : target;
        }

        Leaf<THead> head_;  // 头状态
        Children<0, TChildren...> children_; // 子状态的容器
        Branch active_ = NO_ACTIVE_BRANCH; // 当前活跃子状态
        Branch resumable_ = INVALID_BRANCH; // 应该被恢复的子状态
    };

    // 对外隐藏隐式根状态。
    template <typename... TChildren>
    using PeerRoot = Composite<ImplicitRoot, TChildren...>;

    // 对手写清单中的每种类型，核对它在树里恰好出现一次。
    template <typename TTree, typename TList>
    struct ListedStates;

    // 用于截止递归
    template <typename TTree>
    struct ListedStates<TTree, TypeList<>> {
        static constexpr bool ALL_PRESENT = true;
        static constexpr bool AT_MOST_ONCE = true;
    };

    // 递归检查状态是否在树中出现过，检查状态是否重复
    template <typename TTree, typename TFirst, typename... TRest>
    struct ListedStates<TTree, TypeList<TFirst, TRest...>> {
        static constexpr bool ALL_PRESENT = TTree::template count<TFirst>() > 0 &&
            ListedStates<TTree, TypeList<TRest...>>::ALL_PRESENT;
        static constexpr bool AT_MOST_ONCE = TTree::template count<TFirst>() <= 1 &&
            ListedStates<TTree, TypeList<TRest...>>::AT_MOST_ONCE;
    };

    // 务必保证Context在整个状态机生命周期内有效
    template <typename TTree>
    class Instance {
        static_assert(std::is_same<typename TTree::Owner, MachineT>::value,
                      "状态机和状态树不匹配");
        // 正常使用不触发这个断言
        static_assert(TTree::IS_ROOT, "状态树必须有隐式根状态 请使用 PeerRoot 配置状态树");

        static_assert(!Contains<ImplicitRoot, StateList>::value, "状态树中不能存在隐式根状态");
        static_assert(TTree::ALL_REGISTERED, "状态树中不能存在未注册进状态列表的状态");
        static_assert(ListedStates<TTree, StateList>::ALL_PRESENT, "状态列表中不能存在状态树中没有的状态");
        static_assert(ListedStates<TTree, StateList>::AT_MOST_ONCE, "状态树中存在重复的状态");
        static_assert(TTree::NODE_COUNT == STATE_COUNT, "状态树中状态数量和状态列表中状态数量不匹配");

    public:
        // 使用 StateId 的构造函数，保证context在整个状态机生命周期内有效
        explicit Instance(Context& context, const StateId initial) : core_(context) {
            initialResult_ = transitionTo(initial);
        }
        // 使用 State 类型的构造函数，保证context在整个状态机生命周期内有效
        template <typename T>
        explicit Instance(Context& context, Initial<T>) : Instance(context, stateId<T>()) {}

        explicit Instance(Context&& context, const StateId initial) = delete;
        template <typename T>
        explicit Instance(Context&& context, Initial<T>) = delete;
        
        // 析构函数，会退出整个状态树
        ~Instance() { tree_.deepExit(core_); }

        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;

        // 查询检查状态是否活跃
        template <typename T>
        bool isActive() const { return core_.active[stateId<T>()]; }
        
        // 查询状态机是否在运行
        bool isRunning() const { return tree_.isRunning(); }
        
        // 获取初始化结果，即有没有成功进入第一个状态
        TransitionResult initialResult() const { return initialResult_; }

        // Control请求当场无法知道Guard结果，update返回本轮实际处理结果。
        TransitionResult update() {
            Control control(core_);
            // deepUpdate可能产生新的转换请求
            tree_.deepUpdate(control);
            // processRequest会处理这些请求
            return processRequest();
        }

        template <typename T> TransitionResult immediateChangeTo() { return immediateChangeTo(stateId<T>()); }
        template <typename T> TransitionResult immediateRestart() { return immediateRestart(stateId<T>()); }
        template <typename T> TransitionResult immediateResume() { return immediateResume(stateId<T>()); }
        TransitionResult immediateChangeTo(StateId target) { return transitionTo(target, TransitionType::Change); }
        TransitionResult immediateRestart(StateId target) { return transitionTo(target, TransitionType::Restart); }
        TransitionResult immediateResume(StateId target) { return transitionTo(target, TransitionType::Resume); }

    private:
        TransitionResult processRequest() {
            if (!core_.request.pending)
                return TransitionResult::NoRequest;
            const StateId target = core_.request.target;
            const TransitionType type = core_.request.type;
            core_.request.pending = false;
            return transitionTo(target, type);
        }
        // 执行转换
        TransitionResult transitionTo(const StateId target, const TransitionType type = TransitionType::Change) {
            if (target >= STATE_COUNT || !tree_.contains(target))
                return TransitionResult::InvalidTarget;
            // 执行退出保护，进入保护
            GuardControl guard(core_);
            tree_.forwardExitGuard(target, type, guard);
            if (guard.isCancelled()) {
                // std::cout << "transition cancelled by exit guard\n";
                return TransitionResult::RejectedByExitGuard;
            }
            tree_.forwardEntryGuard(target, type, guard);
            if (guard.isCancelled()) {
                // std::cout << "transition cancelled by entry guard\n";
                return TransitionResult::RejectedByEntryGuard;
            }
            // 执行转换
            const bool changed = tree_.changeToRequested(target, type, core_);
            return changed ? TransitionResult::Changed : TransitionResult::Unchanged;
        }

        Core core_;
        TTree tree_;
        TransitionResult initialResult_ = TransitionResult::NoRequest;
    };

};
} // namespace hfsm
