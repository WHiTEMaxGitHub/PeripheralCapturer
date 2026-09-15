<script setup lang="ts">
const props = withDefaults(
  defineProps<{
    x?: number;
    y?: number;
    label?: string;
  }>(),
  { x: 0, y: 0, label: "" },
);

const size = 64;
const r = 26;
const cx = size / 2;
const cy = size / 2;

function clamp(v: number) {
  return Math.min(1, Math.max(-1, v));
}

function dotX() {
  return cx + clamp(props.x) * r;
}

function dotY() {
    // 归一化 Y 上为正；SVG 的 Y 向下。
    return cy - clamp(props.y) * r;
}
</script>

<template>
  <div class="stick">
    <svg :width="size" :height="size" :viewBox="`0 0 ${size} ${size}`">
      <circle class="ring" :cx="cx" :cy="cy" :r="r" />
      <line class="cross" :x1="cx - r" :y1="cy" :x2="cx + r" :y2="cy" />
      <line class="cross" :x1="cx" :y1="cy - r" :x2="cx" :y2="cy + r" />
      <circle class="dot" :cx="dotX()" :cy="dotY()" r="5" />
    </svg>
    <div v-if="label" class="name">{{ label }}</div>
  </div>
</template>

<style scoped>
.stick {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
}
.ring {
  fill: rgba(0, 0, 0, 0.35);
  stroke: rgba(255, 255, 255, 0.45);
  stroke-width: 1.5;
}
.cross {
  stroke: rgba(255, 255, 255, 0.2);
  stroke-width: 1;
}
.dot {
  fill: #00e5ff;
  stroke: none;
}
.name {
  font: 10px/1 "Segoe UI", sans-serif;
  color: rgba(255, 255, 255, 0.7);
}
</style>
