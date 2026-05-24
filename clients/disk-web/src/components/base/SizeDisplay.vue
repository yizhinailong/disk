<template>
  <span class="size-display">{{ formatted }}</span>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = withDefaults(defineProps<{
  /** 字节数 */
  bytes: number
  /** 小数精度 */
  precision?: number
}>(), {
  precision: 2,
})

const formatted = computed(() => {
  if (props.bytes < 0) return '0 B'

  const units = ['B', 'KB', 'MB', 'GB', 'TB'] as const
  let value = props.bytes
  let unitIndex = 0

  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024
    unitIndex++
  }

  if (unitIndex === 0) {
    return `${Math.round(value)} B`
  }

  return `${value.toFixed(props.precision)} ${units[unitIndex]}`
})
</script>

<style scoped>
.size-display {
  font-variant-numeric: tabular-nums;
}
</style>
