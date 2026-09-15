<script setup lang="ts">
export type AxisSeries = {
  id: string;
  color: string;
  values: number[];
};

defineProps<{
  series: AxisSeries[];
}>();

const w = 160;
const h = 52;

function points(values: number[]) {
  const n = Math.max(values.length, 1);
  return values
    .map((v, i) => {
      const x = n === 1 ? 0 : (i / (n - 1)) * w;
      const y = h - Math.min(1, Math.max(0, v)) * h;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(" ");
}
</script>

<template>
  <svg class="plot" :viewBox="`0 0 ${w} ${h}`" preserveAspectRatio="none">
    <line class="axis" :x1="0" :y1="h" :x2="w" :y2="h" />
    <line class="axis" :x1="0" :y1="0" :x2="0" :y2="h" />
    <polyline
      v-for="item in series"
      :key="item.id"
      class="line"
      :stroke="item.color"
      :points="points(item.values)"
    />
  </svg>
</template>

<style scoped>
.plot {
  display: block;
  width: 160px;
  height: 52px;
  background: rgba(0, 0, 0, 0.35);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 4px;
}
.axis {
  stroke: rgba(255, 255, 255, 0.25);
  stroke-width: 1;
}
.line {
  fill: none;
  stroke-width: 1.5;
  stroke-linejoin: round;
  stroke-linecap: round;
}
</style>
