<template>
  <el-icon :size="size" :style="iconStyle" class="file-icon">
    <component :is="iconComponent" />
  </el-icon>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import {
  Picture,
  VideoPlay,
  Headset,
  Document,
  Folder,
  Files,
} from '@element-plus/icons-vue'

const props = withDefaults(defineProps<{
  /** 文件 MIME 类型 */
  mimeType?: string
  /** 是否为文件夹 */
  isFolder?: boolean
  /** 图标大小（px） */
  size?: number
}>(), {
  mimeType: '',
  isFolder: false,
  size: 32,
})

type IconDef = {
  component: typeof Document
  color: string
}

const iconMap = computed<IconDef>(() => {
  if (props.isFolder) {
    return { component: Folder, color: '#e6a23c' }
  }

  const mime = props.mimeType.toLowerCase()

  if (mime.startsWith('image/')) {
    return { component: Picture, color: '#409eff' }
  }
  if (mime.startsWith('video/')) {
    return { component: VideoPlay, color: '#9b59b6' }
  }
  if (mime.startsWith('audio/')) {
    return { component: Headset, color: '#e67e22' }
  }
  if (mime === 'application/pdf') {
    return { component: Document, color: '#e74c3c' }
  }
  if (
    mime === 'application/zip' ||
    mime.startsWith('application/x-rar') ||
    mime.startsWith('application/x-7z') ||
    mime.startsWith('application/x-tar') ||
    mime.startsWith('application/gzip')
  ) {
    return { component: Files, color: '#f1c40f' }
  }
  if (mime.startsWith('application/vnd.')) {
    return { component: Document, color: '#409eff' }
  }
  if (mime.startsWith('text/')) {
    return { component: Document, color: '#909399' }
  }

  return { component: Document, color: '#909399' }
})

const iconComponent = computed(() => iconMap.value.component)

const iconStyle = computed(() => ({
  color: iconMap.value.color,
}))
</script>

<style scoped>
.file-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}
</style>
