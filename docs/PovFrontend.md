# POV 前端（Vue）

配置窗是 Qt Widgets。**POV 是独立 Web 应用**：开发用 Vue 3 + Vite，Native 只当浏览器壳 + 本机接口。

## 怎么跑

Debug 构建会自己找 `PeripheralCapturer/web`、必要时 `npm install`，再 `npm run dev`。`127.0.0.1:5173` 已经在听就复用，退出不杀。本机需要已安装 Node.js（`npm.cmd` 在 PATH 里）。

仍可手动：

```bat
cd PeripheralCapturer\web
npm install
npm run dev
```

POV 打开 `http://127.0.0.1:5173/?ws=...`（Native 把本机 WS 地址塞进 query），边框和 HUD 是 Vue 组件（`PovShell` / `PovHud` / `KeyCluster` / `AxisPlot` / `StickPad`），不要在 C++ 里 `runJavaScript` 拼 DOM。Vite 没起来时落到 `qrc` 离线提示页（没有 Vue）。Release **不**自动拉 Vite。

发布：`npm run build`，再把 `dist/` 打进资源或放到 exe 旁（尚未接进 CMake）。

## Native ↔ 页面

不要用 QWebChannel 把 C++ 对象塞进 JS。约定：

| | 通道 | 形态 |
| --- | --- | --- |
| 数据面 | `ws://127.0.0.1:<port>/?token=...` | Native 推 `snapshot`，可丢旧帧 |
| 控制面 | 同一条 WS 上的 request/response | 以后：穿透开关、皮肤名等 |

页面用 `?ws=` 覆盖地址。Native 监听 `127.0.0.1` 随机端口并带 token；没有 query 时默认 `ws://127.0.0.1:9123/?token=dev`（只方便浏览器里单独打开 Vite）。

快照形状（可随实现微调，但不要改成全量 InputEvent）。`keys` / `axes` 的 id 与 `InputEvent.control` 相同（`w`、`mouse-left`、`pad-a`、`pad-lx`），不要 `Key_W` 那种别名：

```json
{
  "type": "snapshot",
  "payload": {
    "frameIndex": 12,
    "keys": ["w", "mouse-left", "pad-a"],
    "axes": { "pad-lx": 0.2, "pad-ly": -0.1, "pad-lt": 0.4, "pad-rt": 0.0 },
    "mouseDx": 0,
    "mouseDy": 0
  }
}
```

JS 只缓存最新一份 snapshot。F–t 短时历史只在 JS 环形缓冲。本轮 HUD 默认组装：

- `KeyCluster`：WASD + 方向键位图，按下瞬时点亮
- `KeyCap`：不在位图里的其它按下键
- `AxisPlot`：一张图多条序列，默认 `pad-lt` / `pad-rt`
- `StickPad`：一组 xy，默认左右摇杆各一份

键位皮肤、颜色仍来自 Profile JSON（以后通过控制面或启动参数下发），不在 Vue 里写死码本。

## 怎么画（视觉状态）

数字键的按下/松开必须跟当前 snapshot **瞬时对齐**。禁止用 CSS `transition`、透明度淡入淡出、颜色 lerp、弹性缓动等去「补间」按键外观。那些渐变会自带一套绘制状态机，和真实 `keys[]` 脱节（松开了还在亮、连点看起来黏在一起、回放对不上帧）。

线性控件（`value_kind = analog`）可以映射成**坐标轴形状**的部件，而不是再做成一颗数字键。典型：

- 左右扳机 → 油门 / 刹车的 **F–t 图**（纵轴为归一化开度或力，横轴为最近一段时间；点来自连续 snapshot，不是 CSS 动画假装出来的曲线）
- 摇杆 → 二维十字/圆盘，当前位置 = 当前 `axes` 值

画哪种部件由 **Profile** 决定（同一条 `key_id` 可以是条、盘或 F–t），不改码本、不改 `frame_data`。F–t 若需要短时历史，只在 JS 里保留一小段 snapshot 环形缓冲；不要为此让 Native 推全量事件。以后每个控件做成独立 Vue 组件，由 Profile 组装，不要在 `index.html` 里堆 DOM。

装饰性渐变填充（背景、条的配色）可以有；禁止的是**会改变「现在算不算按下 / 轴在哪」的过渡动画**。
