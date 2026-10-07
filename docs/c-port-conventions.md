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

## 8. 每个阶段的验收

1. `cmake --build`（MSVC x64、MinGW x64、MinGW x86）全过。
2. `ctest --output-on-failure` 全绿（MSVC x64 与 MinGW x64 各一次；x86 由 CI 覆盖）。
3. 如果改了渲染路径，人工比对 5 张 UI 截图与 `build-baseline/`。
4. 更新 `plan.md` / 本文档里已经变化的部分。

## 9. 体积纪律（延续 `AGENTS.md`）

- 不引入 `<filesystem>`、`<fstream>`、`<sstream>`、`fmt`、`<random>`。
- 文件和目录操作走 `src/core/WinFile.*`；数字拼字符串走 `core::AppendNumber` / `Str_AppendNumber`。
- 不新增第三方依赖。`external/` 只有 `cjson` 与 `doctest`（VC-LTL 已删除）。
