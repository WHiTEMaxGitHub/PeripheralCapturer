# PeripheralCapturer

**仅 Windows。** 三个窗口：配置（Widgets）、POV（WebView2 + Vue）、隐藏捕获窗（`HWND_MESSAGE`，收 Raw Input）。Native 用本机 WebSocket 只给 POV 推快照。

采集出 **InputEvent**（微秒时间戳，再按本场抓取帧率归到 `frameIndex`）。录制写入 SQLite `frame_data`：一行一帧，`blob` = bitset + float32。浮层只画降频快照。导出帧率在 Profile 里单独配。码本在库里；颜色、布局用 JSON。

工程保持 Visual Studio CMake：顶层配置 + `PeripheralCapturer/` 放源码。日常用 **x64-debug / x64-release**。给代理/协作者的代码规范：[AGENTS.md](AGENTS.md)。详细设计：

- [架构设计](docs/ArchitectureDesign.md)
- [数据库建表](docs/DatabaseSchema.md)
- [数据库设计](docs/DatabaseDesign.md)
- [InputEvent](docs/InputEvent.md)
- [设备注册表](docs/DeviceRegistry.md)
- [时钟 Timer](docs/Timer.md)
- [输入队列](docs/InputQueue.md)
- [POV 前端（Vue）](docs/PovFrontend.md)
- [待办 TODO](docs/TODO.md)

## 思路一句话

**事件流驱动采集；库里按帧存状态；JSON 只改怎么画。**

不要靠「每 16ms 问一次硬件」来录制。事件来了就记，再用本场的 `kFrameUs` 归帧写入 `frame_data`。一帧里的 down/up 在 bitset 里只看得到帧末。

抓取帧率和 `device_bits` 在**开始录制时**锁定。录着改、录完再改，都只作用于之后新开的录制。导出帧率每次导出用当时的配置。

## 技术栈

| 项 | 选择 |
| --- | --- |
| 语言 | C++20 |
| 配置 / 录制库 | Qt 6 Widgets |
| 浮层 POV | Qt WebView（`QWebView` / WebView2）+ 本机 WebSocket |
| 采集 | Raw Input（`RIDEV_INPUTSINK`）+ XInput |
| 录制落盘 | SQLite `frame_data` BLOB（内存攒批） |
| 码本 / 会话 | 同一份 `data.db` |
| 日志 | spdlog |
| 构建 | CMake + Ninja，MSVC 2022 |

## 两条链路

```text
采集：WM_INPUT 轻拷包 → 处理线程标准化 InputEvent → Recorder 归帧写入 data.db
展示：FrameAggregator 快照 → WebSocket → WebView rAF 渲染
```

Xbox 手柄走 XInput；路径带 `IG_` 的 HID 默认忽略，避免记两遍。键盘鼠标不要 `RIDEV_NOLEGACY`，否则 Qt 控件会收不到键。

## 存储

| 放哪 | 存什么 |
| --- | --- |
| SQLite `key_codes` | 数字键 + 线性轴，用户可注册 / 监听绑定 |
| SQLite `sessions` | 一场一行：`fps`、`device_bits`、名字、标签 |
| SQLite `frame_data` | 一行一帧：bitset + float32 |
| Profile JSON | 颜色、布局、导出 fps |
| app-config.json | 要录的设备、热键、当前 Profile |

通道顺序由 `RecLayout` + `device_bits` 决定。

## 运行时（Release）

```text
应用目录/
├─ PeripheralCapturer.exe
├─ app-config.json
├─ data.db
├─ log/
└─ backups/
```

## 版本计划

| 版本 | 目标 |
| --- | --- |
| v0.1 | 工程、spdlog、配置窗 / 码本 / 录制库空壳 |
| v0.2 | Raw Input 隐藏窗口，键盘鼠标 + XInput → InputEvent |
| v0.3 | Recorder 写 `frame_data`、快照 WebSocket、浮层 Vue |
| v0.4 | 非 Xbox HID、码本监听绑定 |
| v0.5 | 检查器、回放、导出、穿透热键 |
| v1.0 | 多设备、安装包 |

## 现在怎么编

Visual Studio 打开本目录，选 **x64-debug**，删除缓存并重新配置后生成。`windeployqt` 会把 Qt DLL 拷到 exe 旁。

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Qt 路径在顶层 `CMakeLists.txt` 的 `CMAKE_PREFIX_PATH`。Debug 会自动拉 `PeripheralCapturer/web` 的 Vite；也可手动 `npm run dev`。
