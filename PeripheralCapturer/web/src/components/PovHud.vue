<script setup lang="ts">
defineProps<{
  status: string;
  keys: string[] | null;
}>();
</script>

<template>
  <div class="hud">
    <div class="title">POV</div>
    <div class="status">{{ status }}</div>
    <div v-if="keys" class="keys">{{ keys.join(" ") || "（无按下）" }}</div>
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
