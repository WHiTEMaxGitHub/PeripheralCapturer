<script setup lang="ts">
const props = defineProps<{
  pressed: string[];
}>();

const cluster = [
  { id: "w", label: "W", col: 2, row: 1 },
  { id: "a", label: "A", col: 1, row: 2 },
  { id: "s", label: "S", col: 2, row: 2 },
  { id: "d", label: "D", col: 3, row: 2 },
] as const;

const arrows = [
  { id: "arrow-up", label: "↑", col: 2, row: 1 },
  { id: "arrow-left", label: "←", col: 1, row: 2 },
  { id: "arrow-down", label: "↓", col: 2, row: 2 },
  { id: "arrow-right", label: "→", col: 3, row: 2 },
] as const;

function down(id: string) {
  return props.pressed.includes(id);
}
</script>

<template>
  <div class="clusters">
    <div class="pad" aria-label="WASD">
      <button
        v-for="key in cluster"
        :key="key.id"
        class="cell"
        :class="{ on: down(key.id) }"
        :style="{ gridColumn: key.col, gridRow: key.row }"
        type="button"
        tabindex="-1"
      >
        {{ key.label }}
      </button>
    </div>
    <div class="pad" aria-label="arrows">
      <button
        v-for="key in arrows"
        :key="key.id"
        class="cell"
        :class="{ on: down(key.id) }"
        :style="{ gridColumn: key.col, gridRow: key.row }"
        type="button"
        tabindex="-1"
      >
        {{ key.label }}
      </button>
    </div>
  </div>
</template>

<style scoped>
.clusters {
  display: flex;
  gap: 10px;
}
.pad {
  display: grid;
  grid-template-columns: repeat(3, 28px);
  grid-template-rows: repeat(2, 28px);
  gap: 3px;
}
.cell {
  margin: 0;
  padding: 0;
  border: 1px solid rgba(255, 255, 255, 0.25);
  border-radius: 4px;
  background: rgba(0, 0, 0, 0.45);
  color: #cfd8dc;
  font: 700 12px/28px "Segoe UI", sans-serif;
  text-align: center;
  pointer-events: none;
}
.cell.on {
  background: #00e5ff;
  color: #00343a;
  border-color: #00e5ff;
}
</style>
