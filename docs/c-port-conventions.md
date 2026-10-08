# 纯 C 移植约定（`src/` 唯一权威约定）

> 本文档是 `plan.md` 的可执行细则。`src/` 的每一次改动都必须符合这里的规定；
> 与本文件冲突的写法一律视为错误，而不是"风格偏好"。
> 落盘时间：S0 阶段（`pure-c-port` 分支）。

## 0. 总目标

- `src/**` 全部是 `.c` / `.h`，同一份源码同时通过 **MSVC `/std:c17`** 与
  **MinGW-w64 `gcc -std=c17`**。
- `tests/` 与 `tests/scene_viewer` 保持 C++（doctest），它们只是调用 C API 的夹具。
- 不出现任何编译器分支，除非写在 `src/graphics/win_compat.h`（SDK/工具链差异）
  或 `src/audio/MfCompat.h`（Media Foundation 头缺口）里。

## 1. 命名

C 没有命名空间，所以：

| 类别 | 规则 | 例子 |
|---|---|---|
| 结构体/枚举类型 | 大驼峰，不加模块前缀，除非会撞名 | `Card`、`Cards`、`HandPattern`、`GameState`、`Scene`、`Str` |
| 枚举常量 | 全大写 + 类型前缀 | `PDK_SUIT_SPADES`、`RANK_THREE`、`PATTERN_SINGLE` |
| 函数 | `模块_动作`，模块名大驼峰 | `Core_AppendNumber`、`WinFile_ReadText`、`GameState_Play`、`D2D_CreateBrush` |
| 手写 vtable 类型 | `XxxVtbl` + `Xxx`，`Xxx` 首成员是 `const XxxVtbl* vtbl` | `SceneVtbl` / `Scene` |
| 生成/内部宏 | `PDK_` 前缀 | `PDK_CALL`、`PDK_RELEASE`、`PDK_ARRAY_COUNT` |

`extern "C"`：所有仍会被 C++ 调用（过渡期）的头文件都要包
```c
#ifdef __cplusplus
extern "C" {
#endif
...
#ifdef __cplusplus
}
#endif
```

## 2. 允许/禁止的语言特性

允许：C17 公共子集、`_Static_assert`、`//` 注释、`stdbool.h`、`stdint.h`、
`inline`、指定初始化器、复合字面量 `(T){...}`、`offsetof`。

禁止：
- VLA、嵌套函数、`__attribute__`（`DEFINE_GUID` 内部除外）、`__declspec`
  （`DEFINE_GUID`/`DECLSPEC_SELECTANY` 内部除外）、`_Generic`。
- 任何 C++ 语法：`class`、`namespace`、`template<`、`std::`、`virtual`、`override`、
  引用 `&`、重载。
- 标准库：`<string>`、`<vector>`、`<array>`、`<optional>`、`<map>`、`<set>`、
  `<memory>`、`<span>`、`<function>`、`<charconv>`、`<chrono>`、`<mutex>`、
  `<thread>`、`<atomic>`、`<algorithm>`、`<filesystem>`、`<fstream>`、`<sstream>`、
  `<random>`、`<iostream>`。
  体积约束（`AGENTS.md`）本来就禁止后几个；前几个由本次移植一并去掉。
- 隐式函数声明：GCC 16 直接报错，所以每个被调用的函数都必须有可见原型。
- 空参数列表 `f()`：C 里是"参数未指定"，统一写 `f(void)`。

## 3. 类型替换表

| C++ 用法 | C 写法 |
|---|---|
| `std::vector<Card>` / `rules::Cards` | `typedef struct { Card items[48]; int count; } Cards;`（定长值类型，零堆分配） |
| `const Cards&` 传参 | `const Cards*`（避免 96 字节按值拷贝） |
| `std::string` | `Str`（`src/core/Str.h`，malloc 支撑、可增长、永远 NUL 结尾） |
| `std::wstring` | `WStr`（同头文件） |
| 固定短文本（toast、reason） | `char buf[128]` + `Str_Append*` 的定长变体 |
| `std::map<Rank,int>` | 按点数索引的定长数组（3..15 → 13 项） |
| `std::set<int>`（选牌/推荐索引） | `uint64_t` 位掩码（手牌 ≤16） |
| `std::set<std::string>` | 小数组 + 线性查找（元素 < 10） |
| `std::map`+`std::list` LRU（字体缓存） | 定长 64 项开放寻址表 + 环形淘汰序号 |
| `std::array<T,N>` | `T x[N]` |
| `std::optional<T>` | `struct { T value; bool has; }` + `X_Set` / `X_Clear` |
| `std::unique_ptr` / `shared_ptr` | `malloc`/`free` + `Init`/`Destroy`；需要共享的手写 `long refs` + `Retain`/`Release` |
| `ComPtr<T>` | `PDK_RELEASE(p)`（`src/graphics/Com.h`） |
| `class` + `virtual` | 手写 vtable：`typedef struct XxxVtbl { ... } XxxVtbl; struct Xxx { const XxxVtbl* vtbl; void* user; };` + 每个实现一张 `static const XxxVtbl` |
| `std::function` | 函数指针 + `void* user` |
| `std::span<const Point>` | `(const Point* p, int n)` |
| `std::initializer_list<GradientStop>` | `(const GradientStop* stops, int n)` |
| `operator<=>` / 比较运算符 | `int Card_Compare(Card, Card)` |
| `enum class` | 普通 `enum` + 类型前缀常量 |
| `std::thread` / `std::mutex` / `std::atomic` | `CreateThread` / `WaitForSingleObject` / `CloseHandle`；`CRITICAL_SECTION`；`volatile LONG` + `Interlocked*` |
| `<chrono>` | `QueryPerformanceCounter` 封装 |
| `<charconv>` | 手写整数转 ASCII（`Str_AppendNumber` 已有） |
| `__uuidof` / `IID_PPV_ARGS` | `IID_X` 常量 + 显式 `void**`；GUID 定义只在 `src/graphics/iids.c`（`INITGUID`） |
| `std::lround` 等 | `ui::RoundToInt` 的 C 版 |

## 4. 手工 vtable 模式（唯一允许的多态实现）

```c
/* 头文件 */
typedef struct Scene Scene;
typedef struct SceneVtbl {
    void (*OnEnter)(Scene *self);
    void (*OnExit)(Scene *self);
    void (*Update)(Scene *self, float dt);
    void (*Render)(Scene *self, RenderContext *ctx);
    bool (*OnMouseDown)(Scene *self, float x, float y);
} SceneVtbl;

struct Scene {
    const SceneVtbl *vtbl;
    void *user;
};

static inline void Scene_Update(Scene *s, float dt) { s->vtbl->Update(s, dt); }
```

```c
/* 实现文件：每个具体场景一张表，按声明顺序全部列出 */
static const SceneVtbl kStartSceneVtbl = {
    StartScene_OnEnter,
    StartScene_OnExit,
    StartScene_Update,
    StartScene_Render,
    StartScene_OnMouseDown,
};
```

要点：
- `self` 永远是第一个参数，`Xxx` 的第一个成员永远是 `vtbl`。
- 表里**必须按声明顺序列出全部槽**，用不到的填一个什么都不做的函数，不要留空。
- 默认实现用 `static void Xxx_Noop(...)`，不要用 `NULL` 槽 + 判空。
- 不要发明第二套多态机制（不要函数指针字段散落在结构体里）。

## 5. 资源与清理

没有 RAII、没有异常。每个函数遵守：

```c
bool Foo_Init(Foo *f) {
    bool ok = false;
    f->a = NULL;
    f->b = NULL;
    if (!Step1(&f->a)) { goto cleanup; }
    if (!Step2(&f->b)) { goto cleanup; }
    ok = true;
cleanup:
    if (!ok) { Foo_Destroy(f); }
    return ok;
}
```

- 单出口 `goto cleanup`，或"每个分配都有对应释放点"的显式序列。
- COM：`PDK_RELEASE(p)` 释放并把指针置空；绝不要出现 `Base.Base`、`__uuidof`。
- `CreateThread` 的句柄必须 `CloseHandle`。
- `InterlockedIncrement` 实现 generation 取消；`std::atomic<float>` 用位模式存 `LONG`。

## 6. COM shim（`src/graphics/`）

| 文件 | 角色 |
|---|---|
| `win_compat.h` | 唯一的 SDK/工具链差异出口：包含顺序、`PDK_` 小工具、`shlwapi` 手工声明 |
| `Com.h` | `PDK_IUnknown` + `PDK_AS` / `PDK_CALL` / `PDK_CALL0` / `PDK_RELEASE` / `PDK_QUERY` |
| `d2d_c.h` | **生成**：Direct2D 的平铺 PDK vtable（`D2D1_*` 来自 `<d2d1.h>`） |
| `dwrite_c.h` | **生成**：DirectWrite 类型 + 平铺 PDK vtable（**不包含** `<dwrite.h>`） |
| `iids_gen.h` | **生成**：所有 `IID_*` / `CLSID_*` / `KSDATAFORMAT_*` 的 `DEFINE_GUID` |
| `iids.c` | 唯一定义 `INITGUID` 的 TU；GUID 只在这里落地 |
| `MfCompat.h` | Media Foundation 头缺口（见 §7） |

生成器：`python tools/gen_com_shim.py`（`--check` 只校验是否最新）。
它从 **MinGW-w64 的 C 模式头**解析 vtable 布局，因此：

- 生成文件**不要手改**；改生成器后重新生成并提交。
- 业务代码里不允许出现 `#ifdef _MSC_VER` / `__MINGW32__`。
- 调用一律 `PDK_CALL(brush, SetColor, &color)`；无参方法用 `PDK_CALL0`。
- `tests/shim_layout` 用 MinGW 的真实 SDK 头做 `sizeof` + `offsetof` 静态断言
  （`shim_layout_ref.c`），并在两个工具链上校验 vendored DirectWrite 类型
  （`shim_vendored_types.c`）。改了 shim 就跑它。

## 7. 工具链坑（踩过的，别再踩）

1. **MinGW 的 `<root>/bin` 必须在 PATH 上。** gcc 会调用
   `<root>/<triple>/bin/as.exe`，它依赖 `<root>/bin/libwinpthread-1.dll`；
   找不到时以 `0xC0000135` 退出且 **gcc 不打印任何诊断**，只返回 1。
   `CMakeLists.txt` 在 `project()` 之后有一条链接冒烟检查专门抓这个。
2. **MSVC 的 C 模式不能 include `<dwrite.h>`**（`dwrite.h:4704` 是 C++ 继承）。
   所以 `dwrite_c.h` 自带类型副本。
3. **MinGW 的 C 模式不能 include `<shlwapi.h>`**（`shlwapi.h:981` 的
   `IQueryAssociations` 未声明）。需要的 `Path*` / `SHCreateMemStream` 在
   `win_compat.h` 手写声明。
4. **MSVC 的 `DEFINE_GUIDEX` 永远只是声明**（不受 `INITGUID` 影响），所以
   `KSDATAFORMAT_SUBTYPE_*` 这类 GUID 必须由 `iids_gen.h` 自己定义。
5. **include 顺序**：`imm.h` 在 `windows.h` 之后；`mfapi.h`+`mfidl.h` 在
   `mfreadwrite.h` 之前；`ks.h` 在 `ksmedia.h` 之前。
6. **`INITGUID` 只能在 `iids.c` 里定义**，且那个 TU 不能包含 `d2d_c.h` /
   `dwrite_c.h`（否则同一批 GUID 会被定义两次）。
7. **本地 DSH 沙箱**：放在工作区内的 exe 无法在 `%TEMP%` 建目录
   （返回 EACCES），会误伤 `unit_tests` 的 temp 目录用例。
   本地跑 ctest 时把 `TEMP`/`TMP` 指到构建目录即可；CI 不受影响。
8. **不要用 `interface` 当局部变量名。** MinGW 的 `basetyps.h` 里有
   `#define interface struct`（给 COM 头用），于是 `Foo interface;` 会静默变成
   `Foo struct;`，报的是"expected '{' before ';'"这种和真因毫无关系的错。
   同理要避开的是 `small`、`near`/`far`、`IN`/`OUT`、`OPTIONAL` 这些老 Windows 宏。
9. **新加的 C 结构成员必须显式初始化。** 这一条是被 MSVC 抓出来的真事故：
   `GameState` 新增的 `externalAiControllerCount_` 没有初值，析构函数按这个
   未定值遍历数组并对垃圾指针调用 `Destroy` —— MinGW 恰好没崩，MSVC 在
   往返记录用例里直接 SIGSEGV。**规则**：C++ 类里的 C 结构成员一律用类内初始化
   （`Foo x_{};` / `int n_{0};`），数组用 `{}`；数组型成员即使每次开局都会清空，
   也要在构造函数里清一次。跨工具链的"一个崩一个不崩"几乎总是这类问题。
10. **注释里的 `*/` 会提前闭合块注释。** 写 `UI_BUTTON_*/UI_ANCHOR_*` 这类通配符描述时，
    那个 `*/` 直接结束了注释，后面的说明文字变成代码——一次 569 个编译错误，而报错位置
    离真正的元凶很远。斜杠要分开写（`UI_BUTTON_ 与 UI_ANCHOR_ 前缀`）。
11. **C++ 的"聚合初始化 + 花括号初值"在 C 里没有对应物。** 这是移植中最容易造成静默行为
    变化的一类：
    - 若某类型被**按位置聚合初始化**（`{{rect}, "文本", Style::Danger}`），而它又有
      花括号初值（`fontSize{19.0f}`、`visible{true}`），那么 C++ 会给被省略的成员套用初值，
      裸 C 结构则把它们**清零**。给 C 结构加一个用户提供的构造函数能让它不再是聚合，
      于是那些按位置的初始化点全部编译失败；不加则会得到 `visible = 0` —— 按钮直接不可见。
      **做法**：结构保持聚合（不加构造函数），在门面里提供一个 `<Type>_Make(...)` 助手
      （内部调 `<Type>_Init`），把那些按位置的初始化点改成调用它。`ui/Widgets.h` 的 `Button`
      就是这条规则的样板。
    - 若某类型**从不**被按位置聚合初始化（只写 `Type x;` 或 `Type{}` 或逐字段赋值），
      那就照惯例加 `#ifdef __cplusplus` 构造函数并调用 `<Type>_Init`。
    - 判断依据是"调用点怎么写的"，不是"类型长什么样"——动手前先 grep 一遍初始化点。

12. **常量不一定住在你猜的那个头里。** `PLAYER_HUMAN`/`PLAYER_AI1`/`PLAYER_AI2` 在
    **`rules/Scoring.h`**，不在 `rules/Card.h`——只包含后者会报"未声明"。转一个文件之前，
    先 grep 一下你要用的每个符号**定义**在哪，别按名字猜（这一条一次转换里踩了两次：
    `GameLayout.h` 和 `TalkBubbleOverlay.c`）。同理 `GAME_LAYOUT_*` 常量只在
    `scenes/GameLayout.h`，不在任何 scene 头里。
13. **共享助手放在它所属的类型旁边，不要每个文件各写一份。** 转 UI 层时一次性补了这些，
    后面每个文件都直接用：`Point_Make`/`Rect_Make`（`core/Geometry.h`，对应 C 无法移植地
    写出的花括号初始化 `{x, y, w, h}`）、`ColorF_Make`（`ui/Theme.h`）、`GradientStop_Make`、
    `TextStyle_Label`/`TextStyle_Centered`/`TextStyle_Kai`（`graphics/D2DContext.h`，按它们替代的
    C++ 门面函数命名）、`ClampF`（`ui/Anim.h`）、`Button_Make`（`ui/Widgets.h`）。
    发现自己在第二个文件里复制同一个 static 助手时，就把它提升到类型所属的头里。
14. **C 消费者可以先于它依赖的 C++ 类转换——用一条极小的 `extern "C"` 边界。**
    覆盖层转 C 时 App 还是 C++，而它们只需要"放音效、关自己、确认退出、回主菜单"四件事，
    于是有了 `app/AppApi.h`：C 头声明、`App.cpp` 里 `extern "C"` 定义、覆盖层持不透明的
    `void *app`。**先转 App 会让一次改动变成三个文件**；这条边界把顺序解耦，头文件里写明
    App 变 C 后即删除。同类情况（C 结构 vs 带 `std::string` 的 C++ 门面结构）用显式转换函数
    而不是 `static_cast`：`stats/CppCompat.h` 的 `ToCSettings`/`FromCSettings`、`ToCRound`/
    `FromCRound` 就是为此存在的。
15. **`RenderContext` 门面有"视图"形态。** `graphics/CppCompat.h` 的 `RenderContext` 持有
    `::RenderContext*` + 所有权标志；`RenderContext view(cContextPtr);` 是不拥有、不 Init/不
    Shutdown 的视图。场景/覆盖层的 C vtable 递回 C context、而 C++ 实现要门面引用时，就靠它
    （见 `core/CppCompat.h` 的 `Scene_RenderFn`）。这是 S7 为了桥接才加的形态。

## 8. 每个阶段的验收

每个阶段的提交都必须同时满足下面全部条件（本地就按这个跑，别只跑一个工具链）：

1. **三套工具链都编过**：MSVC x64、MinGW UCRT x64、MinGW UCRT x86。
   - MinGW 记得把 `<root>\bin` 加到 `PATH`（见第 7 节第 1 条），否则 gcc 静默失败。
   - MSVC 先 `cmd /c "call ...\vcvars64.bat" && cmake --build --preset vs2026-release"`。
2. **三套工具链的 ctest 全绿**，当前是 **8 个目标**：`shim_layout`、`audio_decode`、
   `unit_tests`、5 个 `ui_*`。
   - 本地跑之前把 `TEMP`/`TMP` 指到构建目录（见第 7 节第 7 条），否则 `unit_tests` 会因
     沙箱 EACCES 假失败。
3. 改了 `src/graphics/{d2d_c.h,dwrite_c.h,iids_gen.h}`、`tools/gen_com_shim.py` 或
   `tools/shimparse.py` 时，必须重新生成并让 `shim_layout` 保持绿；
   CI 另有 `python tools/gen_com_shim.py --check` 漂移检查。
4. 如果改了渲染路径，**人工**比对 5 张 UI 截图与 `build-baseline/`。
   注意截图**逐次运行并不可复现**（场景里有计时与洗牌，同一个 binary 两次的 JPEG 都不同），
   所以判据是"肉眼无差异"，**字节数漂移不能当作回归证据**。
5. 更新 `plan.md`（追加一条"修订 N 完成记录"）与本文档里已经变化的部分。
6. 提交前清掉一次性改写脚本（`tools/_*.py`），并确认 `git status` 干净。

## 9. 体积纪律（延续 `AGENTS.md`）

- 不引入 `<filesystem>`、`<fstream>`、`<sstream>`、`fmt`、`<random>`。
- 文件和目录操作走 `src/core/WinFile.*`；数字拼字符串走 `core::AppendNumber` / `Str_AppendNumber`。
- 不新增第三方依赖。`external/` 只有 `cjson` 与 `doctest`（VC-LTL 已删除）。

---

## 附录 A：rules / game / stats 的目标 API（下一步照这个写，别自己另发明一套）

### A.1 `rules/Card.h`

**已落地（S2，commit `3c742b1`），与下面的草案有两处有意偏离，以落地版为准：**
- `Rank`/`Suit`/`PatternType`/`PlayerId` 是 **`uint8_t` typedef + 匿名枚举常量**，
  不是普通 `enum`（普通 enum 在 C 里是 `int`，会让 `Card` 8 字节、`Cards` 388 字节）。
- `reason` 用 `char[PATTERN_REASON_CAP]`，`RankName`/`SuitName`/`PatternName`/`PlayerKey`
  返回字符串字面量（`const char*`）。
- **`Cards` 自 S4b 起在整个仓库都是 C 定长结构**（`Card items[48]` + `count`），
  不再是 `std::vector<Card>`。C++ 侧靠 `rules/Card.h` 里一段**带 `#ifdef __cplusplus` 的
  成员垫片**（`size/empty/push_back/operator[]/begin/end/...`）暂时沿用 vector 形状；
  那个头在 `tools/check_c_only.py` 的白名单里，S8 收尾时连同门面一起删。
  聚合初始化一律走 `MakeCards({...})`——直接写 `Cards a{C(...)}` 只会填 `items`、
  `count` 仍是 0，**静默变成空手牌**。
- `src/rules/CppCompat.h` 是**临时** C++ 门面（路线 B）。删门面的时机 =
  `src/` 最后一个 C++ 文件消失的同一刻。

```c
typedef enum Suit { SUIT_SPADES, SUIT_HEARTS, SUIT_DIAMONDS, SUIT_CLUBS } Suit;
typedef enum Rank {
    RANK_THREE = 3, RANK_FOUR, RANK_FIVE, RANK_SIX, RANK_SEVEN, RANK_EIGHT,
    RANK_NINE, RANK_TEN, RANK_JACK, RANK_QUEEN, RANK_KING, RANK_ACE, RANK_TWO = 15
} Rank;

typedef struct Card { Rank rank; Suit suit; } Card;

/* Replaces `std::vector<Card>` / `rules::Cards`.  Fixed capacity: the deck is
 * 48 cards and the largest hand is 16, so nothing in this project needs more.
 * Zero heap traffic and freely copyable. */
enum { CARDS_MAX = 48 };
typedef struct Cards { Card items[CARDS_MAX]; int count; } Cards;

void  Cards_Clear(Cards *c);
bool  Cards_Push(Cards *c, Card card);          /* false when full */
bool  Cards_Remove(Cards *c, Card card);        /* removes first match */
void  Cards_RemoveAt(Cards *c, int index);
bool  Cards_Contains(const Cards *c, Card card);
int   Cards_IndexOf(const Cards *c, Card card);
void  Cards_Append(Cards *dst, const Cards *src);
int   Cards_Compare(const Cards *a, const Cards *b); /* was operator<=> (rank, then suit) */

int   RankValue(Rank rank);
int   SortValue(Card card);
bool  IsSpadeThree(Card card);
/* The old std::string returns become caller-owned Str (free with Str_Free). */
void  RankName(Str *out, Rank rank);
void  SuitName(Str *out, Suit suit);
void  Card_ToString(Str *out, Card card);
void  Cards_ToString(Str *out, const Cards *cards);
void  SortByGameOrder(Cards *cards);
```

`enum class` 的具名限定用**正则机械替换**（全仓库）：
`rules::Rank::X` → `RANK_X`、`rules::Suit::X` → `SUIT_X`、
`rules::PatternType::X` → `PATTERN_X`、`rules::PlayerId::X` → `PLAYER_X`
（`Player`→`PLAYER_HUMAN`、`Ai1`→`PLAYER_AI1`、`Ai2`→`PLAYER_AI2`）。
约 1,276 处，替换后逐个文件编译核对，不要手工改。

### A.2 `rules/HandPattern.h`、`MoveValidator.h`、`Scoring.h`、`RuleSet.h`

- `PatternResult.reason` / `MoveValidation.reason` / `AiMoveChoice.reason`：定长
  `char reason[128]`（计划 §2）。写入一律走带长度检查的 helper，禁止裸 `strcpy`。
- `HandPattern.IsValid()` → `bool HandPattern_IsValid(const HandPattern *p);`
- `PatternResult IdentifyPattern(const Cards *cards, int handSizeBeforePlay, bool allowShortFinal);`
  （原来的默认参数改成显式传值，调用点补 `-1, false`）
- `RoundScoreInput` / `RoundScoreResult` 里的 `std::array<int,3>` 直接写 `int x[3]`，
  `std::vector<BombScoreEvent> bombs` → `BombScoreEvent bombs[8]; int bombCount;`
  （单局最多 12 张炸弹牌，8 个事件足够；越界时丢弃并断言）。`SpringInfo.losers`
  → `PlayerId losers[3]; int loserCount;`
- `PlayerKey` → `const char *PlayerKey(PlayerId player);`（返回静态字符串，无分配）

### A.3 `game/StrategyMetadata.h`

四个工厂都返回字符串字面量，所以直接：

```c
typedef struct StrategyMetadata { const char *strategy; const char *strategyVersion; } StrategyMetadata;
StrategyMetadata HumanStrategyMetadata(void);
StrategyMetadata BasicStrategyMetadata(void);
StrategyMetadata StrongStrategyMetadata(void);
StrategyMetadata RulesStrategyMetadata(void);
```

### A.4 `game/Player.h`、`game/AiStrategy.h`

```c
typedef struct PlayerState {
    char name[64];          /* settings 的玩家名上限远小于此 */
    Cards hand;
    bool hasPlayedCards;
} PlayerState;
bool PlayerState_Empty(const PlayerState *p);
```

`AiContext`：`std::optional` / `std::vector` 内嵌成员按计划 §2 替换：

```c
typedef struct PassObservation { HandPattern pattern; int remainingCards; } PassObservation;
typedef struct OptionalPass { PassObservation value; bool has; } OptionalPass;
typedef struct PassHistory { PassObservation items[48]; int count; } PassHistory;
```

`AiStrategy` 用 §4 的手写 vtable：

```c
typedef struct AiStrategy AiStrategy;
typedef struct AiStrategyVtbl {
    AiMoveChoice (*ChooseMove)(AiStrategy *self, const Cards *hand, const AiContext *context);
    StrategyMetadata (*Metadata)(AiStrategy *self);
} AiStrategyVtbl;
struct AiStrategy { const AiStrategyVtbl *vtbl; void *user; };

/* 每个实现一个返回自静态表的构造 + 一个 Destroy（无分配时可空） */
AiStrategy *BasicAiStrategy_Create(void);
AiStrategy *StrongAiStrategy_Create(void);
void        AiStrategy_Destroy(AiStrategy *strategy);
AiMoveChoice BasicAiStrategy_ChooseMove(const Cards *hand, const AiContext *context);
void         BasicAiStrategy_Recommend(const Cards *hand, const AiContext *context,
                                       int limit, AiMoveChoice *out, int *outCount);
```

`tests/rules_tests/TestWeakAiStrategy.h` 改成填一张 `static const AiStrategyVtbl` + `void *user`
（对应 `GameState_SetLocalAiStrategy`），验证 vtable 注入点。

### A.5 `game/GameState.h`

- `class GameState` → `typedef struct GameState GameState;` + `GameState_Init/Destroy`
  （内部固定缓冲，尽量零分配）；所有成员函数 → `GameState_Xxx(GameState *s, ...)`。
- `std::optional<...> lastPattern` / `nextRoundLeader` → `{ T value; bool has; }` + `_Set/_Clear`。
- `std::set<int>`（选中 / 推荐索引）→ `uint64_t` 位掩码（手牌 ≤16），涉及
  `GameState.cpp:598/643/706/734` 附近的位运算改写。
- 事件列表（`ClearEvents` / 事件队列）用定长环形缓冲，不要把 `std::vector` 换成无界 realloc。
- 线程与取消：`LocalAiController` 用 `CreateThread`/`WaitForSingleObject`/`CloseHandle` +
  `InterlockedIncrement` 的 generation；`StrongAiStrategy` 的 8 worker 同理；
  `std::atomic<float> masterVolume` → `LONG` 存 bit pattern + `InterlockedExchange`。
  句柄一定 `CloseHandle`，不引入 winpthreads。

### A.6 `stats/*`

`AppSettings` / `StatStore`：`std::string` → `Str`，`std::string` 列表 → `StrList`
（`src/core/Str.h`，S1 引入）。JSON 字段名与 schema **一个字节都不许变**
（`StatsTests` 是判据）。cJSON 已经是 C，改动应很小。

### A.7 `tests/rules_tests/*`

保持 C++ + doctest，只把 `rules::` / `game::` 调用改成上面的 C API：
`TestHelpers.h` 提供 `C(rank, suit)`、`MakeCards({...})` 之类的薄夹具；
`GameStateTests.cpp`（1361 行）改动量最大，先改夹具再机械替换。
**这些测试是移植正确性的主要判据，禁止为了让测试通过而改产品语义。**
