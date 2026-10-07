# 把 pao-de-kuai 改造为纯 C，双工具链：MSVC（保留）+ MinGW-w64 GCC (UCRT)（主发布）

> 说明：本文件由已批准的改造计划落盘而成，供人工批注修改。
> 初版批准时间：2026-10-07，分支 `pure-c-port`，起点 commit `7f7c15d`。
>
> **修订 2**：工具链与 CRT 策略最终定为——
> 1. `src/` 纯 C 化后**删除 VC-LTL**（`external/vc-ltl/` 整个 vendor 目录，以及 CMake/CI/文档/About 中全部引用）；
> 2. **MinGW-w64 GCC (UCRT) x64 成为主要发布构建**（替换 CI 里的 `build-vs2026-vcltl-x64`，继续发布到 MinIO），exe 链接系统 `ucrtbase.dll`；
> 3. **MSVC 仍然必须能独立编译整套 `src/`**（CI 里 `/MD`、`/MT` × x64/x86/arm64 六个组合原样保留），只把 `build-vs2026-vcltl-x86` 换成 MinGW x86；
> 4. **放弃 Win8 兼容基线**，最低系统改为 Win10（`WINVER`/`_WIN32_WINNT` = `0x0A00`）。
>
> 原目标不变：`src/` 全部 `.c`/`.h`，`tests/` 与 `scene_viewer` 保留 C++/doctest。
>
> **修订 3（本次）**：把本地两条 MinGW 工具链与 CI 的下载方式钉死——
> 1. 本地：x64 = `D:\_\3rd\mingw64-ucrt`（gcc **16.1.0**），x86 = `D:\_\3rd\mingw32-ucrt`（gcc **16.2.0**，已核对可用）；环境变量定为 `PDK_MINGW_ROOT` / `PDK_MINGW32_ROOT`。
> 2. CI：从 **niXman/mingw-builds-binaries** 的 release tag `16.2.0-rt_v14-rev1` 下载 `x86_64-16.2.0-release-win32-seh-ucrt-...7z` 与 `i686-16.2.0-release-win32-dwarf-ucrt-...7z`（**win32 线程 + ucrt**，与本地构建参数一致），带 SHA256 校验并缓存。**（修订 5 已改为"解析 latest、不 pin"；本条的资产命名规则与 win32+ucrt 约束仍然有效。）**
> 3. 由此产生一个待你拍板的版本差：**建议本地 x64 也升到 16.2.0**，与 x86 和 CI 对齐（§8 开放项 1）。
>
> **修订 4（本次）**：启用完全权限后重跑了之前被卡住的实测，原"环境限制"取消，并据此收紧计划——
> 1. **`as.exe` 之谜解开**：不是沙箱，是 PATH 里缺 `<root>\bin`（`as.exe` 依赖 `libwinpthread-1.dll`）→ `0xC0000135` 且 gcc **不报错**。CI/本地都必须前置 `bin`。
> 2. **CRT 底噪实测**：x64/x86 空程序 `-Os -s` 均 **15,872 B**，`+--gc-sections` 15,360 B，依赖只有 `KERNEL32` + `api-ms-win-crt-*`（无 msvcrt/vcruntime/libgcc）。**679,424 B 基线里 CRT 只占约 15 KB**，体积风险主要看应用代码。
> 3. **shim 布局假设已用编译器验证**：7 个接口的"平铺 PDK 结构 == SDK vtable"静态断言全部通过；并抓到两处真实错误（`ID2D1Factory` 其实直接继承 `IUnknown`、`IDWriteFactory` 必须按全量顺序占位）。§3.1/§3.3 已改为可照抄的规则。
> 4. **IID 来源定死**：`#define INITGUID` + 单一 TU，`-luuid` 补不齐（实测缺 3 个符号）。
> 5. **`windres` 实测可用**（真实 ico + RCDATA + UTF-8 中文注释）；**`-flto` 在最小程序上零收益**。
>
> **修订 5**：工具链版本策略按你的决定改为——**本地不追平（x64=16.1.0、x86=16.2.0），CI 每次解析 niXman 的 latest release，不 pin 版本**。为了不因此丢掉可复现性，§5 补了三件事：正则选资产（不靠版本号）、用 API 的 asset `digest` 校验完整性（不写死哈希）、CI 打印解析到的 tag/asset/`gcc --version` 并提供 `PDK_MINGW_TAG` 逃生舱。§8 开放项 1 关闭。
>
> **修订 6（S0 完成记录，2026-10-07，commit `18abe57`）**：S0 已落地并**三条链路全绿**
> （MSVC x64 7/7、MinGW UCRT x64 7/7、MinGW UCRT x86 7/7；5 张 UI 截图与基线逐像素一致）。
> 与计划正文的偏差与实际做法：
> 1. **shim 不再做 `#ifdef` 分流**：`dwrite.h` 在**两套工具链上都不 include**，
>    `graphics/dwrite_c.h` 自带类型副本（由生成器从 MinGW 头提取），因此业务代码里
>    **一个编译器分支都没有**。只有 `tests/shim_layout/shim_layout_ref.c` include `<dwrite.h>`
>    （并 `#define PDK_SKIP_DWRITE_TYPES`）来和真实 SDK 做 `sizeof`/`offsetof` 断言。
> 2. **生成器已实现**：`tools/gen_com_shim.py`（+ `tools/shimparse.py`）生成
>    `src/graphics/{d2d_c.h,dwrite_c.h,iids_gen.h}` 与 `tests/shim_layout/ShimLayoutChecks.h`；
>    `--check` 模式供 CI 做漂移检测。不采用"只给用到的方法定型"，而是**全部方法都定型**
>    （D2D 参数类型走 `<d2d1.h>`；DWrite 未用到的方法留 `void*` 占位），
>    未识别的接口参数统一折成 `void*`（ABI 相同）。
> 3. **`iids_gen.h` 还要定义 `KSDATAFORMAT_SUBTYPE_{PCM,IEEE_FLOAT}`**：MSVC 的
>    `DEFINE_GUIDEX` 在任何情况下都只是声明（不受 `INITGUID` 影响），这两个 GUID
>    靠任何头文件都不会有定义。值从 MinGW 的 `DEFINE_GUIDSTRUCT("...", NAME)` 取。
> 4. **新增 `src/audio/MfCompat.h`**：MinGW 头里没有 `MFCreateMFByteStreamOnStream`
>    的声明（但 `libmfplat.a` 导出它），手工补原型。
> 5. **`StringUtil` 合并进 `Str`**（计划 §2 本来就把 `Str_AppendNumber` 放在 `Str.h`），
>    `src/core/StringUtil.*` 删除，不保留同名包装。
> 6. **ctest 变成 7 个目标**（新增 `shim_layout`，即计划 §6 要求的那个自检）。
> 7. **体积现状（仍是 C++ 混编，仅供对照）**：MinGW x64 `pao_de_kuai.exe` = 756,224 B
>    （`-Os -s --gc-sections`；但 UI/场景还是 C++，仍链了 `-static-libstdc++`）；
>    MSVC `/MT` x64 = 803,328 B。等 `src/` 全 C 后去掉 `-static-libstdc++` 才是有意义的数字。
> 8. **本地 DSH 沙箱坑**：放在工作区内的 exe 无法在真实 `%TEMP%` 下建目录（EACCES），
>    会让 `unit_tests` 的 temp 目录用例假失败。本地跑 ctest 时把 `TEMP`/`TMP` 指向构建目录。
> 9. **逐阶段做法确定为"依赖顺序 + 每步保持三链路全绿"**：核心叶子 → rules/stats/game →
>    graphics → audio → UI/场景/app（此时才把 `Scene`/`Overlay` vtable 化）。
>    `docs/c-port-conventions.md` 是后续每一步的唯一约定来源。
>
> **修订 6 追加（S1 完成记录，commit `c8a52a4`）**：
> - 已 C 化：`core/Str.c`（含新增 `StrList`）、`core/WinFile.c`、`core/Tween.c`、
>   `core/Timer.c`、`core/Geometry.h`、`ui/Anim.h`；删除 `core/StringUtil.*`。
> - **新增 `src/core/CppCompat.h`（临时，路线 B）**：把新 C core 适配回
>   `std::string` 风格的旧调用点，避免同一批调用点改两遍。文件头列出了仍依赖它的
>   10 个 `.cpp`，S8 必须随最后一个 C++ 文件一起删除。`tools/check_c_only.py` 目前会
>   把它算作 `std::` 残留，这是预期的。
> - **顺带做掉的必要重命名**：`core::Point/Rect/...` 等 251 处、`ui::RoundToInt` 等 4 处
>   去掉命名空间限定（C 里本来就没有命名空间，早晚都要改）；
>   `Rect::Contains` → `Rect_Contains`、`EaseOutBack(t,o)` → `EaseOutBackWith(t,o)`、
>   `ViewTransform{}` → `ViewTransform_Identity()`；
>   `D2DContext::ViewTransform()` 改名为 `View()`（访问器名与类型名冲突）。
> - 进度：`src/` 还有 **48 个 `.cpp`**，`check_c_only.py` 报 293 处 `/std::/` 类残留。
> - 体积（仍混编）：MinGW x64 758,272 B、MSVC /MT x64 798,208 B。
>
> **修订 7（S2 完成记录，commit `3c742b1`）——`src/rules/` 已是纯 C**：
> - `Rank`/`Suit`/`PatternType`/`PlayerId` 定为 **`uint8_t` + 匿名枚举常量**，不是普通
>   `enum`：C 里 enum 是 `int`，会让 `Card` 变 8 字节、`Cards` 变 388 字节，而 AI 搜索在
>   热路径上大量拷贝手牌。现在 `Card` 2 字节、`Cards` 定长 48 张。这是对 §2 表格
>   "`enum class` → 普通 `enum`" 的**有意偏离**，理由与计划要的"定长值类型 + 搜索更快"一致。
> - `Cards` 是定长值类型（`Card items[48] + count`），全写入走 `Cards_Push`；
>   `SortByGameOrder` 用插入排序（手牌 ≤16 张），不引 `qsort`。规则层**零堆分配**。
> - `reason` 用 `char[128]`；`RankName`/`SuitName`/`PatternName`/`PlayerKey` 返回字符串字面量。
> - `HandPattern.c` 用按点数索引的计数数组替掉 `std::map<Rank,int>`，遍历顺序与 map 一致。
> - 枚举常量机械重命名 33 个文件（`rules::Rank::Three` → `RANK_THREE` 等，共 1,024 处），
>   `pattern.IsValid()` 13 处改为 `rules::IsValid(pattern)`。
> - **新增 `src/rules/CppCompat.h`（临时，路线 B）**：`src/rules` 以外**所有** rules 符号都是
>   `rules::X` 限定写法（已实测：0 处非限定引用），所以门面放在 `namespace pdk::rules` 里即可
>   零改动兼容：`Cards` 仍是 `std::vector<Card>`，内联重载负责打包成 C 定长数组；六个原本返回
>   `std::string` 的 helper 仍返回 `std::string`；`PaoDeKuaiRules` 保留类形态。
> - **性能副作用（计划 §9 期望的）**：`unit_tests` 从 MinGW 0.39s → 0.20s、MSVC 0.38s → 0.16s。
> - 进度：`src/` 剩 **41 个 `.cpp`**，`check_c_only.py` 报 275 处残留。体积：MinGW x64 753,152 B、
>   MSVC /MT x64 800,256 B（仍链 libstdc++）。
>
> **修订 8（S3 + S4a 完成记录，commit `f2c781f` / `db5c1b1`）**：
> - **S3 `src/stats/` 纯 C**：`AppSettings` 用 `char playerName[64]` 等定长缓冲；
>   `RoundRecord` 是定长值类型；`DailyStat` 用 Init/Free/Append 管理可增长的 rounds
>   数组；`StatStore` 是定长 root + 自由函数；`LocalDateTime` 的 chrono+strftime 换成
>   `GetLocalTime` + `Str_AppendPaddedNumber`（C 层里没有 snprintf）。
>   **兼容性直接实测过**：把仓库里由旧代码写的 `appsettings.json` 读进来再写出去，
>   170 字节**逐字节相同**；手工造一天数据写出的 `stat/yyyyMMdd.json` 与文档 schema
>   完全一致，重读和月/历史聚合都对。
> - **S4a `src/resources/` 纯 C**：`ByteBuffer`（malloc + size）替代
>   `std::vector<std::uint8_t>`；`GetCardAtlasInfo()` 返回指针而不是引用；
>   `IconAtlasData.cpp` 因为只有一行 include 被直接删除。
> - 两个模块各有一个临时门面：`src/stats/CppCompat.h`、`src/resources/CppCompat.h`。
> - 进度：`src/` 剩 **36 个 `.cpp`**（原 53），`check_c_only.py` 残留 267 处（原 313）。
>   体积：MinGW x64 747,008 B、MSVC /MT x64 772,608 B。
> - **`game/`（7 个 `.cpp`，约 3,500 行）与 `graphics/`（5 个）、`ui+scenes+overlays+app`（21 个）
>   仍在后面**；体积达标必须等这些全部转完、去掉 `-static-libstdc++` 之后才可能实现。
>
> **修订 9（S6 完成记录，commit `ef10c1b`）——`src/audio/` 已是纯 C**：
> - `std::shared_ptr<const std::vector<float>>` → 引用计数 `SampleBuffer`；
>   `std::mutex` → `CRITICAL_SECTION`；`std::thread` → `CreateThread`+`WaitForSingleObject`；
>   `std::atomic<bool>` → `volatile LONG`；`std::atomic<float>` 主音量 → **bit pattern 存 LONG**
>   + `InterlockedCompareExchange/Exchange`（没有 32 位 interlocked float 交换）；
>   `std::map<SoundId, Sound>` → 按 SoundId 索引的定长表；声部表与待播队列各 24 项定长数组。
> - **`IMMNotificationClient` 是应用侧唯一手写的 COM 回调**，8 槽全部写出，并加了
>   `_Static_assert(sizeof(PDK vtable) == sizeof(IMMNotificationClientVtbl))`——**两套工具链都会查**。
> - **实测抓到一个真问题**：`iids.c` 原本 include `<mmdeviceapi.h>`/`<audioclient.h>` 靠
>   INITGUID 拿 GUID，但这只对 MinGW 有效（MinGW 用 `DEFINE_GUID`）；MSVC 那两个头只有
>   `EXTERN_C const IID name;` 声明，于是 MSVC 链接期报 6 个未解析符号（`IID_IAudioClient`、
>   `IID_IAudioClient3`、`IID_IAudioRenderClient`、`IID_IMMNotificationClient`、
>   `IID_IMMDeviceEnumerator`、`CLSID_MMDeviceEnumerator`）。修法：生成器从 MinGW 头把
>   这 6 个 GUID 一起产出到 `iids_gen.h`，并从 `iids.c` 里移除那两个 include，
>   保证两套工具链**各自只有一份定义**。
> - **新增 ctest 目标 `audio_decode`**（`tests/audio_decode/AudioDecodeTest.c`）：启动 MF 后
>   解码全部 21 个内嵌音效，断言 `sampleRate == 44100`、样本非空、且在 577 样本延迟之上
>   **首样本必须恰好为 0**（延迟裁剪淡入的签名）。不需要声卡，所以三套工具链都能跑。
>   ctest 因此变成 **8 个目标**。
> - `AudioEngine` 生命周期用一次性探针实测过：Initialize 返回 1、重复调用幂等、Available 为真、
>   Play 接受、Destroy 干净回收（含渲染线程 join）。
> - 进度：`src/` 剩 **33 个 `.cpp`**，`check_c_only.py` 残留 254 处。体积：MinGW x64 741,376 B、
>   MSVC /MT x64 764,416 B；依赖表仍然只有系统 DLL（无 msvcrt/vcruntime/libgcc/libwinpthread）。
>
> **修订 10（S4b 完成记录，commit `039cf83`）——`Cards` 在全仓库翻成 C 定长值类型**：
> 这是附录 B 路线 A 预判的"必须一次跨过"的那一步：只要 `rules::Cards` 对 C++ 还是
> `std::vector<Card>`，任何 C 文件都没法持有手牌，game 层就无法开工。
> - **做法（避免长时间断树）**：`src/rules/Card.h` 里加了一段**明确标注为临时**的
>   `#ifdef __cplusplus` 成员垫片（`size/empty/clear/reserve/resize/push_back/emplace_back/
>   pop_back/operator[]/front/back/begin/end/erase/insert`），让约 255 个 vector 形状的调用点
>   先继续编过；它随 `rules/CppCompat.h` 一起删除，文件一旦变成 `.c` 就自动失去它。
>   `tools/check_c_only.py` 已把该头列入白名单并写明理由。
> - **垫片里的"清零默认构造函数"是必须的**：`Cards x;` 原来是空 vector，直接换成裸 struct 会让
>   `count` 未初始化，第一次 `push_back` 就越界写。
> - `rules/CppCompat.h` 去掉 vector 别名与 `ToCCards/FromCCards`，改成对指针形 C API 的引用形薄包装。
> - **brace-aware 代码改写**（`tools/_wrap_cards_init.py`）：把 `Cards a{C(...)}` 之类的聚合初始化
>   全改成 `MakeCards({...})`——否则 `Cards a{C(...)}` 只初始化 `items` 数组而 `count` 仍为 0，
>   **静默变成空手牌**。它只动"元素全是 `C(...)` 的最内层花括号组"，且**故意不动空 `{}`**
>   （空 `{}` 有歧义：可能是 `HandPattern` 或 `std::string`）。第一遍的误伤已手工修回。
> - 结果：**MinGW x64 741,376 → 726,016 B、MSVC /MT x64 764,416 → 761,344 B**（`std::vector` 机制消失）。
> - 下一步 game 层逐文件开工：`StrategyMetadata`/`Player`/`AiPlayer`/`TurnRecord`/`RoundRecorder`/
>   `AiStrategy`(vtable)/`ExternalAiController`+`LocalAiController` → `GameState` → `StrongAiStrategy`
>   + `RoundTraceRecorder`。`TurnRecord`/`ExternalAiRequest` 里的 `std::string`/`std::vector` 成员
>   是主要改动面，`GameState.cpp`（1,348 行）是它们的消费大户。
>
> **修订 11（S4c 完成记录，commit `836ab57`）——game 层开始，第一批值类型已纯 C**：
> - `StrategyMetadata.h`：两个字段都是 `const char*` 字面量（新增 `UnknownStrategyMetadata()`）。
> - `Player.h`：`char name[PDK_SEAT_NAME_CAP]` + 定长 `Cards hand` + `hasPlayedCards`，
>   配 `static inline PlayerState_Empty/Init`。常量叫 `PDK_SEAT_NAME_CAP` 而不是
>   `PDK_PLAYER_NAME_CAP`，因为后者已被 `stats/AppSettings.h` 占用（同名会冲突）。
> - `TurnRecord.h`：文本成员全是 `char[]`，用新增的 `core/Str.h: Str_CopyTo` 填充
>   （它替换了我手写在 3 个 C 文件里的同一段拷贝循环）；`optional<HandPattern>` →
>   值 + `has` 标志；`GameAction.ranks` 从 `std::vector<std::string>` 变成
>   `char[20][16]` + count，走 `GameAction_Clear/Set/AddRank`；
>   `TurnDecisionSource/Reason` 的 scoped enum → `TURN_SOURCE_*` / `TURN_REASON_*`。
> - `TurnRecord.cpp` → `TurnRecord.c`；`PlayerLabel/SourceLabel/ReasonLabel` 返回字面量。
> - **新增 `src/game/CppCompat.h`（临时）**：把 C 类型与常量以 `using` 重新导出到
>   `namespace pdk::game`，由 `GameState.h`/`RoundTraceRecorder.h`/`AiStrategy.h` 拉进来，
>   这样未转换的 C++ 调用点写法不用改。
> - 踩到的两个真实细节：① `TurnSnapshot` 位置式聚合初始化会因为新插入的 `hasLastPattern`
>   整体错位，所以改成逐字段赋值；② 测试里 `metadata.strategy == "strong"` 现在是**指针比较**，
>   5 处改成 `std::string(...) == ...`（doctest 把 `char[64]` 打印成空白是它的 stringify
>   假象，447 条断言证明数据本身是对的）。
> - 进度：`src/` 剩 **32 个 `.cpp`**，`check_c_only.py` 残留 244 处；MinGW x64 exe
>   **726,016 → 717,824 B**。
>
> **修订 12（S4d 完成记录，commit `98fe024`）——AI 策略接口已纯 C**：
> - `AiStrategy` 从虚基类变成 `(vtable, user)` 二元组（`ChooseMove`/`Metadata`/`Destroy`）；
>   两个内置策略改成**自由函数**，不再为了问一手牌而 `make_unique` 一个对象。
>   内置单例的 `Destroy` 为 NULL，所以只有注入的策略（测试那个暴力策略）需要释放。
> - `AiContext` 的两个容器成员变定长：`optional<PassObservation>` → 值 + `has` 标志；
>   每人的 `vector<PassObservation>` → `PassHistory`（数组 + count，上限是一局能过的轮数）。
>   C 没有数组赋值，所以 4 处整数组拷贝走 `PassObservations_Copy`/`PassHistories_Copy`。
> - 新增 `AiMoveChoice_MakePass/MakePlay`：`char[]` 成员**只能用字面量做聚合初始化**，
>   而所有"不要 + 理由"的站点都是 `cond ? "a" : "b"`——C++ 版本因为成员是 `std::string`
>   才能那么写。
> - 新增纯 C 的 `AiStrategy.c`（两个 vtable、适配器、`AiStrategy_Release`、Metadata）与
>   `AiPlayer.h/.c`（座位按值持有 `(vtable, user)` + 所有权标志）。**GameState 必须显式
>   `AiPlayer_Init` 三个座位**——裸 C 结构的 vtbl 是垃圾值。
> - `AiStrategy.cpp` → `BasicAiStrategy.cpp`（名字终于和内容一致），成员函数改成命名空间内
>   帮助函数 + 一个 C 入口；`StrongAiStrategy.cpp` 同样处理。C 入口定义在
>   `namespace pdk::game` **之外**，否则和头里的 `extern "C"` 声明对不上。
> - `game/CppCompat.h` 补上 `AiStrategyClass` + `BasicAiStrategy`/`StrongAiStrategy` 包装，
>   以及 `Borrow`/`Transfer` 两个桥（不接管 / 接管 C++ 策略的所有权）。
> - 进度：MinGW x64 exe **717,824 → 713,216 B**；`check_c_only.py` 残留 237 处。
>
> **修订 13（S4e 完成记录，commit `8339e5f`）——外部 AI 控制器已纯 C**：
> - `ExternalAiController` 抽象类 → `(vtable, user)`；两个 `std::optional` 返回 → bool + 出参。
> - **删掉两个死字段**：`ExternalAiRequest::humanName` / `::history` 每回合都被构造
>   （history 还整份拷贝 turn-record 向量）却从没有人读过，包括测试替身。
>   删掉后每回合少一次 vector 拷贝，请求固定约 4 KB。
> - `LocalAiController` → `.c`：`std::thread` 分离线程 + `shared_ptr<SharedState>` →
>   `CreateThread` + `CloseHandle` + **显式引用计数的 `LocalAiShared`**
>   （控制器持一份、每个在飞 worker 各持一份），generation 计数器让取消是"正确"
>   而不只是"大概率"；`std::map<PlayerId, LocalAiKind>` → 定长数组 + 每座位配置标志。
> - 所有权：`GameState` 销毁交给它的控制器，所以接口值是同一对 `(vtable, user)` 的**借用**
>   ——这正是接口做成小可拷贝结构而不是引用计数句柄的原因。`GameState` 新增析构函数释放它们。
> - C++ 版的 `try/catch(...)` 兜底没有了：C 没有异常，两个策略都是纯值计算。
> - **踩坑**：局部变量不能叫 `interface`（MinGW `basetyps.h` 里
>   `#define interface struct`），已写进约定文档第 7 节。
> - **双工具链抓到真 bug**：新增的 `externalAiControllerCount_` 没有初值，析构函数按未定值
>   遍历并对垃圾指针调 `Destroy`——**MinGW 恰好不崩、MSVC 在往返记录用例里 SIGSEGV**。
>   已给全部新成员加类内初始值，并在构造函数里清一次 pass 数组。这正是保留双工具链的价值。
> - 进度：`src/` 剩 **31 个 `.cpp`**，`check_c_only.py` 残留 222 处；
>   MinGW x64 exe **713,216 → 707,584 B**。
>
> **修订 14（S4f 完成记录，commit `9137252`）——记录器已纯 C，并清掉两个空 TU**：
> - `RoundRecorder` → `.c`：原类只装了一个 `StatStore`，默认构造函数本来就把 root 解析成
>   进程当前目录，所以 C API 就是 `RoundRecorder_Init` + `RoundRecorder_AppendToday`。
> - `RoundTraceRecorder` → `.c`，且 `RoundTrace` 从**拥有数据**（`std::array` + `std::vector<TurnRecord>`）
>   改成**只读视图**（`const PlayerState*` / `const TurnRecord*` / `const RoundRecord*` + count）。
>   这条不只是整洁：`TurnRecord` 约 **2.6 KB**（两个快照 + 两个 action + 256 字节校验消息 +
>   512 字节 trace），一局的记录是几百 KB，旧代码每次写盘都要整份拷贝；记录器只是遍历，
>   所以现在直接指向 GameState 自己的存储。
> - 旧类只持有一个 root，所以 C API 直接收 root 参数（NULL/空 = 进程当前目录），
>   没有为单字段结构造一个 struct。
> - JSON 写入是逐行照搬（NULL pattern 等于旧的 nullopt，仍然写成 JSON null；时间键仍然去掉
>   `:` 并在空时回退 `"000000"`）。
> - **验证方式值得一提**：已有的往返记录测试断言的是**精确 JSON 字节串**（含 cJSON 的制表符
>   分隔）——`"schemaVersion":\t2`、`"rulesVersion":\t"pdk48-v1"`、`"turnOrder":\t"counterclockwise"`、
>   `"3S"` 等，所以格式或字段一旦回归会直接失败，不会溜过去。它原样通过。
> - 另外删掉 `graphics/SpriteAtlas.cpp` 与 `graphics/TextRenderer.cpp`：两个文件各自只有一行
>   `#include`，对应的头是纯 header-only 类，属于零贡献 TU。
> - 进度：`src/` 剩 **27 个 `.cpp`**，`check_c_only.py` 残留 210 处；
>   MinGW x64 exe **707,584 → 701,440 B**。
>
> **修订 15（S4g 完成记录，commit `dbe58ae`）——AI 内部数据结构 + 基础策略已纯 C**：
> - `AiStrategyInternal.h` → C 头 + `AiStrategyInternal.c` 装容器。STL 容器逐一替换：
>   `std::map<Rank,int> CountRanks` → `AiRankCounts`（`int[16]` 按 rank 索引；map 唯一提供的
>   就是升序遍历，rank 循环完全等价，而且更快更小）；`std::vector<Rank>` → `AiRankList`；
>   `std::vector<Candidate>` → `AiCandidateList`；`std::vector<uint64_t> masks` → `AiMaskList`
>   （**C(16,8)=12870**，定长数组要 100 KB 栈，必须可增长）；`std::set<std::string>` →
>   `AiKeySet`（FNV-1a 哈希，线性重扫在候选数上是平方级，搜索会明显变慢）。
> - `BasicAiStrategy.cpp` → `.c`；两个 lambda 改成构建上下文结构 + 两个静态比较函数。
> - **排序是个必须刻意处理的点**：`std::sort` **不稳定**，且两套工具链用不同算法。若照搬不稳定
>   排序，在原比较器下打平的候选之间可能选出不同的牌——这是真实的跨工具链分歧。所以候选改用
>   **稳定插入排序 + 原比较器原样**（不发明新 tie-break，原来能定的现在也定得一样）；掩码用
>   `qsort`，因为每个掩码互不相同、序是全序。
> - 删掉两个**确实是死代码**的帮助函数：`CountMaskBits64`、`PossibleFollowCardCount`
>   （全树定义但从未被调用），没有照搬。
> - `StrongAiStrategy.cpp` 仍是 C++ 且仍按老形状书写，所以加了一段**明确标注的临时垫片**，
>   在 C API 之上重建 `Candidate`/`CountRanks`/`GenerateCandidates`/`DeduplicateCandidates`/
>   `PatternBaseScore`/`UnknownRankCount`/`KickerControlPenalty`。现在改这 ~20 个调用点、等
>   转 `.c` 时再改一遍是同一份工作做两次；垫片随该文件一起删。
> - 进度：`src/` 剩 **26 个 `.cpp`**，`check_c_only.py` 残留 205 处；
>   MinGW x64 exe **701,440 → 693,760 B**。
>
> **修订 16（S5a 完成记录，commit `36e27b6`）——graphics 层开工，D2D shim 首次真正被使用**：
> - `ProceduralTextures` 与 `WicImageLoader` → `.c`。**这是全树第一次真正在 C 里驱动 Direct2D**
>   ——`tests/shim_layout` 只证明了 vtable 布局，没证明"调用真的能用"；现在两套工具链
>   （含 MSVC）都编过并渲染正确，才算真的验证了 shim 存在的意义。
> - `ComPtr` 成员 → 结构自己持有的裸接口指针（懒创建，`ProceduralTextures_Reset` 释放）；
>   `D2D1::BitmapProperties`/`SizeU` 是 `<d2d1.h>` 的 C++ 帮助函数，改成直接填结构体；
>   `std::vector<float>` 临时缓冲 → 定长数组（阴影掩码恰好 96²、绒面 128²、高斯核
>   `2*ceil(3σ)+1 = 61`，都是编译期常量），**每次构建纹理少 3 次堆分配**；
>   `std::span` → 指针 + 长度；返回 ComPtr → 返回持有**新引用**的裸指针。
> - WIC 需要 3 处 C++ 隐式转换改成显式：`IWICStream`→`IStream`、
>   `IWICBitmapFrameDecode`→`IWICBitmapSource`、`REFWICPixelFormatGUID` 在 C 里是指针。
> - 新增 `graphics/CppCompat.h`（临时，路线 B）：在 C API 之上复现
>   `graphics::LoadBitmapFromMemory` 返回 ComPtr、以及 `textures_.Shadow(target)` 形状；
>   包装本身只是类型转换（PDK_* 镜像与 SDK 接口布局一致，shim_layout 编译期已断言）。
> - **本轮确立的一条结构性规则（要记住）**：**D2D shim 不能被 C++ 翻译单元会包含的头间接引入**。
>   `d2d_c.h` 会拉进 `dwrite_c.h`，其 vendored `DWRITE_*` 类型与 C++ 侧 `<d2d1.h>` 带进来的
>   真 `<dwrite.h>` 冲突（`multiple definition of 'enum DWRITE_FACTORY_TYPE'`）。所以这两个头
>   只**前向声明** `PDK_*` 镜像，由 `.c` 去包含 shim。重复 typedef 在两门语言里都合法，零成本。
> - **验证经验（写给后续轮次）**：UI 截图**逐次运行并不可复现**——同一个 binary 跑两次得到
>   114,097 与 114,091 字节、SHA256 不同（场景里有计时与洗牌）。所以目标里"5 张截图与基线肉眼
>   无差异"才是正确的判据，**字节数漂移不能当作回归证据**。绒面桌布与圆角阴影与基线肉眼一致。
> - 进度：`src/` 剩 **24 个 `.cpp`**，`check_c_only.py` 残留 205 处；
>   MinGW x64 exe **693,760 → 692,736 B**；依赖表仍是 22 个系统 DLL。
>
> **修订 17（S7a + S5b 完成记录，commit `79d3c86`、`063c08c`）——IME 与渲染上下文已纯 C**：
> - `ImeInput` → `.c`：类只持 HWND + 使能位 + 上次 caret，无所有权；两个读取函数从
>   `std::wstring&` 改成 `WStr`，用 `WinFile` 那套 Reserve-再写 的模式。App 的 4 个调用点零改动。
> - **`D2DContext` → `.c`（这是 UI 层的枢纽，转完即解锁剩余 22 个文件）**：
>   5 个 `std::map` 缓存 + `std::list` LRU → 定长数组 + 使用戳（一个梯度键本来分散在三个 map
>   里共享一条 LRU 链，现在合并成一条带可空成员的表）；文本格式缓存同样有界，**C++ 版在满 64
>   条时是整个清空**，现在改成淘汰最久未用的，界一样但不会把热格式也扔掉；
>   `std::vector` 变换/透明度栈 → 定长数组 + 深度计数器（溢出忽略，和原来 `PopTransform`
>   拒绝弹掉单位矩阵一致）；`initializer_list`/`span` → 指针 + 数量；`std::wstring` → `WStr`。
>   `D2D1::*` 帮助函数全部改成直接填结构体，`Matrix3x2F::operator*` 按 `D2D1Matrix3x2FMultiply`
>   重写。`TextStyle` 的 DWRITE 字段在 C 侧是 `int`、FontFamily 是一对常量——**MSVC 的 C 模式
>   根本无法解析 `<dwrite.h>`**；C++ 侧的 DWRITE 类型与 `enum class FontFamily` 由
>   `graphics/CppCompat.h` 保留并在边界转换。
> - **新增 `graphics/D2DContext.c` 时才暴露的一类 shim 细节（值得记住）**：flat shim 镜像的是
>   **COM 签名**，所以几何参数是**指针**而 C++ 内联包装收的是引用（`SetTransform`/`Clear`/
>   `SetColor`/`Resize`/`PushAxisAlignedClip`/`FillRectangle`/`DrawRectangle`/`Fill|DrawRoundedRect`/
>   `Fill|DrawEllipse`/`DrawBitmap`）；`Draw*` 形式还要一个 stroke style；`EndDraw` 要两个 tag 出参；
>   `DrawText`/`DrawTextLayout` 要一个 options；几何 sink 是批量的 `AddLines`（**不是 `AddLine`**）；
>   brush 派生接口的 `SetOpacity` 第一个参数声明为 `PDK_ID2D1Brush *`；另外 C 需要
>   `&CLSID`/`&IID`/`&GUID_WICPixelFormat32bppPBGRA` 而 C++ 帮你隐式取址。
> - 生成器新增 `CLSID_WICImagingFactory` / `IID_IWICImagingFactory`（C 里没有 `__uuidof`），
>   因此 `iids.c` 不能再 include `<wincodec.h>`——和 WASAPI GUID 同一条"只定义一次"规则。
> - **清掉 VC-LTL 残留引用**：About 面板与开始页页脚还在写"C++ …… VC-LTL"和 VC-LTL 许可，
>   现在都是假的。**这两处字符串是唯一一处刻意偏离基线截图的内容改动**——布局/配色/阴影逐像素未变，
>   而为了迁就"像素一致"的判据去留一句假署名是更糟的选择。README 去掉 VC-LTL 条目、构建段落与
>   许可行，改为主发布工具链 MinGW UCRT x64 的说明。
> - 进度：`src/` 剩 **22 个 `.cpp`**，`check_c_only.py` 残留 202 处；
>   MinGW x64 exe **692,736 → 686,080 B**（距 679,424 还差 6.7 KB）。
>
> **修订 18（S5c + S5d 完成记录，commit `5f3f802`、`7fa1ebd`）——UI 层开始批量转**：
> - `Icons` → `.c`：`Icon` 枚举类 → `UI_ICON_*` 常量（**必须加 UI_ 前缀**，因为
>   `resources/IconAtlasData.h` 已经占用了 `ICON_*` 作为图标*图集*的顺序——两件不相关的事在
>   文件作用域撞名，编译器立刻抓到了）；`std::array` 顶点表 → 普通数组。
> - `Theme.h` → C 头：`constexpr D2D1_COLOR_F` 命名空间常量 → `static const` 结构 +
>   `PDK_RGBA` 花括号宏（仍是编译期常量表达式），名字加 `THEME_` 前缀。
>   **关键在于零改动迁移**：`ui/CppCompat.h` 把调色板以 `inline const D2D1_COLOR_F theme::Gold`
>   等形式重新导出（外加两个半径的 `inline constexpr float`），于是全树 **230 处 `theme::`**
>   一处都不用改；C 文件直接用 `THEME_*`。
>   `Text`/`Centered`/`Kai` 搬进门面——它们返回 `TextStyle`，而 C++ 侧那个是 DWRITE 类型的门面
>   结构（C 侧是 `int`，因为 MSVC 的 C 模式解析不了 `<dwrite.h>`）。
> - 顺带给 `RenderContext` 门面加了 `Native()` 访问器：任何"门面转 C 函数"的桥都需要它，
>   UI 层剩下的每一次转换都要用。
> - **Theme 是剩下 20 个 UI 文件共同的依赖**，所以这一步不只是整理，而是 CardView / Widgets /
>   Inputs / overlays / scenes 的前置条件。
> - 进度：`src/` 剩 **21 个 `.cpp`**，`check_c_only.py` 残留 200 处；exe 仍是 686,080 B
>   （Icons/Theme 没有引入新的 C++ 运行库依赖被移除，libstdc++ 仍被其它 TU 链着，
>   所以这一步没有体积收益——体积要等 `-static-libstdc++` 摘掉才会跳）。
>
> **修订 19（S5e 完成记录，commit `ab0eaf0`）——卡牌渲染已纯 C**：
> - `SpriteAtlas.h` → C 头且**仍是 header-only**：原来是一个最多三级的 `std::vector` 加几个
>   小访问器，现在是定长数组 + `static inline`，顺带把图集路径上最后一次堆分配也去掉了。
>   `ComPtr` 成员 → 结构自己持有的裸接口指针（`SetBitmap`/`AddLevel` 接管引用，`Reset` 释放
>   被替换的；**"层数已满"时释放传入引用而不是泄漏它**）。门面用 `ComPtr::Detach()` 完成移交，
>   正好等价于原来按值 `ComPtr` 参数的移动语义。
> - `CardView` → `.c`：模板 + lambda 的 `DrawCardCommon` 改成函数指针 + 用户指针，共享的
>   抬升/旋转/发光/悬停/选中处理只留一份，不必为牌面与牌背复制。
> - `CardLook` 用上惯例的 `#ifdef __cplusplus` 默认构造，且 **`CardLook_Init` 是默认值的唯一
>   来源**（构造函数调用它），C 侧与 C++ 侧不会漂移；C 用 `CardLook_Default()` 取默认值。
> - 图集查找、半纹素内缩、图集层级选择、fallback 牌面的 DWRITE 样式都没有改动。
> - 进度：`src/` 剩 **20 个 `.cpp`**，`check_c_only.py` 残留 194 处；
>   MinGW x64 exe **686,080 → 683,520 B**（距 679,424 只差 **4.1 KB**）。
>
> **修订 20（S5f 完成记录，commit `2beca01`）——控件库已纯 C，体积逼近目标**：
> - `Widgets` → `.c`：按钮/面板/胶囊/头像/印章/房间背景/模态动画全部纯 C。
>   `std::string text` → Button 上定长缓冲、绘制函数收调用方 C 字符串；
>   `ButtonStyle`/`Anchor` → `uint8_t` + `UI_BUTTON_*`/`UI_ANCHOR_*`（C 才能 switch）。
> - **本轮最关键的是聚合初始化**：覆盖层按位置构造按钮
>   （`{{rect}, "退出游戏", ui::ButtonStyle::Danger}`），C++ 里被省略的成员会取花括号初值
>   （`fontSize{19.0f}`、`visible{true}`、`visibleT{1.0f}`）。给 Button 加构造函数会让它变成
>   非聚合、11 处全崩；但留成裸 C 结构又会把 `visible` 静默清零——**按钮直接不可见**。
>   所以 Button **故意没有构造函数**，那 11 处改调 `ui::MakeButton(rect, text, style)`，
>   由 `Button_Init` 提供同一套默认值。`PanelStyle`/`ChipStyle` 没有任何按位置聚合初始化的
>   地方，所以照惯例用 `#ifdef __cplusplus` 构造函数调用各自的 `*_Init`（默认值唯一来源）。
> - 门面保持所有调用点不变：`ButtonGroup` 是对 `(Button*, count)` 的门面结构，
>   `DrawPanel`/`DrawChip`/`DrawHairline`/`DrawOrnamentCorners`/`DrawRadialGlow` 承载 C 没有的
>   默认实参，`AvatarLabel` 仍返回 `std::string`。GameScene 里直接调用的
>   `button.Update/Draw/HitTest/UpdateHover` 改成 C 函数。
> - **自己踩的坑（值得记住）**：头注释里写了 `UI_BUTTON_*/UI_ANCHOR_*`，那个 `*/` **提前闭合了
>   块注释**，把后面的说明文字变成了代码——569 个编译错误。描述通配符时要把斜杠分开写。
> - 进度：`src/` 剩 **19 个 `.cpp`**，`check_c_only.py` 残留 190 处；
>   **MinGW x64 exe 683,520 → 680,960 B，距 679,424 只差 1,536 B**。
>   S8 摘掉 `-static-libstdc++`/`-static-libgcc`（等最后一个 C++ 文件消失）的收益远大于这 1.5 KB。
>
> **S7 设计与 S8 清单（勘察结论，供后续轮次直接执行）**
>
> **S7 的 C 接口形状**（已从 8 个覆盖层 + 5 个场景的重写集合反推确定）：
> - `OverlayVtbl` = C++ 接口的 11 个虚函数（Update / Render / BlocksInputBelow / OnMouseMove /
>   OnMouseDown / OnMouseUp / OnKeyDown / OnText / WantsTextInput / OnImeComposition /
>   TextCaretRect）+ **`Expired`** + `Destroy`。
> - `SceneVtbl` = 9 个虚函数（OnEnter / OnExit / Update / Render / OnMouseMove / OnMouseDown /
>   OnMouseUp / OnD2DResourcesLost / OnD2DResourcesRecreated）+ **`RestartRound`** + `Destroy`。
> - 那两个新增槽位不是装饰：它们**消掉 `App.cpp` 里仅有的两处 `dynamic_cast`**（C vtable 没有
>   RTTI）。`App::Update` 现在用 `dynamic_cast<overlays::InvalidMoveToast*>` /
>   `<overlays::TalkBubbleOverlay*>` 判断过期 → 改成查 `Expired` 槽（NULL = 永不过期）；
>   `App::RestartCurrentGame` 用 `dynamic_cast<scenes::GameScene*>` → 改成 `RestartRound()` 虚函数
>   （基类默认 false）。**改完设计比现在更干净**，不是权宜之计。
> - 容器：`App::overlays_`（`std::vector<std::unique_ptr<Overlay>>`）→ 定长数组 + count + 拥有权；
>   `SceneManager`（`std::unique_ptr<Scene>`）→ `{ Scene scene; bool has; }`。
> - 12 个尚未转换的 C++ 场景/覆盖层用 `core/CppCompat.h` 的 `SceneClass`/`OverlayClass` 桥接
>   （与 `AiStrategyClass`、`ExternalAiControllerClass` 同一套手法），所以它们能原样编译；
>   转换某个文件时把它的基类从 `core::Scene` 换成 C vtable 即可。
>
> **各覆盖层状态映射**（`buttons_` 最多 4 个 → 定长数组；`text_` → 定长缓冲）：
> | 文件 | 重写 | 状态 |
> |---|---|---|
> | InvalidMoveToast(32) | Update/Render/BlocksInputBelow(false)/**Expired(>2s)** | text, elapsed |
> | TalkBubbleOverlay(51) | 同上 | text, elapsed |
> | TipOverlay(36) | Update/Render/BlocksInputBelow(false)/OnMouseDown | app&, text, elapsed |
> | AboutOverlay(59) / ConfirmExitDialog(42) / ReturnToMenuOverlay(42) / RoundResultOverlay(172) | Update/Render/BlocksInputBelow(true)/OnMouseMove/OnMouseDown | app&, buttons, elapsed |
> | SettingsOverlay(212) | 上面 5 个 + OnMouseUp/OnKeyDown/OnText/WantsTextInput/OnImeComposition/TextCaretRect | app&, originalVolume, buttons, elapsed, + 5 个控件成员 |
>
> **各场景状态映射**：
> | 文件 | 重写 | 状态 |
> |---|---|---|
> | LoadingScene(47) | OnEnter/Update/Render | app&, elapsed, progress, shownProgress, item(串), loaded |
> | StartScene(152) | + OnMouseMove/OnMouseDown | app&, buttons, welcome(串), elapsed |
> | StatsScene(134) | + OnMouseMove/OnMouseDown | app&, buttons, elapsed |
> | HelpScene(113) | + OnMouseMove/OnMouseDown, `DrawBullets(string_view)` | app&, buttons, elapsed |
> | GameScene(824) | + OnMouseUp | app& + buttons/dragPath/handLift/handHover 四个 vector + 约 20 个旗标/浮点 + toastText(串) |
>
> **S8 清单（已勘察到具体位置）**：
> 1. CI **已经是 8 组合**（6 MSVC + 2 MinGW），并且已经跑 `gen_com_shim.py --check` ✓ 无需改动。
> 2. `tools/check_c_only.py` **本来就在不合格时 `exit 1`** → CI 接入只需加一个 step，**不用改代码**。
> 3. 摘掉 `-static-libstdc++ -static-libgcc`：`CMakeLists.txt` **第 268 行**（app 目标的 foreach）
>    与 **第 315 行**（`unit_tests`）。注意 `unit_tests` 仍是 C++/doctest，**这两行删掉后
>    `unit_tests` 仍需要它们**——只有 `pao_de_kuai`/`scene_viewer` 该摘。
> 4. 删掉 9 个临时门面：`src/{app,audio,core,game,graphics,resources,rules,stats,ui}/CppCompat.h`。
> 5. 更新 `README.md`（已改过构建段落）与 `AGENTS.md`（本轮已改两处失效描述）。
> 6. 删除 `src/rules/Card.h` 里的 `#ifdef __cplusplus` 成员垫片（`Cards` 的 vector 形状 API），
>    以及从 `tools/check_c_only.py` 的白名单里移除它。
> 7. 量最终体积并比对 5 张基线截图。
>
> **修订 21（S5g 完成记录，commit `d7e5f07`）——`ui/` 已全部纯 C，体积目标达成**：
> - 四个控件（`TextField`/`Slider`/`Segmented`/`Toggle`）→ C 结构 + `*_Init` + 函数；
>   唯一消费者 `SettingsOverlay` 的约 33 处方法调用改成 C 调用。
> - `core/KeyEvent.h`（新，C）：`KeyEvent` 从 `core/Overlay.h` 拆出——因为 `ui/Inputs.h` 现在是 C 头，
>   文本编辑器要用同一个结构；`core/Overlay.h` 仍是 C++（`Overlay` 是虚类），用 `using` 把全局
>   类型重新导出为 `pdk::core::KeyEvent`。
> - `TextField` 的 `std::wstring` → `WStr`，`ComPtr<IDWriteTextLayout>` → 结构自己持有的裸指针
>   （`PDK_RELEASE` 释放），`private` 状态在 C 里变成普通字段（标注为 internal）。
> - **`Segmented` 的选项必须自己持有（拷贝）**：`SettingsOverlay` 是从一个**局部**
>   `std::vector<std::string>` 赋值的，C++ 版把这些字符串拷进了控件。存指针会在构造函数返回后
>   悬空、分段控件渲染出乱码。
> - 四个结构体都有 `#ifdef __cplusplus` 构造函数调用各自的 `*_Init`（默认值唯一来源），
>   否则 `TextField field;` 的状态是不定的。
> - **里程碑：MinGW x64 `pao-de-kuai.exe` = 678,400 B ≤ 679,424 B，体积目标达成**（余量 1,024 B）。
>   注意这**仍链着** `-static-libstdc++`/`-static-libgcc`（还剩 18 个 C++ 文件），
>   S8 摘掉后余量会大幅扩大。
> - 新增 `tools/verify_all.ps1`：三工具链 build+ctest 一键验收（自动处理"每个工具链自己的 bin 上
>   PATH"和"TEMP/TMP 指到构建目录"这两个手工最容易搞错的步骤）。本次提交就是用它验收的。
> - 进度：`src/` 剩 **18 个 `.cpp`**，`check_c_only.py` 残留 185 处。
>
> **修订 22（S7a 完成记录，commit `ba8d1fb`）——场景/覆盖层已是 C vtable，两处 `dynamic_cast` 消失**：
> - `core/{Scene.h,Overlay.h,SceneManager.h}` → 纯 C。`OverlayVtbl` = 原 11 个虚函数 + `Expired` +
>   `Destroy`；`SceneVtbl` = 原 9 个 + `RestartRound` + `Destroy`。可选槽可空、内联包装吸收判空，
>   实现只填自己需要的。**本提交没有把任何 `.cpp` 变成 C**——它是脚手架，让剩下 13 个
>   场景/覆盖层文件可以逐个独立转换、逐个独立可构建。
> - 两个新增槽位的价值就是**删掉 `App.cpp` 里仅有的两处 `dynamic_cast`**（C vtable 没有 RTTI）：
>   - `App::Update` 的每帧清扫原本用 `dynamic_cast<InvalidMoveToast>/<TalkBubbleOverlay>` 只为问一句
>     "你过期了吗"。现在问 vtable。而这两个类**本来就声明了非虚的 `bool Expired() const`**，
>     于是**一个字都不用改**就变成了重写——而且以后再加会过期的覆盖层，App 不用动。
>   - `App::RestartCurrentGame` 原本 `dynamic_cast<GameScene*>` 再 `StartNextRound()`。现在场景自己
>     通过 `Scene::RestartRound` 回答，基类默认 false，也就是"当前场景不是牌局"成了基类行为，
>     而不是一次恰好失败的转换。
> - `App` 的 `std::vector<std::unique_ptr<Overlay>>` → 定长数组（容量 8，最深是"对话框 + toast"），
>   移除时显式释放；`std::unique_ptr<Scene>` 管理器 → C 的 `SceneManager`（Change/Release 会触发
>   OnExit/OnEnter 并释放）。`App` 补了析构函数——原来这两个成员的释放是 C++ 隐式完成的。
> - `core/CppCompat.h` 新增 `SceneClass`/`OverlayClass`（12 个未转换实现仍继承的 C++ 抽象基类）与
>   `Transfer`（把 C++ 对象交给 C 侧，由 C 侧拥有并 delete）。13 个头文件的基类从
>   `core::Scene`/`core::Overlay` 改名为这两个，`Render` 收门面的 `graphics::RenderContext&`。
> - 为让上面这条成立，`graphics::RenderContext` 改成**视图**：持有 `::RenderContext*` + 所有权标志，
>   这样桥接层能把 vtable 递回来的 C context 包成一个**非拥有**的门面视图交给 C++ 实现。
>   原先是按值内嵌 C 结构，这种视图根本无法表达。拥有版本的默认构造对调用方没有变化，
>   `App::renderContext_` 仍然只有一个对象。
> - **体积如实说明**：MinGW x64 `pao-de-kuai.exe` 678,400 → **679,936 B**，即**超出目标 512 B**。
>   这一步**只可能**如此：它加了两层 vtable 和一层桥接，却**没有删掉任何 C++**。所以体积要靠后续
>   转换（13 个文件各自丢掉 class、RTTI 和 `std::string`）回收，最后由 S8 摘掉
>   `-static-libstdc++`/`-static-libgcc` 一举确定。
> - 进度：`src/` 仍剩 **18 个 `.cpp`**，`check_c_only.py` 残留 177 处。三链路 8/8 全绿，
>   `ui-result` 肉眼一致（它同时覆盖 `Scene_Render`、结算覆盖层和新增 `Expired` 槽的对话气泡，
>
> **修订 23（S7b 完成记录，commit `503cb54`）——6 个覆盖层转 C，体积回到目标内**：
> - `InvalidMoveToast` / `TalkBubbleOverlay` / `TipOverlay` / `ConfirmExitDialog` /
>   `ReturnToMenuOverlay` / `AboutOverlay` → `.c`，直接实现 `OverlayVtbl`。
>   `src/` 从 18 → **12 个 C++ 文件**；MinGW x64 exe 679,936 → **674,816 B**，
>   即 S7a 超出的 512 B 已回收，**还剩 4,608 B 余量**——正是那次提交预告的结果。
> - 每个覆盖层的状态结构私有于自己的 `.c`，头文件只暴露 `<Name>_New(...)`（分配 + 填 vtable +
>   返回拥有句柄），`Destroy` 槽负责释放。这正是 `core::Transfer(new X)` 做的事，App 的拥有权语义不变。
>   vtable 用 **C99 指定初始化器**——只写有意义的槽、其余为 NULL，比 13 个按位置的条目好审得多，
>   也正是内联包装能把"未实现"和"返回 false"当成同一件事的原因。
> - 需要驱动 App 的 3 个覆盖层要一条从 C 到 App 的路。它们只需要 4 个操作，于是
>   `app/AppApi.h` 是一个**极小的 `extern "C"` ABI**（`App_PlaySound`/`App_CloseTopOverlay`/
>   `App_ConfirmExit`/`App_ShowStart`，定义在 `App.cpp`），覆盖层持不透明的 `void *app`。
>   先转 App 意味着一次动三个文件；这样还能一次一个，头文件里也写明了 App 变 C 后它即删除。
> - **前置**：`scenes/GameLayout.h` 也是 C 了——它原本还是 `constexpr` +
>   `namespace pdk::scenes::layout`，而对话气泡需要座次板和头像几何。名字改为 `GameLayout_*`，
>   带一个记录在案的 `#ifdef __cplusplus` 块保住 `GameScene.cpp` 里 11 处 `layout::` 调用点。
>   **给后续转换的提醒**：`PLAYER_HUMAN`/`PLAYER_AI1`/`PLAYER_AI2` 在 `rules/Scoring.h`，
>   **不在** `rules/Card.h`——只包含后者会报未声明。
> - 新增共享助手，按类型归属放置而不是每个文件各写一份：`Point_Make`/`Rect_Make`
>   （`core/Geometry.h`，对应 C 无法移植地写出的花括号初始化）、`ColorF_Make`(`ui/Theme.h`)、
>   `GradientStop_Make`(`graphics/D2DContext.h`)、`ClampF`(`ui/Anim.h`)、
>   `TextStyle_Label`/`Centered`/`Kai`（按它们替代的 C++ 门面函数命名）、`Button_Make`(`ui/Widgets.h`)。
> - `AboutOverlay` 的署名段落原本是捕获累加 `y` 的 lambda；C 里改成显式 `CreditsCursor` 交给助手，
>   顺带让"累加值"不可能被第二个调用点写错。
> - `check_c_only.py` 残留 177 → **146** 处。
>
> **S7c 起的最优顺序（依赖分析结论，供后续轮次直接执行）**
>
> 关键依赖：**`GameScene` 重度依赖 `GameState`**（用到 27 个方法：`Update`/`Players`/`Events`/
> `HintIndices`/`PlaySelected`/`SelectBestPatternFromDraggedCards`/`StartNewRound`/`LastRoundRecord`…），
> 而 `GameState` 是 C++。所以要么先把 `GameState` 转 C（然后 GameScene 直接调 `GameState_*`），
> 要么给 GameState 开一条 27 函数的 `extern "C"` 边界——**前者明显更好**。
> 但**四个小场景不依赖 GameState**，可以先用 `AppApi.h` 边界拿下：
>
> | 场景 | 行数 | 需要的 `app_.` | 需要的 ABI 新增 |
> |---|---|---|---|
> | HelpScene | 113 | Audio, CardAtlas, LoadCardAtlas, ShowStart | `App_CardAtlas`(→`::SpriteAtlas*`), `App_LoadCardAtlas` |
> | StatsScene | 134 | Audio, Settings, ShowStart | `App_GetSettings`（已在计划内） |
> | StartScene | 154 | Audio, CardAtlas, LoadCardAtlas, PushOverlay, RequestClose, Settings, ShowHelp, ShowSettings, ShowStats, StartGame | `App_ShowHelp`/`App_ShowSettings`/`App_ShowStats`/`App_StartGame`/`App_RequestClose` |
> | LoadingScene | 47 | Audio, ChangeScene, LoadGameResources | `App_LoadGameResources`, `App_EnterGame`/`App_EnterStats`（C 文件不能 `new GameScene`） |
>
> 注意 `LoadingScene` 原本直接 `app_.ChangeScene(make_unique<GameScene>(app_))`——**C 文件无法构造场景类型**，
> 所以要在 ABI 里给 `App_EnterGame`（App.cpp 内 `ChangeScene(Transfer(new GameScene(*this)))`）。
>
> 推荐顺序：
> 1. `RoundResultOverlay` + `SettingsOverlay`（本轮子代理在做）→ 10 个 `.cpp`
> 2. `HelpScene` → `StatsScene` → `StartScene` → `LoadingScene`（配 `AppApi.h` 扩展）→ 6 个
> 3. **`GameState`(1388 行)**：最大的一块。`std::vector`×67、`std::string`×49、`std::optional`×20、
>    `std::sort`×7，47 个公开方法；先转它，GameScene 才好办
> 4. `GameScene`(826) → 5 个
> 5. `App`(334) / `Window`(158) / `WinMain`(19)：转完 App 后 `AppApi.h` 整条边界立即删除
> 6. `StrongAiStrategy`(1321)：`std::thread` 后台控制器，注意线程生命周期
>
>
> **修订 24（S7c 完成记录，commit `debe05c`）——覆盖层全部纯 C，并修掉一个潜伏崩溃**：
> - `RoundResultOverlay` / `SettingsOverlay` → `.c`，直接实现 `OverlayVtbl`。`src/` 从 12 →
>   **10 个 C++ 文件**；MinGW x64 exe 674,816 → **665,600 B**（**余量 13,824 B**，且仍链着
>   `-static-libstdc++`）。
> - 两者要跨 C 边界拿"活动设置"和"本局记录"：`AppApi.h` 增
>   `App_GetSettings`/`App_ApplySettings`/`App_SetMasterVolume`/`App_Hwnd`（App.cpp 里基于已有的
>   `ToCSettings`/`FromCSettings` 实现）；`RoundResultOverlay` 收 C 的 `RoundRecord`，
>   由 App.cpp 用 `ToCRound` 转换。覆盖层永远看不到 `std::string`。
>
> **一个潜伏崩溃，以及它是怎么进来的。**
> 第一次跑门禁时 `ui_overlay_settings` 和 `ui_overlay_result` 在**三套工具链上全部 SEGFAULT**。
> release exe 被 strip 过（第一次 gdb 只有地址），于是编了 debug preset 拿到符号栈：
> 崩在 `Widgets_DrawPanel`，调用点是覆盖层的 `Render` 传了 **NULL** 当 `PanelStyle`——
> 字面读起来很像"没有样式"。
> C API 无法承载 C++ 的默认实参 `style = {}`，所以正确的修法在 **API 层**而不是每个调用点：
> `Widgets_DrawPanel` 与 `Widgets_DrawChip` 现在把 NULL 当作"取默认值"，也就是门面里
> 默认构造的样式本来产生的东西。这一处改动同时修掉上述两处调用点。
> 两件事值得记下来而不是含糊过去：
>   * 同一个 NULL **已经在 `AboutOverlay.c` 里了，而那是我在 S7b 写的**。它在树里活了一整个提交，
>     在开始界面点"关于"就会崩。
>   * 它能活下来是因为**没有任何测试覆盖那个覆盖层**：8 个覆盖层里只有 5 个、6 个场景里只有 4 个
>     有测试，而 5 张基线截图恰好不含 About。所以这次把缺口补上：新增 **7 个 `add_test`** 覆盖
>     stats / help / loading / confirm-exit / about / return-menu / invalid，各自写到独立文件，
>     所以发版要比对的 5 张基线截图不受影响。测试从 **8 → 15 个**，三链路全绿，
>     而这类崩溃以后会在提交前就被抓住。
> - 另外做了 **13 路探针**：`scene_viewer` 跑遍 start/stats/settings/help/loading/game 与
>   confirm-exit/about/tip/invalid/talk/return-menu/result-win，全部 exit 0；
>   `ui-settings` 与 `ui-result` 肉眼一致。
> - `check_c_only.py` 残留 146 → **137** 处。
> 7. S8 收尾
>   是验证新分派最好的单张图）。
>
>

>
> **修订 2 的直接后果（务必先读）**：既然 MSVC 要继续编译同一个 `src/`，那么 **MSVC C 模式缺 D2D vtable、且完全不能 include `dwrite.h`** 这个问题就重新生效。因此 §3 的两个 ABI shim **必须保留**——但改写成"**双工具链共用一份可移植 shim**"，而不是修订 1 里只服务于 MinGW 的那套宏。MinGW 侧的价值随之改变：它的 C 模式 vtable **成了 shim 布局的机器可核对来源**（§3.3），这是本次修订最实质的技术收益。

## 0. 可行性结论：两套编译器都要过，难点回到 shim，但有机器核对

**实测环境**
- MinGW x64（主发布）：`D:\_\3rd\mingw64-ucrt`，MinGW-W64-builds 5.0.0 / **gcc 16.1.0**（`x86_64-win32-seh-rev1`）/ binutils / `--with-default-msvcrt=ucrt` / `--threads=win32` / `--exceptions=seh` / `--arch=x86_64`。探测命令 `gcc -std=c17 -c`。
- MinGW x86：`D:\_\3rd\mingw32-ucrt`，MinGW-W64-builds 5.0.0 / **gcc 16.2.0**（`i686-win32-dwarf-rev1`）/ `--with-default-msvcrt=ucrt` / `--threads=win32` / `--exceptions=dwarf` / `--arch=i686`。已核对 `windres.exe`/`ar.exe`/`strip.exe`/`objdump.exe`，以及 `i686-w64-mingw32\lib` 下 §2 链接列表用到的全部 import lib（`libd2d1`、`libdwrite`、`libwindowscodecs`、`libole32`、`libuuid`、`libshlwapi`、`libimm32`、`libavrt`、`libmfplat`、`libmfreadwrite`、`libmfuuid`、`libgdi32`、`libuser32`、`libdwmapi`、`libucrt*`）均在位。
- MSVC（六个 CI 组合）：VS2026 Enterprise + SDK 10.0.26100，`cl /TC`（初版已实测，结论见下表）。

> **x86 的 `dwarf` 例外模型**：i686 没有 SEH，只有 dwarf/sjlj。纯 C 的 `src/` 不受影响（不用异常），但 **C++ 的 `tests`/`scene_viewer` 会用 dwarf 展开**——功能正常，只是异常路径慢一些；**不允许**因此产生 `libgcc_s_dw2-1.dll` 依赖，测试目标继续 `-static-libgcc -static-libstdc++`。

| SDK API | MinGW C 模式 | MSVC C 模式 | 双工具链处理 |
|---|---|---|---|
| Direct2D | ✅ 头文件自带 C vtable（`d2d1.h` L500 起 `typedef struct ID2D1ResourceVtbl`，全文 568 处 `lpVtbl`，另有 `ID2D1Factory_CreateHwndRenderTarget` 一类宏） | ⚠️ 只给前向声明 `typedef interface ID2D1Factory ID2D1Factory;`（L3571-3764），但**已提供**全部 `D2D1_*` 结构体/枚举、`D2D1CreateFactory`、`EXTERN_C CONST IID IID_ID2D1*` | **不用任何一方的 vtable**，自建一份平铺 vtable 头（§3.1），两边都 include `<d2d1.h>` 取 `D2D1_*` 类型 |
| DirectWrite | ✅ 头文件自带 C vtable（95 处 `__cplusplus` 分支、619 处 `lpVtbl`、27 处 `COBJMACROS`） | ❌ **完全不能 include**（`dwrite.h:4704` 的 `interface X : public IUnknown` → C2059） | 靠 `#ifdef` 分流：MSVC 用我们自建的枚举/结构体/原型；MinGW 直接 include `<dwrite.h>`（§3.2） |
| WIC | ✅ | ✅（`#define COBJMACROS` + `wincodec.h`） | 两边都用 SDK 自己的 C 宏 |
| WASAPI | ✅（`ac->lpVtbl->GetService(...)` 通过） | ✅（`e->lpVtbl->Release(e)` 可用） | 同上 |
| Media Foundation | ✅（`mfapi.h`→`mfidl.h`→`mfreadwrite.h` 顺序） | ✅（同顺序；顺序错了会假失败） | 同上 |
| IMM32 / avrt / dwmapi / ole2 | ✅ | ✅（`imm.h` 需在 `windows.h` 之后） | 同上 |
| shlwapi | ⚠️ **C 模式头部缺陷**：`shlwapi.h:981` 起 `IQueryAssociations` 未声明 | ✅ | 两边都**不 include `<shlwapi.h>`**，只手写所需 Path 函数声明（S0 统计使用点后决定数量），统一放在 `win_compat.h` |

**两套 vtable 布局的差异（关键，已实测）**：MinGW 的 C vtable 是**基类嵌套**（d2d1.h）或**平铺**（dwrite.h 用 `BEGIN_INTERFACE` 把 `IUnknown` 三槽内联），MSVC 的 C++ vtable 是平铺——

```c
/* d2d1.h 真实形态：ID2D1SolidColorBrush : ID2D1Brush : ID2D1Resource : IUnknown */
typedef struct ID2D1ResourceVtbl { IUnknownVtbl Base; /* GetFactory */ } ID2D1ResourceVtbl;
typedef struct ID2D1BrushVtbl    { ID2D1ResourceVtbl Base; /* SetOpacity/SetTransform/GetOpacity/GetTransform */ } ID2D1BrushVtbl;
typedef struct ID2D1SolidColorBrushVtbl { ID2D1BrushVtbl Base; /* SetColor/GetColor */ } ID2D1SolidColorBrushVtbl;
#define ID2D1SolidColorBrush_GetFactory(this,A) (this)->lpVtbl->Base.Base.GetFactory((ID2D1Resource*)(this),A)

/* dwrite.h 真实形态：IDWriteFactory : IUnknown，三槽内联（平铺） */
typedef struct IDWriteFactoryVtbl { BEGIN_INTERFACE /* = QueryInterface/AddRef/Release */ ... } IDWriteFactoryVtbl;
```

`Base` 是首成员，所以**内存布局与平铺（IUnknown 三槽 + 基类方法 + 派生方法）完全一致**——这就是能用"一份平铺 shim 同时服务两套编译器"的依据。**已用编译器验证**：为 7 个接口手写平铺/嵌套 PDK 结构后，`_Static_assert(sizeof(PDK_X) == sizeof(XVtbl))` 与逐方法 `offsetof` 断言全部通过（见 §3.3、附录 C）。

> **继承链不是想当然的**：`ID2D1Factory : public IUnknown`（**没有** `GetFactory`！），直接继承 `IUnknown` 的还有 `ID2D1GdiInteropRenderTarget`、`ID2D1SimplifiedGeometrySink`、`ID2D1TessellationSink`。我一开始按"Factory 继承 Resource"写过一版 shim，静态断言立刻把 `CreateTextFormat`/方法偏移查了出来——**这正是该机制的价值**。

**原"环境限制"已解除（本次实测）**：之前 `as.exe` 静默失败与文件沙箱无关。根因是 **gcc 默认调用的 `x86_64-w64-mingw32\bin\as.exe` 依赖 `libwinpthread-1.dll`，而该 DLL 只在工具链的 `bin\` 下**；PATH 不含 `bin` 时 `as.exe` 以 `0xC0000135`（STATUS_DLL_NOT_FOUND）退出，**gcc 不打印任何诊断**。把 `<root>\bin` 前置进 PATH 后 x64/x86 都正常链接。**这是必须在 CI 里显式保证的前置条件**（§5）。

**已完成的最小实测（x64 + x86 双架构）**

| 项目 | 结果 |
|---|---|
| 空 `WinMain` 程序 | `-Os -s -mwindows` → x64 **15,872 B**、x86 **15,872 B**；加 `-ffunction-sections -fdata-sections -Wl,--gc-sections` → x64 **15,360 B**；再加 `-flto` **无进一步收益**；不加 `-s` 为 55,612 / 52,510 B |
| 依赖 DLL（MinGW 产物） | 只有 `KERNEL32.dll` + `api-ms-win-crt-{environment,heap,locale,math,private,runtime,stdio,string}-l1-1-0.dll`（UCRT API set，Win10 内置）。**无 `msvcrt.dll`、无 `vcruntime140.dll`、无 `libgcc_s_*`/`libwinpthread-1`** |
| 全 SDK 面链接 | D2D + DWrite + WIC + WASAPI + MF + IMM32 + AVRT + dwmapi 一次性链接通过（37,888 B 的最小程序）；**`#define INITGUID` 必需，`-luuid` 不够** |
| `windres` | 真实 `pao-de-kuai.ico`（ICON）+ `poker-cards.png`（RCDATA）+ UTF-8 中文注释 → `.res.o` 85,858 B，链接出 101,888 B 的 exe |
| 布局静态断言 | 7 个接口（ID2D1SolidColorBrush / HwndRenderTarget / PathGeometry / GeometrySink / IDWriteFactory / TextFormat / TextLayout）sizeof 与逐方法 offset 全部一致 |

**结论**：CRT 底噪只有约 15 KB，679,424 字节的基线**绝大部分是应用代码**，所以"UCRT 路线能不能更小"主要取决于 `src/` 的编译产物与 shim 代码量，而不是 CRT 选择——这降低了 §9 的体积风险等级。难点仍集中在 §3 的 shim（13 个 D2D vtable + 4 个 DWrite vtable + DWrite 类型回落），但**核对手段已从"手抄 learn.microsoft.com"升级为"与 MinGW 的 C vtable 做编译期机器断言"**；剩下的工作量是 `src/` 11,671 行的 C++ 惯用法（`std::string`×330、`std::vector`×176、`std::array`×58、`std::wstring`×40、`std::optional`×37、`map/set`×20+、`unique_ptr/shared_ptr`×29、`D2D1::` 辅助类型×50、`virtual/override`×116、`ComPtr` 模板、`std::span`、`std::function`、`charconv`）以及"删 VC-LTL + 双工具链构建"这套工程改动。

## 1. 目标与验收标准

**完成定义（Definition of Done）**
1. `src/**` 全部为 `.c`/`.h`；`grep` 确认无 `class`、`namespace`、`template<`、`std::`、`virtual`、`#include <string|vector|array|optional|map|set|memory|span|function|charconv|chrono|mutex|thread|atomic|algorithm>`。
2. **同一份 `src/` 同时满足 `cl /std:c17` 与 `gcc -std=c17`**；不得使用任一方的专属扩展（GCC 的 `__attribute__`、MSVC 的 `__declspec` 之外的东西都算），所有工具链分歧集中在 `src/graphics/win_compat.h` / `src/core/win_compat.h`。
3. `pao_de_kuai.exe` 的所有 TU 均为 C → 链接产物不含 C++ 运行时（无 `libcpmt`、无 `/EHsc`、无 C++ EH 表；MinGW 侧无 `libstdc++-6.dll`、无 `libgcc_s_*.dll` 动态依赖）。
4. **VC-LTL 完全消失**：`external/vc-ltl/` 删除；`PDK_USE_VC_LTL`、`pdk_vc_ltl_build`、`vc-ltl-build/` 全部移除；`README.md`、`AGENTS.md`、CI、`src/overlays/AboutOverlay.cpp`、`src/scenes/StartScene.cpp` 中的 VC-LTL 署名/许可文案同步删除。（**`PDK_MSVC_RUNTIME=MD|MT` 保留**，它服务于 MSVC 组合。）
5. CI **8 个组合全绿**：MSVC `x64/x86/arm64 × /MD` + `x64/x86/arm64 × /MT`（6 个，行为不变）**保持绿**，两个原 VC-LTL 组合换成 **MinGW UCRT x64**（主发布）与 **MinGW UCRT x86**。
6. **主发布 = MinGW UCRT x64**，`pao_de_kuai.exe` 依赖表只含系统 DLL：`ucrtbase.dll`/`api-ms-win-crt-*`、`KERNEL32`、`USER32`、`GDI32`、`d2d1`、`DWrite`、`windowscodecs`、`ole32`、`SHLWAPI`、`IMM32`、`AVRT`、`MFPlat`、`MFReadWrite`、`dwmapi`；**不得出现 `msvcrt.dll`、`vcruntime140.dll`、任何随包 DLL**。
7. 6 个 ctest 目标在 **MSVC x64 与 MinGW x64 两边**全绿：`unit_tests`、`ui_scene_start`、`ui_overlay_settings`、`ui_scene_game_deal`、`ui_scene_game_play`、`ui_overlay_result`。
8. 体积：MinGW UCRT x64 的 `pao_de_kuai.exe` **目标 ≤ 679,424 字节**（原 MSVC+VC-LTL 基线，见附录 A）。修订 4 实测 CRT 底噪仅约 15 KB，因此该目标主要取决于 `src/` 的编译产物与 shim 代码量；若达不到，底线是"明显小于同配置 MSVC `/MT`"，并把实测数字报你定夺。
9. 5 张 UI 截图与基线**逐张肉眼比对无可见差异**（UI 测试不做像素比对，人工比对是唯一有效回归信号）；**MSVC x64 与 MinGW x64 各出 5 张交叉比对**（MinGW x86 由 CI 的 ctest 覆盖，不做人工图像比对）。
10. 最低系统为 **Win10**（`WINVER=0x0A00`、`_WIN32_WINNT=0x0A00`）；`IAudioClient3` 低延迟路径可改为无条件使用。
11. `AGENTS.md` / `README.md` / `CHANGELOG.md` 同步更新（双工具链、最低 Win10、C 约束、shim 说明）。

**行为不变式（绝对不许变）**：固定游戏规则、计分规则（含春天/炸弹）、AI 策略强度、UI 视觉与交互、`appsettings.json` schema、`stat/yyyyMMdd.json` 格式、`.rc` 资源嵌入方式、`D2DERR_RECREATE_TARGET` 恢复语义。

**显式变更（经你确认）**：Win8 支持取消 → 最低 Win10；主发布 CRT 由 `msvcrt.dll`(VC-LTL) 改为 `ucrtbase.dll`；MSVC 组合不再使用 VC-LTL。

## 2. 关键设计决策（C 运行时层，Stage 0 定稿后全局冻结）

| C++ 惯用法 | C 替代方案 |
|---|---|
| `std::vector<Card>` / `rules::Cards` | `typedef struct { Card items[48]; int count; } Cards;` 定长值类型。全项目最大集合是牌堆 48、手牌 ≤16、飞机带牌≪48。**零堆分配、天然可拷贝**，规则层与 AI 搜索因此更快 |
| 传参 `const Cards&` | 传 `const Cards*`（避免 96 字节按值拷贝）；返回用值类型 |
| `std::string`（UI 文本、日志名、提示词） | `src/core/Str.h`：`Str { char* data; int len; int cap; }` + `Str_Append`/`Str_AppendNumber`/`Str_Reset`，malloc 支撑、可增长。JSON 与文件名共用。继续遵守"不用流式格式化"的既有约束 |
| 固定短文本（如 `AiMoveChoice.reason`、toast） | `char buf[128]` 固定缓冲（走现有 `core::AppendNumber` 的 C 版），避免小字符串堆分配 |
| `std::map<Rank,int>`（AiStrategy 计数） | 定长数组按点数索引（3..15 → 13 项）；`CoreUsage` 同 |
| `std::set<int>`（选中/推荐索引） | 手牌 ≤16 → `uint64_t` 位掩码，天然去重（GameState.cpp:598/643/706/734 直接改位运算） |
| `std::set<std::string>`（AiStrategy.cpp:549/578） | 小数组 + 线性查找（元素数 < 10） |
| `std::map<uint64_t, TextFormatEntry>` + `std::list` LRU（D2DContext 字体缓存） | 定长 64 项开放寻址表 + 环形淘汰序号（去掉 `std::list` 迭代器） |
| `std::array<T,N>` | 原生 `T x[N]` |
| `std::optional<T>` | `struct { T value; bool has; }` + `X_Set`/`X_Clear`（`lastPattern`、`nextRoundLeader`、`PassObservation`） |
| `std::unique_ptr` / `shared_ptr` | `malloc`/`free` + 显式 `Init`/`Destroy`；需要共享的（`ExternalAiController`、`LocalAiController` 状态）用手写引用计数 `long refs` + `Retain`/`Release` |
| `ComPtr<T>` 模板 | `src/graphics/Com.h`：`PDK_RELEASE(p)` 统一走 §3.1 的 shim 宏（**两套工具链同一份实现**，绝不出现 `Base.Base` 或 `__uuidof`） |
| `class Scene`/`Overlay`/`AiStrategy` + `virtual` | 手写 vtable 模式：`typedef struct SceneVtbl { ... } SceneVtbl; struct Scene { const SceneVtbl* vtbl; void* user; };` 每个实现填一张 `static const SceneVtbl`。**这是 COM 的同一个套路**，全项目统一用，别发明第二套 |
| `std::function`（Tween.h:24/25/37/38） | 函数指针 + `void* user` |
| `std::span<const Point>`（D2DContext/AudioDecoder） | `(const Point* p, int n)` |
| `std::initializer_list<GradientStop>`（D2DContext:255/297/323） | `(const GradientStop* stops, int n)` |
| `auto operator<=>(Card)` | `int Card_Compare(Card, Card)` |
| `enum class` | 普通 `enum` + 类型前缀命名（`PDK_SUIT_SPADES`） |
| `std::thread`/`mutex`/`atomic` | `CreateThread`/`WaitForSingleObject`/`CloseHandle`；`CRITICAL_SECTION`；`volatile LONG` + `InterlockedIncrement/Exchange/CompareExchange`。`std::atomic<float> masterVolume` → `LONG` 存 bit pattern + `InterlockedExchange`。MinGW 侧是 **win32 线程模型**，不要引入 winpthreads |
| `<chrono>` | `QueryPerformanceCounter` 封装（`pdk_core` 的 `Timer`/`Tween`） |
| `<charconv>`（StringUtil） | 手写 `int`→ASCII |
| `Utf8ToWide`/`WideToUtf8` | `MultiByteToWideChar`/`WideCharToMultiByte`（原有实现本身就是这个，只是包在 `std::wstring` 里） |
| `__uuidof(X)` / `IID_PPV_ARGS` | 两套 C 模式下都不可用 → 统一用 `IID_X` 常量 + 显式 `void**`；IID 符号来源在 `win_compat.h` 里定死（§3.4） |
| `std::lround` 类函数 | 继续只用 `ui::RoundToInt` 的 C 版。**理由已变**：UCRT 里 `lround` 是存在的（原"msvcrt.dll 不导出"的强制理由消失），但"统一取整实现、避免额外依赖"仍作为风格与体积规则保留 |
| 异常/RAII 清理 | 无异常；统一 `goto cleanup` 单出口或 `Retain/Release`；每个分配都有对应释放点 |

**C 语言子集约定（新增，因为要同时过两套编译器）**
- 语言级：C17 的**公共子集**——不用 VLA、不用嵌套函数、不用 `__attribute__`、不用 MSVC 的 `__declspec` 以外的扩展、不用 `_Generic` 里的编译器特例；`_Static_assert` 可用（两边都支持）。
- **GCC 16 把隐式函数声明当错误**（实测：漏原型直接 `error:`，即使没加 `-Werror`）。这是近年 GCC 的趋势，但**CI 走 latest，所以每次上游更新后都要以实际行为为准**；好处是"忘写原型"会在编译期暴露，代价是每个被调用的 SDK/自有函数都必须有可见原型。
- 字符集：源文件 UTF-8；MSVC 加 `/utf-8`，GCC 加 `-finput-charset=UTF-8`。
- 资源：`.rc` 由 `rc.exe`（MSVC）或 `windres`（MinGW）编译，`CMakeLists.txt` 里显式设置 `CMAKE_RC_COMPILER`（Ninja + MinGW 时 CMake 默认找不到 `windres`）。**已实测**：`windres` 能吃真实 `pao-de-kuai.ico`(ICON) + `poker-cards.png`(RCDATA) + UTF-8 中文注释。
- 窗口程序：`add_executable(... WIN32)` → MSVC `/SUBSYSTEM:WINDOWS`、MinGW `-mwindows`；若改用 `wWinMain`，MinGW 需 `-municode`。
- **PATH 前置条件（实测踩过）**：MinGW 构建时 `<root>\bin` **必须**在 PATH 里，否则 gcc 默认调用的 `<root>\<triple>\bin\as.exe` 找不到 `libwinpthread-1.dll`，以 `0xC0000135` 退出而 **gcc 不报任何错**。CMake 配置前加一条 gcc 链接冒烟检查。
- 链库名两套一致：`d2d1 dwrite windowscodecs ole32 uuid shlwapi imm32 avrt mfplat mfreadwrite mfuuid gdi32 user32 dwmapi`。**注意**：这些库里没有 `IID_*`/`CLSID_*` 定义（实测 `-luuid` 也补不齐），GUID 一律走 §3.4 的 `INITGUID` 方案；`uuid` 只是保持与 MSVC 侧一致，若实测无用可在 S8 去掉。
- 体积（MinGW 主发布）：`-Os -ffunction-sections -fdata-sections -Wl,--gc-sections -s`。**实测 `-flto` 在最小程序上没有任何收益**（15,360 B 不变），因此默认不开，只在 S8 用真实 exe 复测一次；MSVC 侧保留 `/O2 /Ob2 /Os`。
- 资源清理约定：不引入 GC，不引入 `setjmp`。

## 3. 双工具链可移植 shim（本项目唯一的技术难点）

**设计原则**：业务代码（`D2DContext`/`AudioEngine`/…）里**不出现任何 `#ifdef _MSC_VER`/`__MINGW32__`**，也不出现 `Base.Base`、`__uuidof`、`IID_PPV_ARGS`。所有分歧只在这三个文件里：`src/graphics/win_compat.h`、`src/graphics/d2d_c.h`、`src/graphics/dwrite_c.h`。

### 3.1 `src/graphics/d2d_c.h`：一份平铺 vtable，两套编译器共用
- 13 个 vtable 结构体：`ID2D1Factory`、`ID2D1RenderTarget`、`ID2D1HwndRenderTarget`、`ID2D1SolidColorBrush`、`ID2D1LinearGradientBrush`、`ID2D1RadialGradientBrush`、`ID2D1BitmapBrush`、`ID2D1Bitmap`、`ID2D1GradientStopCollection`、`ID2D1StrokeStyle`、`ID2D1PathGeometry`、`ID2D1GeometrySink`、`ID2D1Brush`。
- **不用 SDK 的 vtable 名字**（MinGW 已经用掉了这些名字）：统一加前缀 `PDK_ID2D1FactoryVtbl` / `PDK_ID2D1Factory`。业务代码用 `PDK_AS(Factory, p)` 一类宏做一次指针转换。
- **结构体构造规则（已实测可行，按这个写）**：
  1. 按**真实继承链**嵌套 PDK 基类结构：`PDK_ID2D1ResourceVtbl{ PDK_IUnknownVtbl Base; GetFactory; }` → `PDK_ID2D1BrushVtbl{ PDK_ID2D1ResourceVtbl Base; SetOpacity; SetTransform; GetOpacity; GetTransform; }` → …
  2. 每个接口的方法**按 SDK 声明顺序全部占位**（用 `void* p_Method;`），**只给实际调用的方法定型**。**绝不能只写用到的方法**——漏一个前面的方法，后面全部错位（我实测时漏了 `IDWriteFactory` 开头的 `GetSystemFontCollection` 等 8 个方法，静态断言立刻抓到）。
  3. 继承链（`ID2D1Factory` 实测是 `: public IUnknown`，**没有** `GetFactory`）：`IUnknown` 直接派生 = Factory、GdiInteropRenderTarget、SimplifiedGeometrySink、TessellationSink；`ID2D1Resource` 派生 = Brush、Image、RenderTarget、Geometry、DrawingStateBlock、GradientStopCollection、Layer、Mesh、StrokeStyle。
- 实测 vtable 大小（用于 S0 写死断言）：`ID2D1FactoryVtbl`=136 B(17 槽)、`ID2D1SolidColorBrushVtbl`=80(10)、`ID2D1HwndRenderTargetVtbl`=480(60)、`ID2D1PathGeometryVtbl`=168(21)、`ID2D1GeometrySinkVtbl`=120(15)。
- `D2D1_*` **结构体与枚举直接复用 SDK**（MSVC C 模式提供了全部），只把 `const D2D1_X&` → `const D2D1_X*`。
- `D2D1::` 辅助类型 50 处 → 聚合初始化：`D2D1::Point2F(x,y)` → `(D2D1_POINT_2F){x,y}`；`Matrix3x2F::Identity()` → 手写常量；`SizeU`/`RectF`/`RectU`/`ColorF`/`PixelFormat`/`RoundedRect`/`Ellipse`/`GradientStop`/`*BrushProperties`/`RenderTargetProperties`/`HwndRenderTargetProperties`/`StrokeStyleProperties`/`BitmapProperties` 同理。
- 调用一律走 `PDK_*_CALL`/`PDK_RELEASE` 宏，例如 `PDK_CALL(rt, Release)`，宏内部展开为 `((PDK_ID2D1RenderTarget*)(rt))->lpVtbl->Release(...)`——**两套工具链同一份实现**。

### 3.2 `src/graphics/dwrite_c.h`：类型回落 + 4 个 vtable
- **MinGW 分支**：直接 `#include <dwrite.h>`，再按 3.1 的规则定义 `PDK_` 前缀 vtable。
- **MSVC 分支**：`dwrite.h` 不可用，因此逐字抄出所需内容（枚举/结构体/常量），并要求与 MinGW 的 `dwrite.h` **逐项静态断言一致**（§3.3）：
  - 枚举/结构体：`DWRITE_FACTORY_TYPE`、`DWRITE_TEXT_ALIGNMENT`、`DWRITE_PARAGRAPH_ALIGNMENT`、`DWRITE_WORD_WRAPPING`、`DWRITE_LINE_SPACING_METHOD`、`DWRITE_TRIMMING_GRANULARITY`、`DWRITE_TRIMMING`、`DWRITE_TEXT_METRICS`、`DWRITE_TEXT_RANGE`、字体 `WEIGHT/STYLE/STRETCH` 常量。
  - 4 个 vtable：`IDWriteFactory`、`IDWriteTextFormat`、`IDWriteTextLayout`、`IDWriteInlineObject`——**按 SDK 全量方法顺序占位**，只给定型用到的那几个（`IDWriteFactory` 有 **24 槽**：`GetSystemFontCollection`…`CreateGlyphRunAnalysis`，我们要的 `CreateTextFormat`/`CreateTextLayout`/`CreateEllipsisTrimmingSign` 在很后面；`IDWriteTextFormat`=**28 槽**；`IDWriteTextLayout`=**67 槽**；`IDWriteInlineObject`=**7 槽**）。
  - `DWriteCreateFactory` 原型 + 所需 GUID（factory = `b859ee5a-d838-4b5b-a2e8-1adc7d93db48` 等，逐字抄自 `dwrite.h`）。
  - **建议（S5 做）**：写一个 `tools/gen_shim_from_mingw_headers.ps1`，从 MinGW 头提取方法顺序生成占位成员，并让 MinGW 侧 ctest 断言"生成结果与当前头一致"。**我本次的验证脚本就是它的原型**（见附录 C）。

### 3.3 正确性门槛：把 MinGW 的 C vtable 当核对基准（本次已跑通）
- **布局静态断言（已验证可行，必做）**：新增 `tests/shim_layout/`（只在 MinGW 下编译的 dev-only ctest），对每个接口断言：
  ```c
  _Static_assert(sizeof(PDK_ID2D1HwndRenderTargetVtbl) == sizeof(ID2D1HwndRenderTargetVtbl), "size");
  typedef char chk[offsetof(PDK_ID2D1HwndRenderTargetVtbl, GetHwnd)
                 == offsetof(ID2D1HwndRenderTargetVtbl, GetHwnd) ? 1 : -1];
  ```
  `sizeof` 相等是最强断言（任何方法缺失/顺序错都会破坏它），逐方法 `offsetof` 用来定位具体哪一项错了。**本次实测已对 7 个接口（D2D 4 个 + DWrite 3 个）全部通过**，并顺带抓出了我自己写错的两处（`IDWriteFactory` 漏前导方法、`ID2D1Factory` 的继承链假设）。DWrite 侧的枚举值/结构体 `sizeof`/字段偏移同样对照 `<dwrite.h>` 断言。
- MSVC 侧无法 self-check（没有可比对的头），因此以"MinGW 已验证通过的同一份 `PDK_` 定义 + 5 个渲染 ctest 端到端跑到"为门槛；`cl` 与 `gcc` 对 `STDMETHODCALLTYPE` 的处理在 x86 上要**单独复核一次**（见 §7）。
- **兜底（已预留，非默认）**：若出现无法定位的 ABI 崩溃，临时加一个 `dwrite_bridge.cpp`（约 200 行，`extern "C"` 句柄 API），其余仍纯 C；修好后不留。
- 应用侧唯一需要自己实现的 COM 回调：`IMMNotificationClient`（`AudioEngine.cpp` 的 `DeviceNotifier`）→ 手写 vtable（`IUnknown` 3 槽 + 5 个通知方法）+ `IID_IMMNotificationClient`。`IDWriteInlineObject` 只由 `CreateEllipsisTrimmingSign` 产出并回传，无需实现。

### 3.4 `src/graphics/win_compat.h`：工具链分歧的唯一出口
1. **IID 符号来源（已实测定死）**：用 **`#define INITGUID` + 单一 TU 里的 `DEFINE_GUID`**，即新增 `src/graphics/iids.c`（只做一件事：`#define INITGUID` 后 include 相关头 + 我们的 GUID 定义）。实测结果：
   - 带 `-luuid` 链接 → **仍缺** `CLSID_MMDeviceEnumerator`、`IID_IMMDeviceEnumerator`、`IID_IDWriteFactory`；
   - 去掉 `-luuid` → 连 `IID_ID2D1Factory` 也缺；
   - **`#define INITGUID`（不带 `-luuid`）→ 全部解析，链接成功（37,888 B）**。
   因此两套工具链都走同一方案，不依赖 `uuid.lib`/`-luuid` 的符号。
2. **`shlwapi.h` 规避**：两套都不 include `<shlwapi.h>`（MinGW C 模式已实测缺陷），只手写实际用到的 `Path*` 函数声明（S0 先统计使用点，预期 1–3 个）。
3. **include 顺序**：`imm.h` 必须在 `windows.h` 之后；`mfreadwrite.h` 必须在 `mfapi.h`+`mfidl.h` 之后；`INITGUID` 必须在相关头之前。
4. 任何后续发现的工具链差异都加在这里，并写清"为什么"和"何时可以删"。

## 4. 分阶段实施（每阶段独立可构建、可测试、可停）

**前置：建立基线（改任何代码之前）**
在**正常 shell**里跑通现有 MSVC+VC-LTL 基线：`cmake --preset vs2026-release` → build → `ctest`；保存 5 张 JPEG、`pao_de_kuai.exe` 体积、`unit_tests` 耗时到 `docs/c-to-baseline/`（或本地不入库）。开分支，master 保持绿。
**MinGW 侧底噪已测完（本次，无需重做）**：x64/x86 空程序体积与依赖 DLL 见 §0；剩余待测的只有"真实项目在 MinGW 下的最终体积"，那要等 S5–S7 之后（§1.8 判定点）。

| 阶段 | 内容 | 验收 |
|---|---|---|
| **S0** 双工具链骨架 | 新增 `mingw-ucrt-x64-release`（`PDK_MINGW_ROOT`）/`mingw-ucrt-x86-release`（`PDK_MINGW32_ROOT`）preset（`CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER`/`CMAKE_RC_COMPILER=windres`，**不签入 `D:\` 本机路径**）；`C_STANDARD 17` + MSVC `/std:c17`；**删除 VC-LTL 全套逻辑与 `external/vc-ltl/`（保留 `PDK_MSVC_RUNTIME`）**；`WINVER`/`_WIN32_WINNT` → `0x0A00`；建 `src/core/Str.h`、`src/graphics/Com.h`、`d2d_c.h` 骨架、`win_compat.h`、vtable 约定、`RoundToInt`；定死 IID 来源与 `shlwapi` 规避；转 1 个最小 TU（`src/app/Dpi.c`、`WindowChrome.c`）**三条链路各编译链接一遍（MSVC x64、MinGW x64、MinGW x86）** | 三条链路 `ctest` 全绿；exe 能跑；`objdump -p` 依赖表符合 §1.6（x86 额外确认无 `libgcc_s_dw2-1.dll`）；无 C++ 运行时符号 |
| **S1** core | `StringUtil`、`WinFile`、`Tween`、`Timer`、`Geometry`（纯头）、`Scene`/`SceneManager`/`Overlay` 的 vtable 化 | 两套工具链下 `unit_tests` 的 Stats/设置用例路径仍通 |
| **S2** rules | `Card`、`Deck`、`HandPattern`、`MoveValidator`、`PaodeKuaiRules`、`RuleText`、`Scoring` → `Cards` 定长值类型；不动任何规则逻辑 | 两套工具链下 `RulesPatternTests`、`ScoringTests` 通过 |
| **S3** stats | `AppSettings`、`StatStore`、`DailyStat`（cJSON 已是 C，改动小）；JSON 字段与旧文件完全兼容 | 两套工具链下 `StatsTests` 通过；旧 `appsettings.json`/`stat/*.json` 读取行为不变 |
| **S4** game | `AiStrategy`→vtable、`BasicAiStrategy`、`StrongAiStrategy`（1260 行，8 线程并行搜索 → `CreateThread` + `Interlocked`）、`GameState`（1339 行，`set<int>`→位掩码、`map`→数组、`optional`→has 标志）、`LocalAiController`（后台线程 + generation 取消）、`TurnRecord`、`RoundTraceRecorder` | 两套工具链下 `GameStateTests`（1361 行）、`AiStrategyTests`、`DragSelectionTests` 通过；**托管整局耗时不得显著劣化**（固定种子冒烟，MSVC 与 GCC 各跑一次） |
| **S5** 图形（最高风险） | 完成 `d2d_c.h`/`dwrite_c.h`（含 §3.3 布局静态断言测试）、`ComPtr`→`Com.h`、`D2DContext`（589 行）、`ProceduralTextures`、`WicImageLoader`、`SpriteAtlas`、`TextRenderer` | **两套工具链各 5 个 UI ctest 全绿** + 10 张 JPEG 交叉人工比对；`D2DERR_RECREATE_TARGET` 路径手测 |
| **S6** 音频 | `AudioEngine`（WASAPI、MMCSS、`IMMNotificationClient` vtable、软件混音）、`AudioDecoder`（MF 源读取器）、`SoundCatalog` | 真机跑一遍（两套构建各一次）：能出声、切默认设备后自动重开、`Play()` 仍可从 UI 线程随意调 |
| **S7** app/UI 上层 | `resources/*`、`scenes/*`（GameScene 822 行）、`overlays/*`、`ui/*`（`Inputs.cpp` 463 行含 IME 组字、`Widgets.cpp` 343 行）、`app/*`（`WinMain.c`、`Window.c`、`App.c`、`ImeInput.c`） | 两套工具链 `ctest` 全绿；手测设置弹窗（Enter 保存/Esc 取消恢复音量）、IME 玩家名输入、关闭确认、提示、托管 |
| **S8** 收尾 | 清掉 CMake/preset 里的 MSVC-only 与 VC-LTL 残留（`/EHsc`、GNU 分支的 `-std=c++2c` 只留 tests）；删 `external/vc-ltl`；重写 CI 矩阵（见 §5）；更新 `AGENTS.md`（双工具链、C 约束、shim、vtable 约定、禁用语言特性）、`README.md`（构建说明 + 许可去掉 EPL-2.0）、`CHANGELOG.md`；两套工具链体积对比 | **CI 8 组合全绿**；体积达标（§1.8） |

## 5. 构建系统与 CI 变更

**`CMakeLists.txt`**
- `project(PaoDeKuai VERSION 1.2 LANGUAGES C CXX RC)`（**CXX 必须保留**：tests/scene_viewer 是 C++，MSVC 组合也在用；**RC 保留**）。
- 新增 `set(CMAKE_C_STANDARD 17)` + `CMAKE_C_STANDARD_REQUIRED ON`。
- **删除**：`PDK_USE_VC_LTL`（现 L37）、`PDK_VC_LTL_*` 变量与 `pdk_vc_ltl_build` target（L65-159）、`pdk_add_vc_ltl_link_dependency()` 与相关 `add_dependencies`（L253-257、L270-278、L291、L298）。
- **保留**：`PDK_MSVC_RUNTIME`（L8-15，MSVC 六个组合仍靠它切 MD/MT）。
- 编译选项按编译器分流：MSVC 保持 `/utf-8 /EHsc(仅 CXX) /O2 /Ob2 /Os /std:c17`；GCC 用 `-std=c17 -Os -ffunction-sections -fdata-sections -Wall -Wextra -Wpedantic -finput-charset=UTF-8` + 链接 `-Wl,--gc-sections`（`-s`/`-flto` 按实测决定）。**去掉 GNU 分支里给 `pao_de_kuai` 用的 `-static-libstdc++ -static-libgcc`**（它现在是纯 C），这两个只留在 tests/scene_viewer 上。
- `CMake_RC_COMPILER`：MinGW 构建必须显式指向 `windres.exe`。
- 链接库列表两套通用：`d2d1 dwrite windowscodecs ole32 uuid shlwapi imm32 avrt mfplat mfreadwrite mfuuid gdi32 user32 dwmapi`。
- `src/resources/resources.rc` 内容不动；`OBJECT_DEPENDS` 注释里"rc.exe 依赖扫描"的旧说明改写为"rc.exe / windres"。

**`CMakePresets.json`**
- 新增 `mingw-ucrt-x64-release`（**主发布**）与 `mingw-ucrt-x86-release`，generator `Ninja`，显式设置 `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER`/`CMAKE_RC_COMPILER`：

| preset | 工具链根 | C/C++ | RC | target triple |
|---|---|---|---|---|
| `mingw-ucrt-x64-release` | `$env{PDK_MINGW_ROOT}` | `bin/gcc.exe`、`bin/g++.exe` | `bin/windres.exe` | `x86_64-w64-mingw32` |
| `mingw-ucrt-x86-release` | `$env{PDK_MINGW32_ROOT}` | 同上（i686 根下） | 同上 | `i686-w64-mingw32` |
| `mingw-ucrt-x64-debug` | 同 x64 | 同 x64 | 同 x64 | 调试用 |

- **两个环境变量的本地默认值**（个人机器路径，写在 `CMakeUserPresets.json` 或 shell profile，**不入库**）：`PDK_MINGW_ROOT=D:\_\3rd\mingw64-ucrt`、`PDK_MINGW32_ROOT=D:\_\3rd\mingw32-ucrt`。
- `vs2026-*` preset 保留不变（MSVC 六个组合继续用）。

**`.github/workflows/build.yml`（保持 8 job 结构，只换两个）**
| job name | 现在 | 改后 |
|---|---|---|
| `x64 /MD`、`x86 /MD`、`arm64 /MD`、`x64 /MT`、`x86 /MT`、`arm64 /MT` | MSVC + Ninja | **不动**（去掉 `use_vc_ltl` 参数即可），仍 `run_tests` 按现表 |
| `x64 VC-LTL` → `x64 MinGW/UCRT`（**主发布**） | MSVC + VC-LTL，`build_dir: build-vs2026-vcltl-x64`，MinIO 上传 | **MinGW UCRT x64**，`build_dir: build-mingw-ucrt-x64`，`run_tests: true`，**MinIO 上传条件改名**（`s3://.../2026/pao-de-kuai.exe` 目标路径不变） |
| `x86 VC-LTL` → `x86 MinGW/UCRT` | MSVC + VC-LTL | **MinGW UCRT x86**，`build_dir: build-mingw-ucrt-x86`，`run_tests: true` |

**MinGW 工具链在 CI 里的获取（从 niXman 下载，不用 MSYS2）**
- 来源：`niXman/mingw-builds-binaries`，**每次运行解析 `releases/latest`，不 pin 版本**（你的决定：16.1/16.2 对本项目无实质差别，跟着最新走）。今天解析到的就是 `16.2.0-rt_v14-rev1`。
- **资产选择靠正则，不靠版本号**（版本会变，命名规则不会）：
  - x64：`^x86_64-.*-release-win32-seh-ucrt-rt_v\d+-rev\d+\.7z$`
  - x86：`^i686-.*-release-win32-dwarf-ucrt-rt_v\d+-rev\d+\.7z$`（i686 只有 dwarf，没有 seh）
  - **必须匹配 `win32` 线程 + `ucrt`**（与本地 `build-info.txt` 的 `--threads=win32 --with-default-msvcrt=ucrt` 一致）；正则改到 0 个或多个匹配都直接失败，别静默选错包。
- **完整性校验不写死哈希**：用 release API 里每个 asset 自带的 `digest`（`sha256:<hex>`）校验下载结果。这样"不 pin 版本"和"防篡改/防半包"可以同时成立。
- 做法：解析 latest → 正则选两个 asset → `actions/cache` 按 **解析出的 tag + arch** 做 key（tag 变了自动失效，不会拿到旧工具链）→ 未命中则 `curl` 下载 + 按 API `digest` 校验 → `7z x`（Windows runner 自带 7z）→ **把解压目录的 `bin` 前置进 `PATH`** → 跑链接冒烟检查 → 给 CMake 传 `-DPDK_MINGW_ROOT` / `-DPDK_MINGW32_ROOT`（或设同名环境变量）。
- **不 pin 版本的补偿措施（必须做，否则"某天 CI 突然挂了"会无从归因）**：
  1. 把**解析到的 tag、asset 名、`gcc --version` 三行**写进 job summary 和 workflow log 开头；
  2. 提供逃生舱 `PDK_MINGW_TAG`（workflow_dispatch 输入或 repo variable）：设了就用指定 tag，用于复现历史失败；
  3. CI 挂了先看这三行，再决定是代码问题还是工具链漂移。
- 当前参考值（2026-08-16 的 latest，仅作对照；**不要**在 CI 里当断言用）：

| 用途 | 资产文件名 | SHA256 |
|---|---|---|
| x64 job | `x86_64-16.2.0-release-win32-seh-ucrt-rt_v14-rev1.7z` | `c9ae8e4a3afa667b3301dd28c567109d174a0fa0856a2eeaa85a91b89569a50b` |
| x86 job | `i686-16.2.0-release-win32-dwarf-ucrt-rt_v14-rev1.7z` | `24197df9616fc626143b0f91ceda22e0314c0ad7806a1eeace49a5c323f30027` |

- **PATH 是硬性要求（本次实测踩过）**：`bin` 不在 PATH 时，gcc 默认调用的 `<root>\<triple>\bin\as.exe` 找不到 `libwinpthread-1.dll`，以 `0xC0000135` 退出且 **gcc 零输出**，表现为"编译静默失败"。CI 里必须：① 把 `bin` 前置进 PATH；② 在 Configure 之前跑一条链接冒烟检查（例如 `gcc -x c -o nul -` 或编译一个 5 行程序），失败就立即报错，别让它在 CMake 里伪装成别的问题。
- **不要用 `msys2/setup-msys2`**：MSYS2 没有官方 ucrt32。
- 其余步骤（Configure/Build/Test/Upload artifact）结构复用；`vcvarsall.bat` 只在 MSVC job 里调用，MinGW job 不调。
- **本地版本不追平**（你的决定）：本地 x64=16.1.0、x86=16.2.0，CI 走 latest。若某天出现"CI 挂、本地复现不出"，**第一嫌疑就是工具链版本**，用上面的 `PDK_MINGW_TAG` 对齐后再排查代码。

## 6. 测试策略

- `tests/` 保持 C++ + doctest（你已确认），且**必须同时被 MSVC `cl` 与 MinGW `g++` 编译通过**（MinGW 侧加 `-static-libstdc++ -static-libgcc`，避免随包 DLL）。测试体改写成 C API 调用：`rules::Cards cards = {...}` → C 的 `Cards` 构造；`GameStateTests.cpp`（1361 行）改动量最大。
- `TestHelpers.h` 改成"C++ 夹具包装 C API"：`C(rank,suit)`、`MakeCards({...})`、`LeadContext/FollowContext` 填 C 结构体。
- `TestWeakAiStrategy.h`：从 `class : public AiStrategy` 改成填一张 `static const AiStrategyVtbl` 函数表 + `void* user`，验证 vtable 注入点（对应 `GameState::SetLocalAiStrategy`）。
- **新增 `shim_layout` 自检 ctest（仅 MinGW）**：§3.3 的布局静态断言，编译期即验证 17 个 vtable 与 DWrite 类型的布局/枚举值。
- 每阶段回归：**两套工具链各跑** `ctest --output-on-failure`（MSVC preset + `mingw-ucrt-x64-release`）+ 截图人工比对。
- 截图交叉比对：MinGW 与 MSVC 各 5 张，接受 DWrite 字体度量带来的 <1px 抗锯齿差异，但布局/颜色/尺寸差异必须为零。

## 7. 边界情况与失败模式

- **定长 `Cards` 溢出**：上限 48；所有写入走 `Cards_Push`（Debug 断言，Release clamp），禁止裸下标写。
- **字符串截断**：固定缓冲文本（toast、reason、对话）统一走带长度检查的追加函数；`StatStore` 的 JSON 转义逻辑必须原样保留。
- **线程与取消**：`LocalAiController` 的后台线程 + generation 版本号用 `InterlockedIncrement` 实现，`CreateThread` 句柄必须 `CloseHandle`；`StrongAiStrategy` 的 8 worker 线程同理；`std::atomic<float>` 用位模式 `InterlockedExchange`。MinGW 是 win32 线程模型，**不要混入 winpthreads**。
- **COM 生命周期**：去掉 `ComPtr` 后最易泄漏/双释放。每个 `ReleaseAndGetAddressOf` 模式都要在 C 里有明确对应；`DeviceNotifier` 的引用计数与 `Release` 时机是重点（`AudioEngine.cpp:200/227`）。
- **`D2DERR_RECREATE_TARGET`**：`OnD2DResourcesLost/Recreated` 路径需重测，保持"游戏/窗口状态保留、音频与 CPU 数据不受影响"。
- **双编译器差异（新增，重点）**：
  - MSVC C 模式：`__uuidof`、`IID_PPV_ARGS` 不可用；`interface`/`STDMETHODCALLTYPE`/`CONST_VTBL` 由 `unknwn.h` 提供；**x86 上 `__thiscall` vs `__stdcall` 需复核**（D2D/DWrite 接口用 `STDMETHODCALLTYPE`，理论上两套一致，但这是 x86 job 必须亲眼确认的一项）。
  - GCC：严格 `-std=c17` 与 SDK 头的兼容性（必要时回退 `-std=gnu17` 并记录）；`__USE_MINGW_ANSI_STDIO` 影响 `printf` 长整型格式化；不要用 `__attribute__`；**GCC 16 把隐式函数声明当错误**（漏原型直接失败，反而省事）。
  - MinGW C 模式头缺陷：`shlwapi.h` 的 `IQueryAssociations` 已实测（§3.4）。
- **`as.exe` / PATH（本次实测的最大坑）**：`bin` 不在 PATH → `as.exe` 缺 `libwinpthread-1.dll` → `0xC0000135`，**gcc 不打印任何诊断**，只在构建系统里表现为莫名的编译失败。凡是在新 shell / CI / 其他机器上跑 MinGW，先做链接冒烟检查。
- **`windres`**：已实测可用（真实 ico + RCDATA + UTF-8 中文注释 → 85,858 B 的 `.res.o`）。剩余风险只在 `OBJECT_DEPENDS` 依赖扫描与 CMake 的 `CMAKE_RC_COMPILER` 设置。
- **IID 符号**：已定死为 `#define INITGUID` + 单一 TU（§3.4）；**不要**指望 `-luuid`/`uuid.lib`（实测缺 `CLSID_MMDeviceEnumerator`、`IID_IMMDeviceEnumerator`、`IID_IDWriteFactory`）。忘了 `INITGUID` 会在链接期报未定义 `IID_*`。
- **MF/头文件顺序**：`mfreadwrite.h` 在 `mfapi.h`+`mfidl.h` 之前 include 会失败（已实测；两套都复核）。
- **性能回归**：rules 层改定长值类型应更快（少堆分配），但 `StrongAiStrategy` 的 8 线程并行搜索 + AI 托管整局耗时必须在 S4 实测，**MSVC 与 GCC 各记一次**，不得退化。
- **体积**：MSVC 组合去掉 VC-LTL 后体积会变大（预期，不再作为发布目标，但 CI 里记录数字）；MinGW 主发布按 §1.8 判定。

## 8. 明确不做 / 假设 / 待你批注

**不做**
- 不改游戏规则、计分、AI 行为强度、UI 视觉与交互、设置与统计的 JSON schema。
- 不引入新第三方依赖（VC-LTL 是**删除**，不是替换）。若某处最终确需一个单头文件 public-domain C 库，先向你申请。
- 不再支持 Win8/Win7（本轮显式放弃）。
- 不放弃 MSVC 编译能力（本轮明确要求保留）。
- 资源仍走 `.rc` 嵌入（rc.exe / windres 各用各的），不引入运行时 metadata 文件。
- 不重新引入联网/大模型 AI。
- 阶段间可在任何一步停下并保持可交付。

**待你批注的开放项（我按默认值先写进计划）**
1. ~~本地 x64 工具链的版本对齐~~ **已定（你的决定）**：16.1/16.2 对本项目没有实质差别，**本地不追平**（x64=16.1.0、x86=16.2.0），**CI 每次解析 latest**（见 §5）。补偿措施：CI 打印解析到的 tag / asset / `gcc --version`，并提供 `PDK_MINGW_TAG` 逃生舱用于复现历史失败。
2. **`-flto` 是否进正式 Release**：**基本已有答案**——修订 4 实测在最小程序上零收益（15,360 B 不变），故默认**不开**；仅保留"S8 用真实 exe 复测一次"的机会。`-s` 默认进 Release。（`-s` 会去掉符号，若你希望保留可调试性，可以改成只对发布 tag 开。）
3. **MSVC 组合是否还上传 artifact**：默认 8 个 job 都上传（现状不变），只有 MinGW x64 额外发 MinIO。
4. **截图基线归属**：默认以 MinGW x64 的 5 张为"新基线"，MSVC 的 5 张作为交叉比对。
5. **`dwrite_c.h` 手抄 vs 脚本生成**：默认先手抄 + §3.3 静态断言；若你希望彻底防漂移，我在 S5 加 `tools/gen_shim_from_mingw_headers.ps1`（修订 4 的验证脚本就是它的原型，成本已很低）。
6. **CI 工具链缓存失效策略**：默认按 `mingw-<tag>-<arch>` 缓存解压目录，tag 变了自然失效；下载的 `.7z` 本身不缓存（约 108 MB，每次重下或走 runner 的临时目录）。
7. **`rules` 迁移路线（附录 B 的 A/B）**：`rules::Cards` + 4 个 `enum class` 是全仓库中心类型，无法"只改 rules 目录"。**路线 A**＝把 `rules`+`game`+`CardView`/`GameScene`+测试合并成约 6,000 行的一次性大步骤（边界干净、无中间态）；**路线 B**＝先用一个临时 C++ 兼容头（约 200–300 行，最终删除）让上层先编过，把重命名分摊到后续阶段。默认按 A。

## 9. 工作量与风险

规模：`src/` 11,671 行 / 123 文件，`tests/` 2,417 行，`scene_viewer` 145 行。相比修订 1，**shim 工作量回来了**（13 + 4 个 vtable + DWrite 类型回落），但**核对成本大幅下降**（MinGW 头可做机器静态断言）；相比初版，**多了一套工具链的构建/CI/验证成本**。

| 风险 | 等级 | 缓解 |
|---|---|---|
| **D2D/DWrite shim 布局或签名写错**（影响面=全部 UI，且要同时满足两套编译器） | ~~高~~ **中**（本次已用 7 个接口的静态断言跑通方法，并抓到 2 处真实错误） | §3.3 用 MinGW 的 C vtable 做编译期 `sizeof`+`offsetof` 断言；两套各 5 个渲染 ctest 兜底；预留 `dwrite_bridge.cpp` 退路 |
| **体积不达标** | ~~高~~ **中低**（实测 CRT 底噪仅约 15 KB，基线 679 KB 主要是应用代码；风险转移到 `src/` 的编译产物与 shim 代码量） | 已定 `-Os -s +--gc-sections`；若超标再看 `-fno-asynchronous-unwind-tables`、绕开 `libmingwex` 大函数、继续手写格式化；`-flto` 实测无收益，不作为期望手段 |
| **`as.exe` PATH 陷阱**（gcc 静默失败，极易误判为代码问题） | 中（新环境必踩一次） | `<root>\bin` 前置进 PATH；CI 在 Configure 前加链接冒烟检查；写进 `AGENTS.md` |
| 双编译器差异漏检（只在一边编过） | 中高 | S0 起每个阶段**两边都跑**；CI 8 组合本身就是这道闸 |
| MSVC x86 上 vtable 调用约定差异 | 中 | x86 job 保留 `run_tests: true`，配合 CI 的 MSVC x86 组合与 MinGW x86 组合各跑一次 |
| MinGW x86 工具链 | 中低 | 已解决：本地 `D:\_\3rd\mingw32-ucrt`（gcc 16.2.0，i686-win32-dwarf-ucrt）；CI 从 niXman 解析 latest（正则选 `i686-*-win32-dwarf-ucrt-*`，见 §5）；**不用 MSYS2**（无官方 ucrt32） |
| 无 RAII 后的 COM 泄漏/双释放 | 中高 | 统一 `PDK_RELEASE`；S5/S6 加 Debug 下 COM 计数核查 |
| 测试体重写（尤指 GameStateTests 1361 行） | 中 | 夹具集中在 `TestHelpers.h`；按阶段随对应模块一起改 |
| MinGW C 模式头缺陷（已中 `shlwapi`） | 中低 | 统一进 `win_compat.h`；S0 做一次"SDK 头体检" |
| **CI 用 latest 带来的工具链漂移**（新增：不 pin 版本，某天 CI 会因为上游更新而变红） | 中 | ① 用 release API 的 asset `digest` 做完整性校验（不必写死哈希）；② cache key 含解析出的 tag，不会误用旧包；③ 打印 tag/asset/`gcc --version` 到 job summary；④ `PDK_MINGW_TAG` 逃生舱复现历史；⑤ §3.3 的 `shim_layout` 静态断言是防"上游头文件变了"的护栏 |
| 本地与 CI 工具链版本不一致（16.1.0 / 16.2.0 / 未来 latest） | 中低 | 已接受（你的决定）；排查顺序写进 `AGENTS.md`：先对齐工具链，再看代码 |
| x86 的 dwarf 例外模型影响 C++ 测试 | 低 | 纯 C `src/` 不受影响；测试目标 `-static-libgcc -static-libstdc++`，禁止产生 `libgcc_s_dw2-1.dll` 依赖 |
| 音频线程/回调 | 中 | S6 真机手测（切设备、静音、暂停），两套构建各一次 |

这是**数周级、需要你逐阶段验收**的工程，不是一次对话能完成的重写。我不会假装它只是"改改语法"。

## 10. 批准后立即执行的第一步（S0）

1. **在正常 shell 补基线**：跑现有 MSVC+VC-LTL 基线（`cmake --preset vs2026-release` + build + `ctest --output-on-failure`），保存 5 张 JPEG、exe 体积、`unit_tests` 耗时。
2. ~~量 MinGW 底噪~~ **已完成（本次）**：x64/x86 空程序 `-Os -s` 均 15,872 B、`+--gc-sections` 15,360 B、依赖只有 `KERNEL32` + `api-ms-win-crt-*`、`windres` 与全 SDK 面链接均通过、7 个接口的 shim 布局静态断言通过——见 §0 与附录 C。**结论：679,424 B 里 CRT 只占约 15 KB**，"能不能更小"最终取决于应用代码。
3. 开改造分支；`CMakeLists.txt` 加 `CMAKE_C_STANDARD 17` + MSVC `/std:c17`，新增 `mingw-ucrt-x64-release`/`mingw-ucrt-x86-release` preset（`PDK_MINGW_ROOT`/`PDK_MINGW32_ROOT` 环境变量），**删除 VC-LTL 全套逻辑与 `external/vc-ltl/`（保留 `PDK_MSVC_RUNTIME`）**。
4. 建 `src/core/Str.h`、`src/graphics/Com.h`、`src/graphics/win_compat.h`、`src/graphics/iids.c`（`INITGUID`）、`d2d_c.h` 骨架、`goto cleanup` 约定文档（写进 `AGENTS.md` 草稿，含 **PATH 必须含 `bin`** 这一条）；做"SDK 头体检"（WIC/WASAPI/MF/IMM32/avrt/dwmapi/D2D/DWrite 全 include 进一个 `.c`，**MSVC、MinGW x64、MinGW x86 各编一次**）；统计 `shlwapi` 使用点。
5. 转 `src/app/Dpi.*`、`src/app/WindowChrome.*` 两个最小 TU 为 `.c`，**三个工具链各编译链接一遍**，验证 `rc.exe`/`windres` 两条资源路径，以及各构建的 `ctest` 全绿。
6. 先写一版 CI（两个 MinGW job 的下载+校验+缓存+**PATH**+**链接冒烟检查**），确认 niXman 资产在 runner 上能解压可用——**这一步不含业务改造，可以立刻做完**。
7. 迁移 §3.3 的验证脚本（本次探测用的那个 PowerShell 原型）到 `tests/shim_layout/`，作为 S5 的前置护栏。
8. 向你汇报 S0 结果与实测数字，再决定是否进入 S1。

---

## 附录 A：MSVC + VC-LTL 历史基线（初版实测，本轮降级为"体积对照基准"）

commit `7f7c15d`，`pure-c-port` 分支起点，`ctest` 6/6 全绿，total 27.06s。

| 项目 | 基线值 |
|---|---|
| `pao_de_kuai.exe` | 679,424 字节 |
| `scene_viewer.exe` | 691,712 字节 |
| `unit_tests.exe` | 720,896 字节 |
| `unit_tests` 耗时 | 0.43s |
| `.text` / `.rdata` / `.pdata` | 0x5C000 / 0x13000 / 0x4000 |
| 依赖 DLL | d2d1, DWrite, ole32, SHLWAPI, IMM32, AVRT, MFPlat, MFReadWrite, USER32, dwmapi, KERNEL32, **msvcrt.dll**（无 vcruntime140/ucrtbase → VC-LTL 生效） |

5 张基线 JPEG（SHA256 前 16 位）：
`ui-start.jpg` 3957C6C2F53AA244 ・ `ui-settings.jpg` F070F87BC6D6C1BF ・ `ui-game-deal.jpg` 6CD22D0439D58FF5 ・ `ui-game-play.jpg` D7BF8DF743A4047B ・ `ui-result.jpg` 42A1685CB8EF1F5C

## 附录 B：S0 期间发现的排期风险（待你决定，尚未写入计划正文）

**`rules` 无法作为独立小步迁移。** `rules::Cards`（`std::vector<Card>`）和四个 `enum class` 是贯穿全仓库的中心类型，实测扇出：

- `rules::Rank::` 734 处、`rules::Suit::` 200 处、`rules::PatternType::` 142 处、`rules::PlayerId::` 200 处（合计约 1,276 处具名枚举限定，遍布 29 个文件）。
- `Cards` 出现在 29 个文件中，含 `game/*`（7 个）、`scenes/GameScene`、`tests/*`（7 个）。
- C 无法使用 `enum class`，因此 `Rank::Three` 这类写法必须整体改为 C 常量名；这一步无法"只改 rules 目录"。

**两条可选路线：**

- **路线 A（推荐）**：把 `rules` + `game` + `CardView`/`GameScene` + 对应测试合并为一个较大的垂直步骤（约 6,000 行），一次性跨过类型断层。步骤更大，但边界干净、无中间态。
- **路线 B**：引入一个一次性 C++ 兼容头（`enum class` 同名 + `Cards` 容器门面包装 C 结构体），让上层 C++ 调用点先编译不变，从而把枚举重命名和调用点改写分摊到后续阶段。代价是多一份临时抽象（约 200–300 行，最终删除）和一处布局兼容假设。

两者都不影响 S0/S1/S3/S5–S8 的划分，只影响 S2/S4 的边界。

## 附录 C：修订记录与实测证据

### 修订 1（已并入修订 2）
- 删除 VC-LTL、改用 MinGW UCRT、放弃 Win8 基线——这三条保留。
- 当时"§3 两个 shim 作废"的结论**已被修订 2 推翻**（因为 MSVC 要继续编译）。

### 修订 2 的实测证据
- `gcc --version` → `gcc.exe (x86_64-win32-seh-rev1, Built by MinGW-Builds project) 16.1.0`；`build-info.txt` 记录 `--with-default-msvcrt=ucrt --threads=win32 --exceptions=seh --arch=x86_64`（nomulti）。
- `gcc -std=c17 -c` 编译同时含 `windows.h`/`d2d1.h`/`dwrite.h`/`wincodec.h`/`mmdeviceapi.h`/`audioclient.h`/`mfapi.h`/`mfidl.h`/`mfreadwrite.h`/`imm.h`/`avrt.h`/`dwmapi.h` 的 `.c`：全部头文件通过，接口调用被正确解析。
- `d2d1.h`：152,087 字节，568 处 `lpVtbl`；`ID2D1ResourceVtbl` 定义于 L500；派生 vtbl 通过 `Base` 嵌套 `IUnknown`。
- `dwrite.h`：225,946 字节，95 处 `__cplusplus` 分支、619 处 `lpVtbl`、27 处 `COBJMACROS`；`IDWriteFactoryVtbl` 定义于 L5231。
- `shlwapi.h:981` 起在 C 模式下报 `unknown type name 'IQueryAssociations'`。
- 工具链自带 `windres.exe`、`ar.exe`、`strip.exe`、`objdump.exe`；`x86_64-w64-mingw32\lib` 下 `libd2d1.a`/`libdwrite.a`/`libwindowscodecs.a`/`libmfplat.a`/`libmfreadwrite.a`/`libmfuuid.a`/`libuuid.a`/`libshlwapi.a`/`libavrt.a`/`libdwmapi.a`/`libucrt*.a` 均在位。
- **MSVC 侧旧结论（初版实测）**：`cl /TC` 下 `dwrite.h` 完全不能 include（`dwrite.h:4704` → C2059），`d2d1.h` C 模式只给前向声明（L3571-3764）但已提供全部 `D2D1_*` 类型与 `IID_ID2D1*` 声明。
- **未完成**：~~两套工具链的链接与体积测量都未做~~ → **已在修订 4 完成**（见下）。

### 修订 4 的实测证据（启用完全权限后重跑）

**1. `as.exe` 静默失败的根因（不是沙箱）**
- 直接调用 `<x64>\bin\as.exe -o out.o in.s` → **成功**（`.s` 582 B → `.o` 914 B）；`as.exe --version` 正常。
- 直接调用 `<x64>\x86_64-w64-mingw32\bin\as.exe`（**gcc 默认走的就是这个**）→ 退出码 **`-1073741515` = `0xC0000135` STATUS_DLL_NOT_FOUND**。
- `objdump -p` 显示 `as.exe` 的导入表含 **`libwinpthread-1.dll`**，而该 DLL 只位于 `<root>\bin\`。gcc 调用它时**不打印任何诊断**，只返回 1。
- 修复：把 `<root>\bin` 前置进 `PATH` → x64/x86 全部正常链接。**CI 与任何新 shell 都必须这么做。**

**2. 体积与依赖（`-Os -s -mwindows`，空 `WinMain`）**

| 配置 | x64 | x86 |
|---|---|---|
| `-Os -s` | **15,872 B** | **15,872 B** |
| `-Os -s` + `--gc-sections` | **15,360 B** | 15,872 B |
| `-Os -s` + `--gc-sections` + `-flto` | 15,360 B（**无收益**） | 15,872 B（**无收益**） |
| `-Os`（不 strip） | 55,612 B | 52,510 B |

- `objdump -p` 依赖表（x64 与 x86 相同）：`KERNEL32.dll` + `api-ms-win-crt-{environment,heap,locale,math,private,runtime,stdio,string}-l1-1-0.dll`。**没有 `msvcrt.dll`、没有 `vcruntime140.dll`、没有 `libgcc_s_*`、没有 `libwinpthread-1.dll`** —— 与 §1.6 的目标一致。
- 含 `snprintf("%s=%d/%llu/%.2f")` 的版本体积相同（15,872 B），说明 `libmingwex` 的格式化开销被页对齐掩盖，量级很小。

**3. 全 SDK 面一次性链接**
- `D2D1CreateFactory` + `DWriteCreateFactory` + `CoCreateInstance(CLSID_WICImagingFactory)` + `CoCreateInstance(CLSID_MMDeviceEnumerator)` + `MFStartup` + `MFCreateSourceReaderFromURL` + `ImmGetContext` + `AvSetMmThreadCharacteristicsW` + `DwmSetWindowAttribute` 在同一 TU 内链接通过（**37,888 B**）。
- **IID 来源实测**：带 `-luuid` 仍缺 `CLSID_MMDeviceEnumerator`、`IID_IMMDeviceEnumerator`、`IID_IDWriteFactory`；去掉 `-luuid` 连 `IID_ID2D1Factory` 都缺；**`#define INITGUID` 且不带 `-luuid` → 全部解析、链接成功**。→ 定稿为单一 `iids.c` + `INITGUID`。

**4. `windres`**
- `<x64>\bin\windres.exe -i t.rc -o t.res.o` → 成功；`.rc` 内含 UTF-8 中文注释、`1 ICON "…/pao-de-kuai.ico"`、`CARDS RCDATA "…/poker-cards.png"`；`.res.o` 85,858 B，链接出 101,888 B 的 exe。

**5. shim 布局的编译期验证（本次最关键的结论）**
- 方法：写脚本从 MinGW 头提取每个接口的**完整方法顺序**，生成 `PDK_*Vtbl`（按真实继承链嵌套 PDK 基类；未用到的方法用 `void*` 占位；只给用到的方法定型），然后断言 `sizeof(PDK_X) == sizeof(XVtbl)` 与逐方法 `offsetof`。
- 结果：**ID2D1SolidColorBrush、ID2D1HwndRenderTarget、ID2D1PathGeometry、ID2D1GeometrySink、IDWriteFactory、IDWriteTextFormat、IDWriteTextLayout 共 7 个接口全部通过**（`gcc -std=c17 -Wall` 退出 0）。
- 过程中被这套断言抓到的真实错误：① `ID2D1Factory` **直接继承 `IUnknown`（没有 `GetFactory`）**——我原先按"继承 ID2D1Resource"写；② `IDWriteFactory` 的 `CreateTextFormat` 前面还有 `GetSystemFontCollection` 等 8 个方法，**只写用到的方法会整体错位**。
- 实测 vtable 大小（供 `tests/shim_layout/` 写死）：`ID2D1FactoryVtbl`=136、`ID2D1SolidColorBrushVtbl`=80、`ID2D1HwndRenderTargetVtbl`=480、`ID2D1PathGeometryVtbl`=168、`ID2D1GeometrySinkVtbl`=120、`IDWriteFactoryVtbl`=192、`IDWriteTextFormatVtbl`=224、`IDWriteTextLayoutVtbl`=536、`IDWriteInlineObjectVtbl`=56。

**6. 其他**
- MinGW 的 `ID2D1FactoryVtbl` 用 `IUnknownVtbl Base` 而非 `ID2D1ResourceVtbl Base`，**这不是头部 bug**（与 `interface ID2D1Factory : public IUnknown` 一致）；d2d1.h 用 `STDMETHOD(...)` 宏写函数指针，dwrite.h 用显式 `HRESULT (STDMETHODCALLTYPE *X)(...)` 写，两者在 C 模式下都可用。
- **GCC 16 把隐式函数声明当错误**（我写错 `IMMGetContext` 时直接 `error:`，未加 `-Werror`）。
- 探测脚本与产物在临时目录中完成，已删除；脚本原型建议迁到 `tests/shim_layout/`（§10 第 7 步）。

### 修订 3 的实测证据

**本地两条工具链（均已实地核对）**

| | x64（主发布） | x86 |
|---|---|---|
| 根目录 | `D:\_\3rd\mingw64-ucrt` | `D:\_\3rd\mingw32-ucrt` |
| `gcc --version` | `16.1.0`，pkgversion `x86_64-win32-seh-rev1` | `16.2.0`，pkgversion `i686-win32-dwarf-rev1` |
| `-dumpmachine` | `x86_64-w64-mingw32` | `i686-w64-mingw32` |
| `build-info.txt` args | `--mode=gcc-16.1.0 ... --with-default-msvcrt=ucrt --rt-version=v14 --threads=win32 --exceptions=seh --arch=x86_64` | `--mode=gcc-16.2.0 ... --with-default-msvcrt=ucrt --rt-version=v14 --threads=win32 --exceptions=dwarf --arch=i686` |
| 构建日期 | 05.24.2026 | 08.16.2026 |
| bin 工具 | `windres.exe`、`ar.exe`、`strip.exe`、`objdump.exe`、`ld.exe` | 同（另有 `ld.bfd.exe`） |

- **版本不一致（需你决定，§8 开放项 1）**：x64 = 16.1.0、x86 = 16.2.0。CI 计划统一用 16.2.0。
- x86 的 `i686-w64-mingw32\lib` 下 §2 链接列表用到的 import lib 全部在位：`libd2d1`、`libdwrite`、`libwindowscodecs`、`libole32`、`libuuid`、`libshlwapi`、`libimm32`、`libavrt`、`libmfplat`、`libmfreadwrite`、`libmfuuid`、`libgdi32`、`libuser32`、`libdwmapi`、`libucrt`/`libucrtbase`/`libucrtapp`、`libmsvcrt`。
- **注意**：MinGW 的 `libmsvcrt.a` 在这个 UCRT 工具链里只是转发/兼容库。**已在修订 4 用 `objdump -p` 在链接产物上核对**：实际依赖是 `KERNEL32.dll` + `api-ms-win-crt-*`（UCRT API set），与库名无关。

**CI 工具链来源（niXman/mingw-builds-binaries，CI 解析 latest，下表是 2026-08-16 的实测参考值）**

| 用途 | 资产名 | 大小 | SHA256 |
|---|---|---|---|
| x64 job | `x86_64-16.2.0-release-win32-seh-ucrt-rt_v14-rev1.7z` | 107,950,837 | `c9ae8e4a3afa667b3301dd28c567109d174a0fa0856a2eeaa85a91b89569a50b` |
| x86 job | `i686-16.2.0-release-win32-dwarf-ucrt-rt_v14-rev1.7z` | 109,488,001 | `24197df9616fc626143b0f91ceda22e0314c0ad7806a1eeace49a5c323f30027` |

- 上表的 SHA256 **只在 CI 里作为"解析结果对照日志"打印，不作为断言**（因为不 pin 版本）；真正的门是 release API 每个 asset 自带的 `digest` 字段。要复现这次的工具链，就设 `PDK_MINGW_TAG=16.2.0-rt_v14-rev1`。
- 同 tag 下还有 `posix-*` / `mcf-*` / `*-msvcrt` 等变体，**必须选 `win32` + `ucrt`**（与本地 `build-info.txt` 的 `--threads=win32 --with-default-msvcrt=ucrt` 一致）。
- `i686` 的例外模型只有 `dwarf`（无 SEH），这与本机 x86 工具链一致，不是选错。
- 资产清单来源：<https://github.com/niXman/mingw-builds-binaries/releases>（当时为 [16.2.0-rt_v14-rev1](https://github.com/niXman/mingw-builds-binaries/releases/tag/16.2.0-rt_v14-rev1)）
