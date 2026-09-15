# TODO

对照现状：三扇窗壳在、采集链路（键鼠 + XInput）能出 `InputEvent`、schema 4 建表、设备页能勾本场要录的设备、POV 能开 WebView2。下面按依赖顺序排，做完再勾。

## 已有（不必再做一遍）

- [x] CMake + spdlog + 配置窗五页空壳
- [x] `InputEvent` / `Timer` / `DeviceRegistry` / 有界队列 / `InputEventBus`
- [x] 隐藏捕获窗 `HWND_MESSAGE`，已注册键鼠 Raw Input
- [x] POV：`QWebView` + Vue/Vite 工程 + 本机 WS 约定文档
- [x] 设备页：XInput 槽位 + HID 手柄枚举（1Hz，不采按键）
- [x] 日志前缀与启动/失败分支
- [x] 配置窗 Debug 页：重建 `data.db`、恢复 `app-config`、采集计数、POV 穿透切换
- [x] Debug 自动拉起 Vite（`npm run dev`，已占用 5173 则复用）

## 下一步（建议按此顺序）

### 1. 采集真正跑起来（v0.2）

- [x] 捕获窗 `RegisterRawInputDevices`：键 `0x06`、鼠 `0x02`，`RIDEV_INPUTSINK`，**不要** `RIDEV_NOLEGACY`
- [x] `WM_INPUT`：`GetRawInputData` → `RawInputPacket` + `Timer::nowUs()` → 包队列 `tryPush`（WndProc 禁止 wait / 解析 / 写盘）
- [x] 处理线程：`pop` 包 → `DeviceRegistry` → 差分 → `InputEvent` → `bus.publish`
- [x] 键盘：Down/Up，丢掉连发；填 `vkey` / `scanCode` / `extended`
- [x] 鼠标：Move 只发相对 `dx/dy`（绝对包先差分）；键与滚轮
- [x] `Timer::MakeBaseEvent` 用包上的时间戳 + **本场** fps，不要写死 60、不要再 `nowUs()` 一次
- [x] Timer 状态放到 cpp（避免头文件 `static` 每 TU 一份）
- [x] 插拔：`WM_INPUT_DEVICE_CHANGE`，Registry `erase` + `DeviceDisconnected`

### 2. 录制写入（v0.3 前半）

- [x] SQLite 框架：`storage/Database` 建表、builtin 码本、`device_bits` 决定帧宽、帧 BLOB 批量写入（热路径不 INSERT）
- [ ] 开录时冻结 `sessions.fps` 与 `device_bits`（API 已有，等 Recorder / 热键接上）
- [ ] Recorder 订总线归并冻通道帧，`FrameBatchWriter` 按帧写入 `frame_data`（`frame` + `blob`）
- [ ] 控制热键在 publish 前拦掉，不进按键流

### 3. POV 真能画（v0.3 后半）

- [ ] Native 本机 WebSocket（`127.0.0.1` + token）
- [ ] `FrameAggregator`：事件归并快照，可丢旧帧；**不**把全量 InputEvent 给 JS
- [ ] Vue 接 `bridge.ts` 画按下键 / 轴；开发 Vite，发布打 `dist`
- [ ] 穿透热键：`PovWindow::setClickThrough` 在可点 / 穿透间切换
- [ ] POV 正式全屏；配置时不要盖住主窗（现在右上角预览可先留着）

### 4. 手柄走同一条事件流（v0.4）

- [x] XInput ~250Hz 轮询差分 → 同一 `InputEventBus`（不要再只给设备列表用）
- [x] `IG_` HID 丢弃，避免 A 键两遍（本步不注册 HID Usage；误入的 HID 包仍走 `shouldIgnoreRawHid`）
- [ ] 非 Xbox HID：`HidP_*` 解析 Button/Axis/Hat，细类写入 `hidKind`
- [x] 轴死区 / ε（XInput 摇杆/扳机）；HID 轴仍等 `HidP_*`

### 5. 配置产品（可与 2–4 交错）

- [ ] 码本页：列表 / 注册 / 监听绑定 `native_*`
- [ ] Profile JSON：颜色、布局、`recording.defaultFps`、`export.fps`
- [x] `app-config.json`：本场要录的设备（键盘 / 鼠标 / 手柄），配置窗勾选；开录再冻 `deviceBits`
- [ ] 录制库：列表、改名、标签、删除、检查器

### 6. 后话（v0.5–v1.0）

- [ ] 回放、JSON/CSV 导出
- [ ] `.pcrec` 备份导入导出
- [ ] 安装包 / `windeployqt` 发布检查（WebView2 Runtime）
- [ ] 可选 overlay 视频

## 明确不做

- 跨平台捕获后端、SDL、`RIDEV_NOLEGACY`
- QWebChannel 把 C++ 对象暴露给 Vue
- 热路径逐事件 `spdlog`

设计细节仍以 [ArchitectureDesign](ArchitectureDesign.md) 和各专项文档为准。
