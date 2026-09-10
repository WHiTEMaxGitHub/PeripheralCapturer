<script setup lang="ts">
import { onMounted, onUnmounted, ref } from "vue";
import { connectNative, type OverlaySnapshot } from "./native/bridge";

const status = ref("正在连接 Native…");
const snap = ref<OverlaySnapshot | null>(null);
let socket: WebSocket | null = null;

onMounted(() => {
  socket = connectNative(
    (payload) => {
      snap.value = payload;
    },
    (text) => {
      status.value = text;
    },
  );
});

onUnmounted(() => {
  socket?.close();
});
</script>

<template>
  <div class="hud">
    <div class="title">POV</div>
    <div class="status">{{ status }}</div>
    <div v-if="snap" class="keys">
      {{ snap.keys.join(" ") || "（无按下）" }}
    </div>
  </div>
</template>

<style scoped>
.hud {
  position: absolute;
  top: 24px;
  left: 24px;
  padding: 10px 14px;
  background: rgba(0, 0, 0, 0.5);
  border-radius: 8px;
  color: #fff;
  font: 14px/1.4 "Segoe UI", sans-serif;
  pointer-events: none;
  min-width: 160px;
}
.title {
  font-weight: 700;
  letter-spacing: 0.08em;
}
.status {
  margin-top: 4px;
  font-size: 12px;
  opacity: 0.75;
}
.keys {
  margin-top: 6px;
  font-family: ui-monospace, Consolas, monospace;
}
</style>
