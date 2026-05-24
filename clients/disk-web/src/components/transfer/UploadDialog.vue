<template>
  <el-dialog
    :model-value="visible"
    title="上传文件"
    width="620px"
    :close-on-click-modal="false"
    destroy-on-close
    @update:model-value="$emit('update:visible', $event)"
    @close="onClose"
  >
    <!-- Drop Zone -->
    <div
      class="upload-drop-zone"
      :class="{ 'is-dragover': isDragover }"
      @dragenter.prevent="isDragover = true"
      @dragover.prevent="isDragover = true"
      @dragleave.prevent="isDragover = false"
      @drop.prevent="onDrop"
      @click="triggerFileInput"
    >
      <el-icon class="drop-zone-icon" :size="40">
        <Upload />
      </el-icon>
      <p class="drop-zone-text">拖拽文件到此处，或<span class="drop-zone-link">点击选择</span></p>
      <p class="drop-zone-hint">支持多文件上传，单个分片大小 5 MB</p>
      <input
        ref="fileInputRef"
        type="file"
        multiple
        class="file-input-hidden"
        @change="onFileInputChange"
      />
    </div>

    <!-- Task List -->
    <div v-if="transferStore.uploads.length > 0" class="upload-task-list">
      <div class="task-list-header">
        <span>上传列表（{{ transferStore.uploads.length }}）</span>
        <el-button
          v-if="hasCompletedOrFailed"
          link
          type="primary"
          size="small"
          @click="transferStore.clearCompletedTasks()"
        >
          清除已完成
        </el-button>
      </div>

      <div class="task-items">
        <div
          v-for="task in transferStore.uploads"
          :key="task.id"
          class="task-item"
        >
          <div class="task-info">
            <span class="task-name" :title="task.file_name">{{ task.file_name }}</span>
            <span class="task-meta">
              <SizeDisplay :bytes="task.file_size" />
              <span class="task-status-dot" :class="`status-${task.status}`"></span>
              <span class="task-status-text">{{ statusLabel(task.status) }}</span>
            </span>
          </div>

          <el-progress
            :percentage="task.progress"
            :status="progressStatus(task.status)"
            :stroke-width="6"
            :show-text="false"
            class="task-progress"
          />

          <div class="task-actions">
            <el-button
              v-if="task.status === 'failed'"
              link
              type="primary"
              size="small"
              @click="transferStore.retryUploadTask(task.id)"
            >
              重试
            </el-button>
            <el-button
              v-if="canCancel(task.status)"
              link
              type="danger"
              size="small"
              @click="transferStore.cancelUploadTask(task.id)"
            >
              取消
            </el-button>
            <el-button
              v-if="isRemovable(task.status)"
              link
              size="small"
              @click="transferStore.removeUploadTask(task.id)"
            >
              <el-icon><Close /></el-icon>
            </el-button>
          </div>
        </div>
      </div>
    </div>

    <template #footer>
      <el-button @click="$emit('update:visible', false)">关闭</el-button>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { Upload, Close } from '@element-plus/icons-vue'
import { useTransferStore, useDriveStore } from '@/stores'
import SizeDisplay from '@/components/base/SizeDisplay.vue'
import type { UploadTaskStatus } from '@/types'

defineProps<{
  visible: boolean
}>()

const emit = defineEmits<{
  (e: 'update:visible', val: boolean): void
  (e: 'uploaded'): void
}>()

const transferStore = useTransferStore()
const driveStore = useDriveStore()

const fileInputRef = ref<HTMLInputElement | null>(null)
const isDragover = ref(false)

const hasCompletedOrFailed = computed(() =>
  transferStore.uploads.some(
    (t) => t.status === 'completed' || t.status === 'failed' || t.status === 'cancelled',
  ),
)

// Watch for all tasks completing to emit uploaded event
const allCompleted = computed(() => {
  const tasks = transferStore.uploads
  return tasks.length > 0 && tasks.every((t) => t.status === 'completed' || t.status === 'cancelled')
})

watch(allCompleted, (done, prev) => {
  if (done && !prev) {
    emit('uploaded')
    driveStore.refreshCurrentView()
  }
})

function triggerFileInput(): void {
  fileInputRef.value?.click()
}

function addFiles(files: FileList | File[]): void {
  if (!files || files.length === 0) return

  const parentId = driveStore.currentFolderId
  for (const file of files) {
    transferStore.addUploadTask(file, parentId)
  }
}

function onFileInputChange(event: Event): void {
  const input = event.target as HTMLInputElement
  if (input.files) {
    addFiles(Array.from(input.files))
    input.value = ''
  }
}

function onDrop(event: DragEvent): void {
  isDragover.value = false
  if (event.dataTransfer?.files) {
    addFiles(event.dataTransfer.files)
  }
}

function onClose(): void {
  isDragover.value = false
}

const STATUS_LABELS: Record<UploadTaskStatus, string> = {
  queued: '等待中',
  hashing: '计算哈希',
  uploading: '上传中',
  completing: '合并中',
  completed: '已完成',
  failed: '失败',
  cancelled: '已取消',
}

function statusLabel(status: UploadTaskStatus): string {
  return STATUS_LABELS[status] ?? status
}

function progressStatus(status: UploadTaskStatus): '' | 'success' | 'exception' | 'warning' {
  if (status === 'completed') return 'success'
  if (status === 'failed') return 'exception'
  if (status === 'cancelled') return 'warning'
  return ''
}

function canCancel(status: UploadTaskStatus): boolean {
  return status === 'queued' || status === 'hashing' || status === 'uploading' || status === 'completing'
}

function isRemovable(status: UploadTaskStatus): boolean {
  return status === 'completed' || status === 'failed' || status === 'cancelled'
}
</script>

<style scoped>
.upload-drop-zone {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 36px 24px;
  border: 2px dashed var(--el-border-color);
  border-radius: var(--el-border-radius-base);
  cursor: pointer;
  transition: border-color 0.2s, background-color 0.2s;
  user-select: none;
}

.upload-drop-zone:hover,
.upload-drop-zone.is-dragover {
  border-color: var(--el-color-primary);
  background-color: var(--el-color-primary-light-9);
}

.drop-zone-icon {
  color: var(--el-text-color-secondary);
}

.upload-drop-zone:hover .drop-zone-icon,
.upload-drop-zone.is-dragover .drop-zone-icon {
  color: var(--el-color-primary);
}

.drop-zone-text {
  font-size: 14px;
  color: var(--el-text-color-regular);
  margin: 0;
}

.drop-zone-link {
  color: var(--el-color-primary);
  cursor: pointer;
}

.drop-zone-hint {
  font-size: 12px;
  color: var(--el-text-color-placeholder);
  margin: 0;
}

.file-input-hidden {
  display: none;
}

.upload-task-list {
  margin-top: 16px;
}

.task-list-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 13px;
  color: var(--el-text-color-secondary);
  margin-bottom: 8px;
}

.task-items {
  max-height: 320px;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.task-item {
  padding: 10px 12px;
  border: 1px solid var(--el-border-color-lighter);
  border-radius: var(--el-border-radius-base);
  display: flex;
  flex-direction: column;
  gap: 6px;
  position: relative;
}

.task-info {
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-height: 22px;
}

.task-name {
  font-size: 14px;
  color: var(--el-text-color-primary);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 340px;
}

.task-meta {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--el-text-color-secondary);
  flex-shrink: 0;
}

.task-status-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background-color: var(--el-text-color-placeholder);
  flex-shrink: 0;
}

.task-status-dot.status-hashing,
.task-status-dot.status-completing {
  background-color: var(--el-color-warning);
}

.task-status-dot.status-uploading {
  background-color: var(--el-color-primary);
}

.task-status-dot.status-completed {
  background-color: var(--el-color-success);
}

.task-status-dot.status-failed {
  background-color: var(--el-color-danger);
}

.task-status-dot.status-cancelled {
  background-color: var(--el-text-color-placeholder);
}

.task-status-text {
  min-width: 48px;
}

.task-progress {
  flex-shrink: 0;
}

.task-actions {
  position: absolute;
  top: 10px;
  right: 8px;
  display: flex;
  align-items: center;
  gap: 2px;
}
</style>
