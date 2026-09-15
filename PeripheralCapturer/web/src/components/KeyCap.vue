<script setup lang="ts">
defineProps<{
  label: string;
}>();
</script>

<template>
  <span class="cap">{{ label }}</span>
</template>

<style scoped>
.cap {
  display: inline-block;
  margin: 0 4px 4px 0;
  padding: 2px 7px;
  border: 1px solid #00e5ff;
  border-radius: 4px;
  background: #00e5ff;
  color: #00343a;
  font: 700 11px/1.3 ui-monospace, Consolas, monospace;
}
</style>
