# AGENTS.md

给在本仓库改代码的人（含 AI）。产品设计以 `docs/` 为准，这里只定**怎么写代码**。对用户说话用简体中文。

## 栈与范围

- 仅 Windows 10/11 x64，C++20，Qt 6（Widgets + WebView2），CMake + MSVC。
- 不要跨平台捕获、不要 SDL、键盘鼠标不要 `RIDEV_NOLEGACY`。
- 源码在 `PeripheralCapturer/`。先读再改；不要顺手大重构、不要无关文件。
- 未要求不要 `git commit` / `push`。不要提交 `out/`、`.vs/`、密钥。

## 注释

该加就加，用简体中文。写**为什么**和易踩的坑，不要复述函数名。

该加：窗口归属、热路径禁令、编码/BLOB 约定、队列丢弃策略、和 `docs/` 不一致时的取舍。  
不该加：逐行翻译、无信息的 `// 打开数据库`。

头文件里用一两句说明模块职责即可；实现细节放 cpp。

## 日志（spdlog）

前缀：`[app]` `[log]` `[ui]` `[timer]` `[capture]` `[pov]` `[registry]` `[pad]` `[bus]` `[db]`。

| 打 | 不打 |
| --- | --- |
| 初始化成功 `info` | `WM_INPUT` / 包队列 / 每条 `InputEvent` |
| 开停录、首次绑码、schema | 查表命中、逐帧 BLOB |
| 失败 `warn` / `error` / `critical` | 热路径 JSON、每包 `spdlog` |

能继续跑用 `warn`；初始化失败要停用 `critical`。看丢包用队列的 `dropped()`，不要靠日志。

## 三扇窗（互不嵌套）

| 窗 | 职责 |
| --- | --- |
| 配置 `Layout/MainWindow` | Widgets：设备 / 码本 / Profile / 录制库 |
| POV `overlay/PovWindow` | 独立 `QWebView`，只画降频快照 |
| 捕获 `capture/HiddenCaptureWindow` | `HWND_MESSAGE`，收 `WM_INPUT` |

不要把 POV parent 到配置窗（会白屏盖住 UI）。捕获不要用配置窗 `winId()`。POV 与 Native 用本机 WebSocket，不要 QWebChannel 把 C++ 暴露给 JS。

## 热路径

```
WM_INPUT：拷包 + 时间戳 + tryPush → 立刻返回
禁止：wait、解析 HID、写盘、SQLite、spdlog、Qt 信号
```

处理线程变成 `InputEvent` 并 `bus.publish`。录制队列 `Block`；捕获 `tryPush` DropOldest；浮层可丢。浮层不要订全量事件，尤其不要全量 `MouseMove`。HUD：数字键禁止按下渐变；模拟轴可画成 F–t / 摇杆盘（Profile）。控制热键在 **publish 前**拦掉。

## 存储

- SQLite 一份 `data.db`（和 exe 同目录）。录制是 `frame_data`：一行一帧，`blob` 前半 bitset、后半 float32。内存攒批再事务写入。
- 开录写入 `sessions.device_bits`；通道顺序是 `RecLayout`。
- Profile / app-config：JSON。

查表用 `findByNativeVk` 等；热路径不要 `INSERT`。

## 输入语义（摘要）

- 鼠标 `dx`/`dy` 永远是相对量（绝对 Raw Input 先差分）。
- 键盘：Down/Up，丢掉连发；填 vkey / scanCode / extended。
- Xbox / `IG_` HID → XInput，HID 侧丢掉，避免 A 键两遍。
- `hDevice` 只运行期用，不要 `CloseHandle`。
- 本场 fps 开录时冻结；`MakeBaseEvent` 用包时间戳 + 本场 fps，不要写死 60。

## 增量

先对照 `docs/TODO.md` 和专项文档。用户没点名的大块（Recorder、WS）不要自行铺开。改完相关行为：初始化/失败分支要有日志；非显然逻辑要有注释。
