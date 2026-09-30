# AGENTS.md

本文档面向 AI 编码代理和项目维护者。公开 README 保持简洁；这里保存更详细的工程约束、规则、测试说明和维护注意事项。

## 构建与测试基线

- 首选工具链：Visual Studio 2026 / MSVC x64。
- 先进入 VS2026 x64 开发者命令行。Community、Professional、Enterprise 或 Build Tools 均可，只要环境已初始化。
- 常用命令：

```powershell
cmake --preset vs2026-release
cmake --build --preset vs2026-release
ctest --preset vs2026-release --output-on-failure
```

- CMake 配置会给 MSVC 添加 `/utf-8` 和 `/EHsc`；默认运行库为 `/MT`，可用 `-DPDK_MSVC_RUNTIME=MD|MT` 切换。
- Release 体积优化使用 MSVC 默认 `/O2 /Ob2` 并额外添加 `/Os`。
- MSVC x64/x86 默认使用 `external/vc-ltl` 中的精简 VC-LTL 源码，在 build 目录生成运行库并把 CRT 链接到系统 `msvcrt.dll`；缺少构建工具或非 x64/x86 架构时自动回退到普通 `/MT`。
- GitHub Actions 覆盖 8 个 VS2026 组合：x64/x86/arm64 的 `/MD`，x64/x86/arm64 的 `/MT`，以及 x64/x86 的 VC-LTL。
- 日常构建和测试以 VS2026 为验证基线。
- `CMakePresets.json` 是项目级配置，应纳入版本控制。个人机器路径应放在 `CMakeUserPresets.json`，不要提交。

## 架构说明

- `pdk_core`：规则、AI、游戏状态、设置、统计和记录。应保持不依赖 Win32 UI。
- `pdk_app`：应用流程、渲染、音频、场景、覆盖层、资源加载和 Win32 相关行为。
- UI 全部使用 Direct2D / DirectWrite 自绘，不使用 user32 控件，设置界面也是自绘覆盖层（`src/overlays/SettingsOverlay.*`）。
- 文本输入：`src/ui/Inputs.*` 提供 `TextField`（光标、选区、剪贴板、内联 IME 组字）、`Slider`、`Segmented`、`Toggle`。`Window` 把 `WM_KEYDOWN`/`WM_CHAR`/`WM_IME_*` 转给 `App`，再交给最上层覆盖层；`src/app/ImeInput.*` 封装 IMM32。只有覆盖层 `WantsTextInput()` 为真时才关联输入法上下文，其余时间输入法是关闭的，候选框位置跟随 `TextCaretRect()`。
- AI 只有本地策略（`basic`/`strong`），经 `LocalAiController` 在后台线程计算。已移除 WinHttp 大模型 AI，不要重新引入联网请求或 API Key 存储。
- 音频：`src/audio/AudioEngine.*` 使用 WASAPI 共享模式事件驱动，在 “Pro Audio” MMCSS 线程上软件混音（最多 24 个声部、主音量实时生效、软限幅）。Win10 上用 `IAudioClient3` 的最小引擎周期降低延迟，拿不到时回退普通 `IAudioClient::Initialize`。mp3 由 Media Foundation 解码为 44.1 kHz 单声道 float，打开设备时按设备采样率做 Hermite 重采样。默认设备切换或 `AUDCLNT_E_DEVICE_INVALIDATED` 时由混音线程重开设备。`Play()` 只入队，可以在 UI 线程随意调用。
- 视觉风格为“东方雅致”：墨绿丝绒牌桌、香槟金细线、朱红只用于印章/炸弹/危险操作；带音高的音效和配色一样统一在 D 大调五声音阶。
- 自绘 UI 分三层：`src/graphics/D2DContext.*`（画刷/文字格式缓存、圆角、渐变、变换栈和透明度栈、`MeasureText`）、`src/graphics/ProceduralTextures.*`（CPU 生成的软阴影九宫格和绒面噪点，只依赖 Direct2D 1.0）、`src/ui/`（`Theme` 色板、`Anim` 缓动、`Widgets` 按钮/面板/胶囊/头像/印章/弹窗、`Icons` 矢量图标、`CardView` 牌面渲染）。新界面优先复用 `src/ui/`，不要在场景里直接写纯色矩形。
- 牌图集加载时额外用 WIC Fant 预缩小 1/2 和 1/4 两级，`CardView` 按目标像素尺寸选级，小牌不会锯齿。
- 生产源码不要用 `std::lround` 等 `msvcrt.dll` 不导出的 C99 数学函数，VC-LTL 构建会链接失败；需要取整用 `ui::RoundToInt`。
- 运行资源通过 `src/resources/resources.rc` 嵌入。
- 设置和统计路径基于进程当前工作目录，不基于 exe 所在目录。
- 应用使用固定 1280x720 逻辑布局。窗口缩放应在场景布局外处理，窗口不应小于 1280x720。
- Windows 基线尽量保持 Win8 兼容：`WINVER=0x0602`、`_WIN32_WINNT=0x0602`、Direct2D、DirectWrite、WIC、Media Foundation、WASAPI（Win10 上额外使用 `IAudioClient3` 低延迟周期）、IMM32。
- 需要处理 `D2DERR_RECREATE_TARGET`：游戏/窗口状态应保留，Direct2D 设备资源和 bitmap 应重建，音频和 CPU 数据应不受影响。

## 体积约束

- 生产源码 `src/` 不引入 `<filesystem>`、`<fstream>`、`<sstream>`、`fmt` 或 `<random>`。这些依赖对当前静态链接 exe 体积影响明显。
- 文件和目录操作走 `src/core/WinFile.*`，字符串数字拼接走 `src/core/StringUtil.*`。
- 需要拼接 UI 文本、日志文件名或提示词时，优先用 `std::string` 追加和 `core::AppendNumber`，不要重新引入流式格式化。
- 洗牌和少量随机选择走当前 `std::srand/std::rand` 路径；本项目不是安全随机场景。
- `std::thread`、`std::mutex`、`std::lock_guard` 可以用于异步网络请求等需要 RAII 的并发代码，不要为了很小体积收益改成裸 `EnterCriticalSection`。
- 测试代码可按需要使用 `<filesystem>` 等标准库便利设施；上述限制主要针对进入主程序的 `src/`。
- 第三方依赖只保留精简 vendor 文件：`external/cjson`、`external/doctest`、`external/vc-ltl`。不要重新引入 CMake 下载依赖，也不要在仓库中签入 VC-LTL `.lib` 产物。

## 产品与 UX 约束

- 这是本地单机桌面游戏，不是联网游戏。
- 第一屏应是实际应用菜单，不做营销落地页。
- 主要场景：Loading、Start、Game、Stats、Settings、Help。
- 主要覆盖层：局末结算、关闭确认、关于、返回菜单、提示、非法出牌 toast、AI 对话气泡。
- UI 控件自绘：按钮、滑块、牌、面板、覆盖层和文字。
- 设置界面保持克制：玩家名、主音量、AI1/AI2 策略（基础/强力）、复盘记录开关。牌大小和动画速度属于固定手感参数，不作为设置项。设置弹窗里 Enter 保存、Esc 取消，取消时恢复打开前的音量。
- 暂不以键盘操作为重点，但 Alt+F4 / 窗口关闭应弹出关闭确认。

## 资源与数据策略

- 所有运行资源通过 `.rc` 嵌入：牌图集、图标和 mp3 文件。
- 精灵和音频布局不依赖运行时 JSON metadata。可替换的资源元数据集中放在 `src/resources/` 或 `src/audio/` 的代码中。
- 音频文件保持独立 mp3 资源，不合并成音频精灵。
- 进入 GameScene 前的重资源加载应放在 LoadingScene。
- `appsettings.json` 保存玩家名、主音量、窗口大小、`ai1`/`ai2`（只接受 `basic`/`strong`，其他值归一为 `basic`）和 `roundTraceEnabled`。不要重新引入已移除的 `cardScale`、`animationSpeed` 或 `aiProviders` 字段；旧配置里的 `aiProviders` 读取时忽略、保存时丢弃。
- 统计数据按每局详细记录写入 `stat/yyyyMMdd.json`；不要存缓存总分。显示时从记录聚合日、月、历史统计。

## 固定游戏规则

- 三人场，48 张牌。
- 去掉大小王；只保留黑桃 2；去掉梅花 A；总共保留 3 张 A。
- 持有黑桃 3 的玩家先出；第一手不强制包含黑桃 3。
- 支持牌型：
  - 单张。
  - 对子。
  - 连对：至少 2 连对。
  - 三带二：两张带牌可以是散牌，不要求对子。
  - 飞机：至少 2 组连续三张；可带 N 到 2N 张散牌；带牌不参与比较；跟飞机时要求三张主体组数相同。
  - 顺子：至少 5 张，不能包含 2。`10JQKA` 有效；`JQKA2` 和 `A2345` 无效。
  - 炸弹：同点数 4 张，范围 3 到 K。没有 A 炸弹或 2 炸弹。
- 四张同点数可以拆三张参与飞机识别，但不能作为四带三出。
- 普通出牌不能三带一；如果最后主动出牌时带牌不够，三张主体可直接出完或只带一张。这个特例不应用于跟牌。
- 如果最后主动出牌时带牌不够，飞机的短牌逻辑可允许直接出完。这个特例不应用于跟牌。
- 要得起必须出，不能不要。
- 规则代码放在 `src/rules/`，并保持不依赖 UI，方便单元测试和未来扩展。

## 计分规则

- 赢家得分为另外两家有效剩余牌数之和。
- 输家只剩 1 张牌时，这 1 张不扣分。
- 春天/关圆鸡：如果某个输家一张牌都没出过，该输家扣 32 分，赢家从该玩家获得 32 分。
- 如果两个输家都被关，赢家获得 64 分。
- 炸弹固定计分：出炸弹者 +20，另外两家各 -10。炸弹分不参与春天翻倍。
- 单局三家总分应为 0。

## AI 与对话策略

- 当前 AI 是固定基础策略：优先走合适牌型，必要时保留炸弹，按规则压牌，能跑完时尽量跑完。
- AI 策略应保持隔离，方便未来增加不同风格。
- AI 对话当前是文字气泡。保留未来接入语音/音频的上下文接口，但不实现真实语音。
- AI 对话应由事件驱动，并带冷却，避免频繁刷屏。

## CTest 目标

当前 CTest 注册：

```text
unit_tests
ui_scene_start
ui_overlay_settings
ui_scene_game_deal
ui_scene_game_play
ui_overlay_result
```

`rules_tests` 运行 `tests/rules_tests/` 下的 doctest 用例，仍产出单个 `rules_tests.exe`。

UI 测试会运行 `scene_viewer.exe`，创建 1280x720 真实窗口，切换到指定场景或覆盖层，更新/渲染固定帧数，然后通过 WIC 保存 JPEG。这些是渲染冒烟测试，不做像素差异比对。

截图模式下 Direct2D 渲染到离屏 WIC 位图（`RenderContext::Initialize(hwnd, true)`），不依赖窗口是否可见；窗口被遮挡或显示器休眠时，HWND 渲染目标会跳过 Present，`BitBlt` 只能截到黑屏。会在 2～3 秒内自动消失的覆盖层（`invalid`、`talk`）和 `loading` 场景会提前截图。

`scene_viewer` 支持：

```text
--scene start|game|stats|settings|help|loading
--overlay confirm-exit|about|tip|invalid|talk|return-menu|result-win
--mock deal|midgame
--screenshot <path>
--quality <1-100>
```

`--scene settings` 会先进入开始场景，再叠加设置弹窗。

## rules_tests 用例说明

测试源码按领域拆分在 `tests/rules_tests/`：

- `RulesPatternTests.cpp`：牌组、牌型识别、比较和出牌校验。
- `ScoringTests.cpp`：计分、春天和炸弹固定分。
- `GameStateTests.cpp`：游戏状态机、出牌顺序、提示和不要约束。
- `AiStrategyTests.cpp`：基础 AI 选牌策略。
- `DragSelectionTests.cpp`：鼠标拖拽选牌。
- `StatsTests.cpp`：设置和统计 JSON。

`fixed deck has 48 cards with only spade two and no club ace`

验证固定牌组为 48 张，只保留黑桃 2，A 有 3 张且没有梅花 A，并包含黑桃 3。

`spade three holder starts`

构造三名玩家手牌，检查持有黑桃 3 的玩家先出。

`recognizes core hand patterns`

覆盖单张、对子、顺子、2 连对起步的连对、普通三带一非法、三带二散牌、飞机、非法 JQKA2、普通裸三张非法、最后短牌三张特例。

`bombs cannot be played as four with three`

验证四张 K 是炸弹，A 炸弹非法，四带三非法。

`move comparison follows fixed rules`

覆盖对子、顺子、飞机和炸弹比较。飞机只比较三张主体点数，且要求主体组数相同。

`lead validation allows short final plane but follow validation does not`

确认最后主动出牌时 `333444` 可作为短飞机出完；同样的牌用于跟牌时不能通过。

`scoring examples and bomb fixed points`

验证剩牌计分、春天计分、炸弹固定分和三家总分为 0。

`game state can finish a full three player autoplay round`

用固定种子跑完整托管对局，作为游戏状态机冒烟测试。

`turn order is counterclockwise so the left-hand player is upstream`

找到 AI2 先出的种子，并验证下一家是 AI1。

`hint passes directly when player cannot beat and pass is blocked when player can beat`

验证玩家要不起时提示会直接不要；要得起时不要会被拒绝。

`ai triple with two keeps an existing pair as a pair`

验证 AI 三带二优先带孤张，不拆已有对子。

`ai plane uses singleton kickers before breaking pairs or triples`

验证 AI 飞机带牌优先使用孤张，不拆对子或三张。

`ai follow chooses a higher singleton when it preserves a pair`

验证 AI 跟牌时以剩余手牌质量优先，不为了最低点数拆对子。

`ai lead avoids a small singleton when next player has one card`

验证下家只剩 1 张时，AI 领出不会打小单张。

`drag selection picks best lead pattern from dragged cards and ignores previous move`

验证拖拽选牌只使用拖拽路线和领出牌型校验，不参考桌面上一手牌。

`drag selection chooses four dragged bomb cards before a smaller pair`

固定拖拽选牌规则：可出张数更多的牌型优先于较小牌型。

`drag selection can choose the longest plane from dragged cards`

确认拖拽选牌可以从路线中选出最长飞机。

`settings and daily stats use current working directory style json`

验证设置 JSON 往返、已移除字段保持缺失、日/月/历史统计可从 JSON 聚合。

`legacy online AI provider settings fall back to basic local AI`

旧配置里带 `aiProviders` 和未知 AI 名称时，加载后回退为 `basic`，重新保存不再写出 `aiProviders` 和 API Key。

## UI 测试说明

`ui_scene_start`

渲染开始场景，验证窗口、Direct2D、字体和主菜单能初始化。

`ui_overlay_settings`

在开始场景上打开自绘设置弹窗并截图，覆盖输入框、滑块、分段选择、开关和按钮的渲染。

`ui_scene_game_deal`

用 `--mock deal` 渲染游戏发牌阶段，覆盖资源加载、发牌动画、牌背/牌面和桌面布局。

`ui_scene_game_play`

用 `--mock midgame` 跳过发牌，脚本化地让玩家先出一手、AI 跟牌后再点一次提示，覆盖桌面出牌、牌型标签、选中抬起、提示光晕和操作按钮栏。

`ui_overlay_result`

在游戏场景上渲染模拟胜利结算覆盖层，验证结算数据显示。

## 需要保持的交互规则

- 普通点击切换鼠标所在牌的选中状态。
- 鼠标左键按住拖过牌时只记录路线，松开时才结算选择。
- 拖拽选牌忽略桌面上一手牌，只从拖拽路线中选择可出的最多张牌型；同张数时使用本地牌型优先级。
- 发牌动画中，中心牌堆保持牌背；玩家牌飞出时显示正面；AI 牌保持牌背。
- GameScene 使用固定 1280x720 逻辑布局，由窗口层负责缩放。
- 提示使用基础 AI 推荐。如果玩家压不过当前牌，提示可直接不要。
- 托管使用 AI 逻辑代替玩家行动，并可取消。
- 选中的牌向上移动显示；悬停的牌高亮。

## 当前版本非目标

- 联网。
- user32 控件。
- 牌桌上的文字输入；输入法只在设置的玩家名输入框接入。
- 联网 / 大模型 AI。
- 复杂成长系统。
- UI 中选择多套规则。
- 运行时资源 metadata 文件。
- 真实 AI 语音或多种实际可切换 AI 性格。
