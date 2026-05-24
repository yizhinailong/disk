<template>
  <div class="transfer-page">
    <!-- 全局统计摘要 -->
    <div class="transfer-page__summary">
      <div class="transfer-page__stat">
        <span class="transfer-page__stat-label">上传中</span>
        <span class="transfer-page__stat-value">{{ transferStore.activeUploads.length }}</span>
      </div>
      <div class="transfer-page__stat">
        <span class="transfer-page__stat-label">下载中</span>
        <span class="transfer-page__stat-value">{{ transferStore.activeDownloads.length }}</span>
      </div>
      <div class="transfer-page__stat">
        <span class="transfer-page__stat-label">排队中</span>
        <span class="transfer-page__stat-value">{{ transferStore.pendingUploads.length }}</span>
      </div>
      <div class="transfer-page__stat">
        <span class="transfer-page__stat-label">已失败</span>
        <span class="transfer-page__stat-value transfer-page__stat-value--danger">
          {{ transferStore.failedUploads.length }}
        </span>
      </div>
    </div>

    <!-- 选项卡 -->
    <el-tabs v-model="activeTab" class="transfer-page__tabs">
      <!-- ==================== 上传列表 ==================== -->
      <el-tab-pane name="upload">
        <template #label>
          <span>上传列表 <el-badge v-if="transferStore.uploads.length" :value="transferStore.uploads.length" type="primary" /></span>
        </template>

        <div v-if="transferStore.uploads.length === 0" class="transfer-page__empty">
          <el-empty description="暂无上传任务" />
        </div>

        <template v-else>
          <div class="transfer-page__toolbar">
            <el-button
              v-if="hasCompletedUploads"
              text
              type="primary"
              @click="transferStore.clearCompletedUploads()"
            >
              清除已完成
            </el-button>
          </div>

          <el-table :data="transferStore.uploads" class="transfer-page__table" row-key="id">
            <el-table-column label="文件名" min-width="240">
              <template #default="{ row }">
                <span class="transfer-page__name" :title="row.file_name">{{ row.file_name }}</span>
              </template>
            </el-table-column>

            <el-table-column label="大小" width="120">
              <template #default="{ row }">
                <SizeDisplay :bytes="row.file_size" />
              </template>
            </el-table-column>

            <el-table-column label="进度" width="200">
              <template #default="{ row }">
                <el-progress
                  :percentage="row.progress"
                  :status="progressStatus(row.status)"
                  :stroke-width="6"
                />
              </template>
            </el-table-column>

            <el-table-column label="状态" width="110">
              <template #default="{ row }">
                <el-tooltip
                  v-if="row.error"
                  :content="row.error"
                  placement="top"
                >
                  <el-tag :type="uploadStatusType(row.status)" size="small" class="transfer-page__status-tag">
                    {{ uploadStatusText(row.status) }}
                  </el-tag>
                </el-tooltip>
                <el-tag v-else :type="uploadStatusType(row.status)" size="small" class="transfer-page__status-tag">
                  {{ uploadStatusText(row.status) }}
                </el-tag>
              </template>
            </el-table-column>

            <el-table-column label="操作" width="140" align="center">
              <template #default="{ row }">
                <div class="transfer-page__actions">
                  <el-tooltip v-if="row.status === 'failed'" content="重试">
                    <el-button link type="primary" @click="transferStore.retryUploadTask(row.id)">
                      <el-icon><RefreshRight /></el-icon>
                    </el-button>
                  </el-tooltip>
                  <el-tooltip
                    v-if="row.status === 'uploading' || row.status === 'hashing' || row.status === 'completing'"
                    content="取消"
                  >
                    <el-button link type="danger" @click="transferStore.cancelUploadTask(row.id)">
                      <el-icon><VideoPause /></el-icon>
                    </el-button>
                  </el-tooltip>
                  <el-tooltip
                    v-if="row.status === 'completed' || row.status === 'cancelled' || row.status === 'failed'"
                    content="删除"
                  >
                    <el-button link type="danger" @click="transferStore.removeUploadTask(row.id)">
                      <el-icon><Delete /></el-icon>
                    </el-button>
                  </el-tooltip>
                </div>
              </template>
            </el-table-column>
          </el-table>
        </template>
      </el-tab-pane>

      <!-- ==================== 下载列表 ==================== -->
      <el-tab-pane name="download">
        <template #label>
          <span>下载列表 <el-badge v-if="transferStore.downloads.length" :value="transferStore.downloads.length" type="primary" /></span>
        </template>

        <div v-if="transferStore.downloads.length === 0" class="transfer-page__empty">
          <el-empty description="暂无下载任务" />
        </div>

        <template v-else>
          <div class="transfer-page__toolbar">
            <el-button
              v-if="hasCompletedDownloads"
              text
              type="primary"
              @click="transferStore.clearCompletedDownloads()"
            >
              清除已完成
            </el-button>
          </div>

          <el-table :data="transferStore.downloads" class="transfer-page__table" row-key="id">
            <el-table-column label="文件名" min-width="240">
              <template #default="{ row }">
                <span class="transfer-page__name" :title="row.file_name">{{ row.file_name }}</span>
              </template>
            </el-table-column>

            <el-table-column label="大小" width="120">
              <template #default="{ row }">
                <SizeDisplay :bytes="row.file_size" />
              </template>
            </el-table-column>

            <el-table-column label="进度" width="200">
              <template #default="{ row }">
                <el-progress
                  :percentage="row.progress"
                  :status="downloadProgressStatus(row.status)"
                  :stroke-width="6"
                />
              </template>
            </el-table-column>

            <el-table-column label="状态" width="110">
              <template #default="{ row }">
                <el-tooltip
                  v-if="row.error"
                  :content="row.error"
                  placement="top"
                >
                  <el-tag :type="downloadStatusType(row.status)" size="small" class="transfer-page__status-tag">
                    {{ downloadStatusText(row.status) }}
                  </el-tag>
                </el-tooltip>
                <el-tag v-else :type="downloadStatusType(row.status)" size="small" class="transfer-page__status-tag">
                  {{ downloadStatusText(row.status) }}
                </el-tag>
              </template>
            </el-table-column>

            <el-table-column label="操作" width="160" align="center">
              <template #default="{ row }">
                <div class="transfer-page__actions">
                  <el-tooltip v-if="row.status === 'downloading'" content="暂停">
                    <el-button link type="warning" @click="transferStore.pauseDownloadTask(row.id)">
                      <el-icon><VideoPause /></el-icon>
                    </el-button>
                  </el-tooltip>
                  <el-tooltip v-if="row.status === 'paused'" content="继续">
                    <el-button link type="primary" @click="transferStore.resumeDownloadTask(row.id)">
                      <el-icon><VideoPlay /></el-icon>
                    </el-button>
                  </el-tooltip>
                  <el-tooltip v-if="row.status === 'downloading' || row.status === 'pending'" content="取消">
                    <el-button link type="danger" @click="transferStore.cancelDownloadTask(row.id)">
                      <el-icon><Close /></el-icon>
                    </el-button>
                  </el-tooltip>
                  <el-tooltip
                    v-if="row.status === 'completed' || row.status === 'cancelled' || row.status === 'failed'"
                    content="删除"
                  >
                    <el-button link type="danger" @click="transferStore.removeDownloadTask(row.id)">
                      <el-icon><Delete /></el-icon>
                    </el-button>
                  </el-tooltip>
                </div>
              </template>
            </el-table-column>
          </el-table>
        </template>
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useTransferStore } from '@/stores'
import SizeDisplay from '@/components/base/SizeDisplay.vue'
import { VideoPause, VideoPlay, Close, RefreshRight, Delete } from '@element-plus/icons-vue'
import type { UploadTaskStatus, DownloadTaskStatus } from '@/types'

const transferStore = useTransferStore()
const activeTab = ref<'upload' | 'download'>('upload')

// ==================== Computed ====================

const hasCompletedUploads = computed(() =>
  transferStore.uploads.some((t) => t.status === 'completed' || t.status === 'cancelled'),
)

const hasCompletedDownloads = computed(() =>
  transferStore.downloads.some((t) => t.status === 'completed' || t.status === 'cancelled' || t.status === 'failed'),
)

// ==================== Upload helpers ====================

const uploadStatusMap: Record<UploadTaskStatus, string> = {
  queued: '排队中',
  hashing: '计算哈希',
  uploading: '上传中',
  completing: '合并中',
  completed: '已完成',
  failed: '已失败',
  cancelled: '已取消',
}

const uploadTagTypeMap: Record<UploadTaskStatus, 'info' | 'success' | 'warning' | 'danger'> = {
  queued: 'info',
  hashing: 'info',
  uploading: 'info',
  completing: 'info',
  completed: 'success',
  failed: 'danger',
  cancelled: 'info',
}

function uploadStatusText(status: UploadTaskStatus): string {
  return uploadStatusMap[status]
}

function uploadStatusType(status: UploadTaskStatus): 'info' | 'success' | 'warning' | 'danger' {
  return uploadTagTypeMap[status]
}

function progressStatus(status: UploadTaskStatus): '' | 'success' | 'exception' {
  if (status === 'completed') return 'success'
  if (status === 'failed') return 'exception'
  return ''
}

// ==================== Download helpers ====================

const downloadStatusMap: Record<DownloadTaskStatus, string> = {
  pending: '等待中',
  downloading: '下载中',
  paused: '已暂停',
  completed: '已完成',
  failed: '已失败',
  cancelled: '已取消',
}

const downloadTagTypeMap: Record<DownloadTaskStatus, 'info' | 'success' | 'warning' | 'danger'> = {
  pending: 'info',
  downloading: 'info',
  paused: 'warning',
  completed: 'success',
  failed: 'danger',
  cancelled: 'info',
}

function downloadStatusText(status: DownloadTaskStatus): string {
  return downloadStatusMap[status]
}

function downloadStatusType(status: DownloadTaskStatus): 'info' | 'success' | 'warning' | 'danger' {
  return downloadTagTypeMap[status]
}

function downloadProgressStatus(status: DownloadTaskStatus): '' | 'success' | 'exception' {
  if (status === 'completed') return 'success'
  if (status === 'failed') return 'exception'
  return ''
}
</script>

<style scoped>
.transfer-page {
  height: 100%;
  display: flex;
  flex-direction: column;
}

/* ==================== Summary ==================== */

.transfer-page__summary {
  display: flex;
  gap: 32px;
  padding: 16px 20px;
  border-bottom: 1px solid var(--el-border-color-lighter);
  background: var(--el-bg-color);
}

.transfer-page__stat {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.transfer-page__stat-label {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.transfer-page__stat-value {
  font-size: 20px;
  font-weight: 600;
  color: var(--el-text-color-primary);
  font-variant-numeric: tabular-nums;
}

.transfer-page__stat-value--danger {
  color: var(--el-color-danger);
}

/* ==================== Tabs ==================== */

.transfer-page__tabs {
  flex: 1;
  display: flex;
  flex-direction: column;
}

.transfer-page__tabs :deep(.el-tabs__content) {
  flex: 1;
  overflow: auto;
}

.transfer-page__tabs :deep(.el-tabs__header) {
  padding: 0 20px;
  margin-bottom: 0;
}

/* ==================== Toolbar ==================== */

.transfer-page__toolbar {
  display: flex;
  justify-content: flex-end;
  padding: 8px 20px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

/* ==================== Table ==================== */

.transfer-page__table {
  width: 100%;
}

.transfer-page__table :deep(.el-table__row) {
  cursor: default;
}

.transfer-page__name {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.transfer-page__status-tag {
  min-width: 64px;
  text-align: center;
}

.transfer-page__actions {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
}

/* ==================== Empty ==================== */

.transfer-page__empty {
  padding: 64px 0;
}
</style>
