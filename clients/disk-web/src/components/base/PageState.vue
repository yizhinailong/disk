<template>
  <div class="page-state">
    <div v-if="state === 'loading'" class="page-state__loading">
      <el-skeleton :rows="3" animated />
    </div>

    <div v-else-if="state === 'empty'" class="page-state__empty">
      <el-empty :description="emptyText" />
    </div>

    <div v-else-if="state === 'error'" class="page-state__error">
      <el-icon :size="48" class="page-state__error-icon">
        <CircleCloseFilled />
      </el-icon>
      <p class="page-state__error-text">{{ errorText }}</p>
      <el-button type="primary" @click="emit('retry')">重试</el-button>
    </div>

    <slot v-else />
  </div>
</template>

<script setup lang="ts">
import { CircleCloseFilled } from '@element-plus/icons-vue'

defineProps<{
  /** 页面状态 */
  state: 'loading' | 'empty' | 'error' | 'content'
  /** 空状态提示文案 */
  emptyText?: string
  /** 错误状态提示文案 */
  errorText?: string
}>()

const emit = defineEmits<{
  /** 错误状态下点击重试按钮 */
  retry: []
}>()
</script>

<style scoped>
.page-state {
  width: 100%;
}

.page-state__loading {
  padding: 24px;
}

.page-state__empty {
  display: flex;
  justify-content: center;
  align-items: center;
  padding: 48px 24px;
}

.page-state__error {
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  padding: 48px 24px;
  gap: 12px;
}

.page-state__error-icon {
  color: var(--el-color-danger);
}

.page-state__error-text {
  margin: 0;
  color: var(--el-text-color-secondary);
  font-size: 14px;
}
</style>
