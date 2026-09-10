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
