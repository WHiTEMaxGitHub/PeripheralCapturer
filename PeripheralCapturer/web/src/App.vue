<script setup lang="ts">
import { onMounted, onUnmounted, ref } from "vue";
import PovHud from "./components/PovHud.vue";
import PovShell from "./components/PovShell.vue";
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
  <PovShell>
    <PovHud :status="status" :snap="snap" />
  </PovShell>
</template>
