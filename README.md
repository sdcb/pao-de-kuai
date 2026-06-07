# 跑得快

一个 Windows 桌面版单机三人跑得快游戏，为家人使用场景设计，也作为一个开源 C++ 桌面应用项目维护。

仓库地址：https://github.com/sdcb/pao-de-kuai

## 截图

![](https://github.com/user-attachments/assets/85be4f99-2fe7-4c32-9995-229286d6606f)

## 技术栈

- C++ / Win32
- Direct2D / DirectWrite / WIC
- Media Foundation / XAudio2.8
- cJSON
- doctest
- CMake

## 项目结构

```text
.
├── assets/                  # 扑克牌图集、图标、音效 wav
│   └── audio/               # 音效资源和生成脚本
├── spec/                    # 项目规格与计划文档
├── src/
│   ├── app/                 # App 主流程、Win32 窗口、DPI、入口 WinMain
│   ├── audio/               # XAudio2 音频引擎、音效目录、wav 加载
│   ├── core/                # 场景/覆盖层基类、场景管理、几何、计时、补间
│   ├── game/                # 游戏状态、AI 出牌策略、玩家模型、对局记录
│   ├── graphics/            # Direct2D 渲染上下文、文本渲染、WIC 图片加载、图集封装
│   ├── overlays/            # 确认退出、返回菜单、结算、提示、AI 对话等覆盖层
│   ├── resources/           # Windows 资源 ID、rc 资源、图集坐标数据、资源加载
│   ├── rules/               # 牌、牌组、牌型识别、出牌校验、规则集、计分
│   ├── scenes/              # 开始、加载、游戏、帮助、设置、统计等页面
│   └── stats/               # 设置 JSON、每日/每月/历史统计读写
├── tests/
│   ├── rules_tests/         # doctest 单元测试
│   └── scene_viewer/        # UI 场景查看和截图测试工具
├── CMakeLists.txt
└── CMakePresets.json        # VS2026 release 预设
```

## 构建

先进入任意 VS2026 x64 开发者命令行。Community、Professional、Enterprise 或 Build Tools 均可，只要环境里有 MSVC、Windows SDK、NMake 和 CMake。

```powershell
cmake --preset vs2026-release
cmake --build --preset vs2026-release
ctest --preset vs2026-release --output-on-failure
```

生成目标：

- `pao_de_kuai.exe`：正式 Windows GUI 程序。
- `scene_viewer.exe`：测试/调试用场景查看器，可按参数打开指定场景并截图。
- `rules_tests.exe`：规则与状态单元测试。

## 运行数据

- `appsettings.json` 默认按当前工作目录读写。
- 对局记录和统计由 `stats::StatStore` 管理。
- wav/png 等运行资源通过 Windows `.rc` 嵌入程序。

## License

MIT License. See [LICENSE](LICENSE).
