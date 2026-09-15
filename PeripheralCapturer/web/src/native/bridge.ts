/**
 * Native ↔ POV 约定：只走本机 WebSocket，不把 Qt 对象暴露给 JS。
 * 控制面 request/response；数据面 snapshot 可丢旧帧。
 */
export type OverlaySnapshot = {
  frameIndex: number;
  keys: string[];
  axes: Record<string, number>;
  mouseDx: number;
  mouseDy: number;
};

export type NativeHello = {
  type: "hello";
  token: string;
  wsUrl: string;
};

export type NativeSnapshot = {
  type: "snapshot";
  payload: OverlaySnapshot;
};

export type NativeMessage = NativeHello | NativeSnapshot;

export function defaultWsUrl(): string {
  const params = new URLSearchParams(window.location.search);
  return params.get("ws") ?? "ws://127.0.0.1:9123/?token=dev";
}

export function connectNative(
  onSnapshot: (snap: OverlaySnapshot) => void,
  onStatus: (text: string) => void,
): WebSocket {
  const url = defaultWsUrl();
  const socket = new WebSocket(url);
  socket.onopen = () => onStatus("已连接 " + url);
  socket.onclose = () => onStatus("未连接");
  socket.onerror = () => onStatus("连接失败");
  socket.onmessage = (ev) => {
    try {
      const msg = JSON.parse(String(ev.data)) as NativeMessage;
      if (msg.type === "snapshot") {
        onSnapshot(msg.payload);
      }
    } catch {
      /* 忽略非 JSON */
    }
  };
  return socket;
}
