# PeripheralCapturer

**仅 Windows。** 三个窗口：配置（Widgets）、POV（WebView2 + Vue）、隐藏捕获窗（`HWND_MESSAGE`，收 Raw Input）。Native 用本机 WebSocket 只给 POV 推快照。

录制保存全量 **InputEvent**（微秒时间戳，再按本场抓取帧率归到 `frameIndex`）。浮层只画降频快照。导出帧率在 Profile 里单独配。按键编码和会话列表在 SQLite；颜色、布局用可手改 JSON。

工程保持 Visual Studio CMake：顶层配置 + `PeripheralCapturer/` 放源码。日常用 **x64-debug / x64-release**。详细设计：

- [架构设计](docs/ArchitectureDesign.md)
- [数据库设计](docs/DatabaseDesign.md)
- [InputEvent 事实源](docs/InputEvent.md)
- [设备注册表](docs/DeviceRegistry.md)
- [时钟 Timer](docs/Timer.md)
- [POV 前端（Vue）](docs/PovFrontend.md)
- [待办 TODO](docs/TODO.md)

## 思路一句话

**事件流是事实源；JSON 只改怎么画；码本在库里且用户可注册。**

不要靠「每 16ms 问一次硬件」来录制。事件来了就记，再用本场的 `kFrameUs` 归帧。一帧里的 down/up 不能只看帧末状态。

抓取帧率在**开始录制时**从当前配置拷进这一场并锁定。录着改、录完再改，都只作用于之后新开的录制。导出帧率每次导出用当时的配置。

## 技术栈

| 项 | 选择 |
| --- | --- |
| 语言 | C++20 |
| 配置 / 录制库 | Qt 6 Widgets |
| 浮层 POV | Qt WebView（`QWebView` / WebView2）+ 本机 WebSocket |
| 采集 | Raw Input（`RIDEV_INPUTSINK`）+ XInput |
| 热路径落盘 | 二进制 append-only log |
| 码本 / 会话 | SQLite |
| 日志 | spdlog |
| 构建 | CMake + Ninja，MSVC 2022 |

## 两条链路

```text
采集：WM_INPUT 轻拷包 → 处理线程标准化 InputEvent → 二进制 log
展示：FrameAggregator 60fps 快照 → WebSocket → WebView rAF 渲染
```

Xbox 手柄走 XInput；路径带 `IG_` 的 HID 默认忽略，避免记两遍。键盘鼠标不要 `RIDEV_NOLEGACY`，否则 Qt 控件会收不到键。

## 存储

| 放哪 | 存什么 |
| --- | --- |
| SQLite `key_codes` | 数字键 + 线性轴，用户可注册 / 监听绑定 |
| 会话表 + 二进制 log | 元数据 + 全量事件 |
| Profile JSON | 颜色、布局、导出 fps |
| app-config.json | 当前 Profile 路径、热键、目录 |

## 运行时（Release）

```text
应用目录/
├─ PeripheralCapturer.exe
├─ app-config.json
├─ data.db
├─ recordings/
├─ log/
└─ backups/
```

## 版本计划

| 版本 | 目标 |
| --- | --- |
| v0.1 | 工程、spdlog、配置窗 / 码本 / 录制库空壳 |
| v0.2 | Raw Input 隐藏窗口，键盘鼠标 → InputEvent |
| v0.3 | 二进制 recorder、60fps 快照、浮层 WebSocket |
| v0.4 | HID + XInput 路由、码本绑定 |
| v0.5 | 检查器、回放、导出、穿透热键 |
| v1.0 | 多设备、安装包 |

## 现在怎么编

Visual Studio 打开本目录，选 **x64-debug**，删除缓存并重新配置后生成。`windeployqt` 会把 Qt DLL 拷到 exe 旁。

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Qt 路径在顶层 `CMakeLists.txt` 的 `CMAKE_PREFIX_PATH`。
