# PeripheralCapturer

**仅 Windows。** 不做 macOS / Linux，捕获也不做跨平台抽象。键盘鼠标走 Win32 Raw Input，手柄走 XInput / Windows HID，热键和浮层同样按 Windows 习惯实现。

数字通道记开/关，线性通道记归一化连续值，按帧存进 SQLite，用透明浮层回放，再按当前皮肤导出。

配置手感对齐先前的 Keyboard Display：颜色、布局、导出参数是可手改的 JSON；按键编码和录制素材在数据库里，靠界面管理，不靠资源管理器拖文件。

工程保持 Visual Studio CMake 布局：顶层配置 + 子目录 `PeripheralCapturer/` 放源码，不另起 `src/`。当前还是能弹出测试窗口的骨架，模块按版本计划往这个子目录里加。详细设计：

- [架构设计](docs/ArchitectureDesign.md)
- [数据库设计](docs/DatabaseDesign.md)

## 思路一句话

**库里记「按了什么、轴在什么位置」；JSON 只改「怎么画、导出多少 fps」。**

同一份录制可以换皮肤再回放或导出。改颜色或导出帧率，不能改「W 是什么键」，也不能改已经入库的时间线。

## 技术栈

| 项 | 选择 |
| --- | --- |
| 语言 | C++20 |
| UI | Qt 6.11.2 Widgets |
| 浮层 | Qt WebEngine + WebChannel |
| 录制存储 | SQLite（`QSqlDatabase`） |
| 配置 | 用户可编辑的 JSON |
| 构建 | CMake + Ninja，MSVC 2022 |
| 系统 | 仅 Windows 10/11 x64 |
| 捕获 | 仅 Windows：Raw Input、XInput / HID |

## 两套东西，不要混

```text
硬件
  -> Raw Input
  -> 归一化成稳定 key_id（w / pad-a / pad-lt / pad-lx）
  -> 按 value_kind 登记到 key_codes（digital 或 analog）
  -> 按录制 fps 同时采样按下集合和轴值
  -> RLE 后写入 SQLite
  -> 录制库界面浏览 / 检查 / 改备注

回放或导出
  -> 从库读出 key_index -> key_id
  -> 套上当前 Profile JSON 的颜色、布局、export.fps
  -> 浮层或导出文件
```

| 放哪 | 存什么 | 用户怎么动 |
| --- | --- | --- |
| SQLite `key_codes` | 数字键 + 线性轴（含范围） | 预置种子；用户可注册、监听绑定；捕获未知控件先占位 |
| SQLite 会话 / 帧 / 标记 | 素材与时间线 | 只通过「录制库」UI |
| Profile JSON | 颜色、布局、导出 fps、导出格式 | 手改、另存、覆盖写回 |
| `app-config.json` | 当前 Profile 路径、热键、静默录制 | 本地状态，一般跟应用走 |

两个 fps 分开：

- `sessions.fps`：开录时写入的采样率，改 JSON 不影响已录内容
- `export.fps`：导出时重采样，只活在 Profile 里

## 为什么录制用数据库

素材集中、标签和检查好做，事务写入也适合高帧率批量提交。

代价是用户不能在文件夹里拖 `.kbdrec`。所以「录制库」是产品，不是调试表：列表、搜索、检查器、改显示名/标签/marker 备注、删除，以及备份文件的导入导出，都要有界面。禁止只丢一个 `QSqlTableModel`。

会话**不**强制绑定 `profile_id`。开录最多记一个 `profile_name_snapshot`，方便回忆当时用的皮肤名字。

## 为什么配置用 JSON

改浮层颜色、键位排布、导出 fps，应该能打开文件直接改，也能发给别人。这些和「键是什么码」无关，不进库。

Profile 里的 `rows[].id` 必须等于库里的 `key_id`：对得上就按 JSON 上色，对不上检查器仍能看到，浮层不画。

非法 JSON 只提示，不擅自覆盖用户文件。

## 运行时形态（Release）

```text
应用目录/
├─ PeripheralCapturer.exe
├─ app-config.json
├─ data.db                 # 编码 + 录制 + 元数据
├─ log/
└─ backups/                # 会话导入导出的默认目录
```

Profile 不预生成。用户「另存为」时自选路径；`sourcePath` 只用于覆盖写回。

## 模块怎么切

```text
InputCapture    捕获并归一化
Recorder        按 fps 采样、打 marker（控制热键不进按键流）
Storage         用户可注册的码本 / 帧 / 元数据，不存皮肤
Config          读写校验 JSON
Playback        读库 + 套当前 Profile
Export          当时选中的皮肤，不读会话上的过期绑定
UI              配置窗、录制库、透明浮层
```

热路径不逐条事件走总线：捕获线程维护按下集合，按 UI 刷新或录制采样周期推 snapshot。日志只记开停录、导入导出和失败。

## 版本计划

| 版本 | 目标 |
| --- | --- |
| v0.1 | C++20 工程、JSON 读写、库表、配置窗、录制库空壳、码本注册页 |
| v0.2 | Raw Input + key_id 归一化 |
| v0.3 | 采样入库、热键、marker、录制库改元数据 |
| v0.4 | 回放时间轴、浮层按 Profile 渲染、检查器 |
| v0.5 | 手柄线性轴入库与回放、会话备份、JSON/CSV、静默录制 |
| v1.0 | 多手柄、安装包；overlay 视频导出可选 |

## 现在怎么编

Visual Studio 打开本目录，选 CMake preset `x64-debug`，生成后运行。构建会调用 `windeployqt`，把 Qt DLL 放到 exe 旁边。

命令行（需已配置 MSVC 与 Qt）：

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Qt 前缀目前写在顶层 `CMakeLists.txt` 的 `CMAKE_PREFIX_PATH`。换机器时改这一处。
