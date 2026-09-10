# POV 前端（Vue）

配置窗是 Qt Widgets。**POV 是独立 Web 应用**：开发用 Vue 3 + Vite，Native 只当浏览器壳 + 本机接口。

## 怎么跑

```bat
cd PeripheralCapturer\web
npm install
npm run dev
```

然后启动 C++ 程序。Debug 下 POV 打开 `http://127.0.0.1:5173`。没开 Vite 时会落到 `qrc` 里的离线提示页。

发布：`npm run build`，再把 `dist/` 打进资源或放到 exe 旁（尚未接进 CMake）。

## Native ↔ 页面

不要用 QWebChannel 把 C++ 对象塞进 JS。约定：

| | 通道 | 形态 |
| --- | --- | --- |
| 数据面 | `ws://127.0.0.1:<port>/?token=...` | Native 推 `snapshot`，可丢旧帧 |
| 控制面 | 同一条 WS 上的 request/response | 以后：穿透开关、皮肤名等 |

页面用 `?ws=` 覆盖地址，默认 `ws://127.0.0.1:9123/?token=dev`。

快照形状（可随实现微调，但不要改成全量 InputEvent）：

```json
{
  "type": "snapshot",
  "payload": {
    "frameIndex": 12,
    "keys": ["Key_W", "Pad_A"],
    "axes": { "LeftThumbX": 0.2, "LeftTrigger": 0.0 },
    "mouseDx": 0,
    "mouseDy": 0
  }
}
```

JS 只缓存最新一份，用 `requestAnimationFrame` 画。键位皮肤、颜色仍来自 Profile JSON（以后通过控制面或启动参数下发），不在 Vue 里写死码本。

C++ WebSocket 服务尚未实现；现在页面会显示未连接，这是预期。

## 怎么画（视觉状态）

数字键的按下/松开必须跟当前 snapshot **瞬时对齐**。禁止用 CSS `transition`、透明度淡入淡出、颜色 lerp、弹性缓动等去「补间」按键外观。那些渐变会自带一套绘制状态机，和真实 `keys[]` 脱节（松开了还在亮、连点看起来黏在一起、回放对不上帧）。

线性控件（`value_kind = analog`）可以映射成**坐标轴形状**的部件，而不是再做成一颗数字键。典型：

- 左右扳机 → 油门 / 刹车的 **F–t 图**（纵轴为归一化开度或力，横轴为最近一段时间；点来自连续 snapshot，不是 CSS 动画假装出来的曲线）
- 摇杆 → 二维十字/圆盘，当前位置 = 当前 `axes` 值

画哪种部件由 **Profile** 决定（同一条 `key_id` 可以是条、盘或 F–t），不改码本、不改 `.bin`。F–t 若需要短时历史，只在 JS 里保留一小段 snapshot 环形缓冲；不要为此让 Native 推全量事件。

装饰性渐变填充（背景、条的配色）可以有；禁止的是**会改变「现在算不算按下 / 轴在哪」的过渡动画**。
