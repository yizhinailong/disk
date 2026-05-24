<template>
  <div class="shares-page">
    <div class="shares-page__header">
      <h2 class="shares-page__title">我的分享</h2>
      <div class="shares-page__toolbar">
        <el-button
          :disabled="selectedRows.length === 0 || shareStore.loading"
          type="danger"
          plain
          @click="handleBatchCancel"
        >
          批量取消（{{ selectedRows.length }}）
        </el-button>
      </div>
    </div>

    <div class="shares-page__filters">
      <el-radio-group v-model="activeFilter" @change="onFilterChange">
        <el-radio-button value="all">全部</el-radio-button>
        <el-radio-button value="active">有效</el-radio-button>
        <el-radio-button value="expired">已过期</el-radio-button>
        <el-radio-button value="cancelled">已取消</el-radio-button>
      </el-radio-group>
    </div>

    <PageState
      :state="pageState"
      empty-text="暂无分享记录"
      error-text="加载分享列表失败"
      @retry="loadShares(1)"
    >
      <el-table
        ref="tableRef"
        v-loading="shareStore.loading"
        :data="shareStore.shares"
        class="shares-page__table"
        @selection-change="onSelectionChange"
      >
        <el-table-column type="selection" width="48" />

        <el-table-column label="文件名" min-width="200" prop="file_name">
          <template #default="{ row }">
            <span class="shares-page__name" :title="row.file_name">{{ row.file_name }}</span>
          </template>
        </el-table-column>

        <el-table-column label="文件数" width="80" prop="file_count" align="center">
          <template #default="{ row }">
            {{ row.file_count }}
          </template>
        </el-table-column>

        <el-table-column label="分享链接" min-width="180">
          <template #default="{ row }">
            <div class="shares-page__link-cell">
              <span class="shares-page__link-text" :title="row.share_link">
                {{ truncateLink(row.share_link) }}
              </span>
              <el-button link type="primary" size="small" @click="copyLink(row.share_link)">
                复制
              </el-button>
            </div>
          </template>
        </el-table-column>

        <el-table-column label="权限" width="80" align="center">
          <template #default="{ row }">
            {{ row.permission === 'download' ? '可下载' : '仅查看' }}
          </template>
        </el-table-column>

        <el-table-column label="密码" width="70" align="center">
          <template #default="{ row }">
            <el-icon v-if="row.has_password" :size="14" color="var(--el-color-warning)"><Lock /></el-icon>
            <span v-else class="shares-page__dash">—</span>
          </template>
        </el-table-column>

        <el-table-column label="状态" width="90" align="center">
          <template #default="{ row }">
            <el-tag :type="getStatusTagType(row.status)" size="small">
              {{ getStatusLabel(row.status) }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="创建时间" width="170" prop="created_at">
          <template #default="{ row }">
            {{ formatDate(row.created_at) }}
          </template>
        </el-table-column>

        <el-table-column label="过期时间" width="170" prop="expires_at">
          <template #default="{ row }">
            {{ row.expires_at ? formatDate(row.expires_at) : '永久有效' }}
          </template>
        </el-table-column>

        <el-table-column label="操作" width="200" fixed="right">
          <template #default="{ row }">
            <el-button link type="primary" @click="handleViewDetail(row)">查看详情</el-button>
            <el-button link type="primary" @click="copyLink(row.share_link)">复制链接</el-button>
            <el-button
              v-if="row.status === 'active'"
              link
              type="danger"
              @click="handleCancelSingle(row)"
            >
              取消分享
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <Pagination
        v-if="shareStore.pagination"
        :total="shareStore.pagination.total"
        :page="shareStore.pagination.page"
        :page-size="shareStore.pagination.page_size"
        @change="onPageChange"
      />
    </PageState>

    <ShareDetailDialog
      v-model:visible="detailDialogVisible"
      :share-id="detailShareId"
      @cancelled="onDetailCancelled"
      @updated="onDetailUpdated"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Lock } from '@element-plus/icons-vue'
import { useShareStore } from '@/stores'
import PageState from '@/components/base/PageState.vue'
import Pagination from '@/components/base/Pagination.vue'
import ShareDetailDialog from '@/components/share/ShareDetailDialog.vue'
import type { ShareItem, ShareStatus } from '@/types'

const shareStore = useShareStore()

const tableRef = ref<InstanceType<typeof import('element-plus')['ElTable']>>()
const selectedRows = ref<ShareItem[]>([])
const activeFilter = ref<string>('all')
const detailDialogVisible = ref(false)
const detailShareId = ref<string | null>(null)

// ==================== Computed ====================

const pageState = computed<'loading' | 'empty' | 'content'>(() => {
  if (shareStore.loading && shareStore.shares.length === 0) return 'loading'
  if (shareStore.shares.length === 0) return 'empty'
  return 'content'
})

// ==================== Helpers ====================

function getStatusTagType(status: ShareStatus): 'success' | 'warning' | 'danger' {
  const map: Record<ShareStatus, 'success' | 'warning' | 'danger'> = {
    active: 'success',
    expired: 'warning',
    cancelled: 'danger',
  }
  return map[status]
}

function getStatusLabel(status: ShareStatus): string {
  const map: Record<ShareStatus, string> = {
    active: '有效',
    expired: '已过期',
    cancelled: '已取消',
  }
  return map[status]
}

function formatDate(dateStr: string): string {
  try {
    return new Date(dateStr).toLocaleString('zh-CN')
  } catch {
    return dateStr
  }
}

function truncateLink(link: string): string {
  if (link.length <= 40) return link
  return link.substring(0, 37) + '...'
}

async function copyLink(link: string): Promise<void> {
  try {
    await navigator.clipboard.writeText(link)
    ElMessage.success('已复制到剪贴板')
  } catch {
    ElMessage.error('复制失败，请手动复制')
  }
}

// ==================== Data loading ====================

function loadShares(page?: number): void {
  shareStore.statusFilter = activeFilter.value
  shareStore.fetchShares(page)
}

function onFilterChange(): void {
  selectedRows.value = []
  tableRef.value?.clearSelection()
  loadShares(1)
}

function onPageChange(page: number): void {
  loadShares(page)
}

function onSelectionChange(rows: ShareItem[]): void {
  selectedRows.value = rows
}

// ==================== Actions ====================

function handleViewDetail(row: ShareItem): void {
  detailShareId.value = row.share_id
  detailDialogVisible.value = true
}

async function handleCancelSingle(row: ShareItem): Promise<void> {
  try {
    await ElMessageBox.confirm(
      `确定要取消分享「${row.file_name}」吗？取消后链接将立即失效。`,
      '确认取消分享',
      {
        confirmButtonText: '取消分享',
        cancelButtonText: '保留',
        type: 'warning',
        confirmButtonClass: 'el-button--danger',
      },
    )
  } catch {
    return
  }

  try {
    await shareStore.cancelShares([row.share_id])
    loadShares(shareStore.pagination?.page)
  } catch {
    // store already shows error
  }
}

async function handleBatchCancel(): Promise<void> {
  if (selectedRows.value.length === 0) return

  const activeSelected = selectedRows.value.filter((r) => r.status === 'active')
  if (activeSelected.length === 0) {
    ElMessage.warning('选中的分享中没有可取消的有效分享')
    return
  }

  try {
    await ElMessageBox.confirm(
      `确定要取消选中的 ${activeSelected.length} 个分享吗？取消后链接将立即失效。`,
      '批量取消分享',
      {
        confirmButtonText: '取消分享',
        cancelButtonText: '保留',
        type: 'warning',
        confirmButtonClass: 'el-button--danger',
      },
    )
  } catch {
    return
  }

  try {
    const ids = activeSelected.map((r) => r.share_id)
    await shareStore.cancelShares(ids)
    selectedRows.value = []
    tableRef.value?.clearSelection()
    loadShares(shareStore.pagination?.page)
  } catch {
    // store already shows error
  }
}

function onDetailCancelled(): void {
  loadShares(shareStore.pagination?.page)
}

function onDetailUpdated(): void {
  loadShares(shareStore.pagination?.page)
}

// ==================== Init ====================

onMounted(() => {
  loadShares(1)
})
</script>

<style scoped>
.shares-page {
  height: 100%;
  display: flex;
  flex-direction: column;
}

.shares-page__header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.shares-page__title {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.shares-page__toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
}

.shares-page__filters {
  padding: 12px 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.shares-page__table {
  flex: 1;
}

.shares-page__table :deep(.el-table__row) {
  cursor: default;
}

.shares-page__name {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.shares-page__link-cell {
  display: flex;
  align-items: center;
  gap: 6px;
}

.shares-page__link-text {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 13px;
  color: var(--el-text-color-regular);
}

.shares-page__dash {
  color: var(--el-text-color-placeholder);
}
</style>
