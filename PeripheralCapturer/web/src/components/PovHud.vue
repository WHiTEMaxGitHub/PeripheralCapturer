<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import type { OverlaySnapshot } from "../native/bridge";
import AxisPlot from "./AxisPlot.vue";
import KeyCap from "./KeyCap.vue";
import KeyCluster from "./KeyCluster.vue";
import StickPad from "./StickPad.vue";

const props = defineProps<{
  status: string;
  snap: OverlaySnapshot | null;
}>();

const clusterIds = new Set([
  "w",
  "a",
  "s",
  "d",
  "arrow-up",
  "arrow-left",
  "arrow-down",
  "arrow-right",
]);

const historyLen = 90;
const ltHist = ref<number[]>(Array(historyLen).fill(0));
const rtHist = ref<number[]>(Array(historyLen).fill(0));

let raf = 0;
onMounted(() => {
  // Native 无变化就不推快照。F–t 按固定时间采样当前值，松开扳机轴也要往前走。
  let last = 0;
  const tick = (now: number) => {
    if (now - last >= 16) {
      last = now;
      const axesNow = props.snap?.axes ?? {};
      ltHist.value = ltHist.value.slice(1).concat(axesNow["pad-lt"] ?? 0);
      rtHist.value = rtHist.value.slice(1).concat(axesNow["pad-rt"] ?? 0);
    }
    raf = requestAnimationFrame(tick);
  };
  raf = requestAnimationFrame(tick);
});
onUnmounted(() => {
  cancelAnimationFrame(raf);
});

const keys = computed(() => props.snap?.keys ?? []);
const extra = computed(() => keys.value.filter((id) => !clusterIds.has(id)));
const axes = computed(() => props.snap?.axes ?? {});

const series = computed(() => [
  { id: "pad-lt", color: "#ff6b6b", values: ltHist.value },
  { id: "pad-rt", color: "#00e5ff", values: rtHist.value },
]);
</script>

<template>
  <div class="hud">
    <div class="title">POV</div>
    <div class="status">{{ status }}</div>
    <div class="row">
      <KeyCluster :pressed="keys" />
      <StickPad :x="axes['pad-lx'] ?? 0" :y="axes['pad-ly'] ?? 0" label="LS" />
      <StickPad :x="axes['pad-rx'] ?? 0" :y="axes['pad-ry'] ?? 0" label="RS" />
    </div>
    <div v-if="extra.length" class="caps">
      <KeyCap v-for="id in extra" :key="id" :label="id" />
    </div>
    <AxisPlot :series="series" />
  </div>
</template>

<style scoped>
.hud {
  position: absolute;
  top: 12px;
  left: 14px;
  color: #fff;
  font: 14px/1.4 "Segoe UI", sans-serif;
  text-shadow: 0 1px 3px #000;
  pointer-events: none;
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
.row {
  display: flex;
  align-items: flex-end;
  gap: 10px;
  margin-top: 8px;
}
.caps {
  margin-top: 6px;
  max-width: 360px;
}
</style>
