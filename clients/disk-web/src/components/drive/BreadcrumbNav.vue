<template>
  <el-breadcrumb separator="/" class="drive-breadcrumb">
    <!-- Root: 我的文件 -->
    <el-breadcrumb-item
      :class="{ 'is-current': isRoot }"
      @click.prevent="handleClick(0)"
    >
      <span class="breadcrumb-item breadcrumb-item--root">
        <el-icon class="breadcrumb-icon"><HomeFilled /></el-icon>
        <span>我的文件</span>
      </span>
    </el-breadcrumb-item>

    <!-- Path items -->
    <el-breadcrumb-item
      v-for="(item, index) in breadcrumbs"
      :key="item.id"
      :class="{ 'is-current': index === breadcrumbs.length - 1 }"
      @click.prevent="handleClick(item.id, index)"
    >
      <span class="breadcrumb-item">{{ item.name }}</span>
    </el-breadcrumb-item>
  </el-breadcrumb>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { HomeFilled } from '@element-plus/icons-vue'
import { useDriveStore } from '@/stores/drive'

const driveStore = useDriveStore()

const breadcrumbs = computed(() => driveStore.breadcrumbs)
const isRoot = computed(() => driveStore.currentFolderId === 0)

function handleClick(folderId: number, index?: number) {
  // Don't navigate if clicking the current (last) breadcrumb
  if (index !== undefined && index === breadcrumbs.value.length - 1) {
    return
  }
  // Don't re-navigate to current root
  if (folderId === 0 && driveStore.currentFolderId === 0) {
    return
  }

  driveStore.navigateToFolder(folderId)
}
</script>

<style scoped>
.drive-breadcrumb {
  font-size: 14px;
  line-height: 1;
}

/* Reset el-breadcrumb-item to allow custom click handling */
.drive-breadcrumb :deep(.el-breadcrumb__inner) {
  cursor: pointer;
  transition: color 0.15s;
}

.drive-breadcrumb :deep(.el-breadcrumb__inner.is-link) {
  font-weight: normal;
}

/* Hover state for clickable items */
.drive-breadcrumb :deep(.el-breadcrumb__inner:hover) {
  color: #409eff;
}

/* Current (last) item styling */
.drive-breadcrumb :deep(.el-breadcrumb__item.is-current .el-breadcrumb__inner) {
  color: #303133;
  font-weight: 500;
  cursor: default;
}

.drive-breadcrumb :deep(.el-breadcrumb__item.is-current .el-breadcrumb__inner:hover) {
  color: #303133;
}

/* Non-current clickable items */
.drive-breadcrumb :deep(.el-breadcrumb__item:not(.is-current) .el-breadcrumb__inner) {
  color: #606266;
}

.drive-breadcrumb :deep(.el-breadcrumb__item:not(.is-current) .el-breadcrumb__inner:hover) {
  color: #409eff;
}

/* Separator color */
.drive-breadcrumb :deep(.el-breadcrumb__separator) {
  color: #c0c4cc;
}

/* ==================== Breadcrumb Item ==================== */
.breadcrumb-item {
  display: inline-flex;
  align-items: center;
  gap: 4px;
}

.breadcrumb-icon {
  font-size: 14px;
}
</style>
