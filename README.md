# 跑得快 [![QQ](https://img.shields.io/badge/QQ_Group-495782587-52B6EF?style=social&logo=tencent-qq&logoColor=000&logoWidth=20)](http://qm.qq.com/cgi-bin/qm/qr?_wv=1027&k=mma4msRKd372Z6dWpmBp4JZ9RL4Jrf8X&authKey=gccTx0h0RaH5b8B8jtuPJocU7MgFRUznqbV%2FLgsKdsK8RqZE%2BOhnETQ7nYVTp1W0&noverify=0&group_code=495782587)

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
- VC-LTL
- CMake

## 项目结构

```text
.
├── assets/                  # 扑克牌图集、图标、音效 mp3
│   └── audio/               # 音效资源和生成脚本
├── external/                # 精简签入的第三方依赖和许可证
├── spec/                    # 项目规格与计划文档
├── src/
│   ├── app/                 # App 主流程、Win32 窗口、DPI、入口 WinMain
│   ├── audio/               # XAudio2 音频引擎、音效目录、mp3 加载
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

先进入任意 VS2026 x64 开发者命令行。Community、Professional、Enterprise 或 Build Tools 均可，只要环境里有 MSVC、Windows SDK、Ninja 和 CMake。

```powershell
cmake --preset vs2026-release
cmake --build --preset vs2026-release
ctest --preset vs2026-release --output-on-failure
```

生成目标：

- `pao_de_kuai.exe`：正式 Windows GUI 程序。
- `scene_viewer.exe`：测试/调试用场景查看器，可按参数打开指定场景并截图。
- `unit_tests.exe`：规则、状态和基础 AI 单元测试。

MSVC x64/x86 默认使用 `external/vc-ltl` 中的精简 VC-LTL 源码，在 build 目录生成运行库并将 CRT 链接到系统 `msvcrt.dll` 以减小 exe 体积；仓库不签入 VC-LTL `.lib` 产物，其他架构或缺少构建工具时会自动回退到普通 `/MT`。

## 运行数据

- `appsettings.json` 默认按当前工作目录读写。
- 对局记录和统计由 `stats::StatStore` 管理。
- mp3/png 等运行资源通过 Windows `.rc` 嵌入程序。

## License

MIT License. See [LICENSE](LICENSE).

第三方依赖：

- cJSON：MIT License，见 `external/cjson/LICENSE`。
- doctest：MIT License，见 `external/doctest/LICENSE.txt`。
- VC-LTL：Eclipse Public License 2.0，见 `external/vc-ltl/LICENSE`。
