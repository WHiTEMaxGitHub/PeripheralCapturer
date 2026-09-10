<script setup lang="ts">
defineSlots<{
  default(): unknown;
}>();
</script>

<template>
  <div class="shell">
    <div class="frame" aria-hidden="true" />
    <div class="slot">
      <slot />
    </div>
  </div>
</template>

<style scoped>
.shell {
  position: fixed;
  inset: 0;
  background: transparent;
  pointer-events: none;
}
.frame {
  position: absolute;
  inset: 0;
  box-sizing: border-box;
  border: 3px solid #00e5ff;
  pointer-events: none;
}
.slot {
  position: absolute;
  inset: 0;
}
</style>
