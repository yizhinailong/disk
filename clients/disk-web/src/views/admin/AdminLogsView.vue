<template>
  <div class="admin-logs-page">
    <div class="admin-logs-page__header">
      <h2 class="admin-logs-page__title">操作日志</h2>
    </div>

    <div class="admin-logs-page__filters">
      <el-select
        v-model="filterAction"
        placeholder="操作类型"
        clearable
        style="width: 160px"
        @change="onFilterChange"
      >
        <el-option
          v-for="opt in actionOptions"
          :key="opt.value"
          :label="opt.label"
          :value="opt.value"
        />
      </el-select>

      <el-date-picker
        v-model="filterDateRange"
        type="daterange"
        range-separator="至"
        start-placeholder="开始日期"
        end-placeholder="结束日期"
        value-format="YYYY-MM-DD"
        clearable
        style="width: 280px"
        @change="onFilterChange"
      />
    </div>

    <PageState
      :state="pageState"
      empty-text="暂无操作日志"
      error-text="加载操作日志失败"
      @retry="loadLogs(1)"
    >
      <el-table
        v-loading="adminStore.loading"
        :data="adminStore.logs"
        class="admin-logs-page__table"
        @row-click="handleRowClick"
      >
        <el-table-column label="操作类型" width="120" align="center">
          <template #default="{ row }">
            <el-tag :type="getActionTagType(row.action)" size="small">
              {{ getActionLabel(row.action) }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="目标类型" width="120" prop="target_type" />

        <el-table-column label="目标名称" min-width="200">
          <template #default="{ row }">
            <span :title="row.target_name">{{ row.target_name ?? '—' }}</span>
          </template>
        </el-table-column>

        <el-table-column label="IP 地址" width="140" prop="ip_address" />

        <el-table-column label="操作时间" width="170">
          <template #default="{ row }">
            <TimeDisplay :time="row.created_at" format="absolute" />
          </template>
        </el-table-column>
      </el-table>

      <Pagination
        v-if="adminStore.logPagination"
        :total="adminStore.logPagination.total"
        :page="adminStore.logPagination.page"
        :page-size="adminStore.logPagination.page_size"
        @change="onPageChange"
      />
    </PageState>

    <el-dialog
      v-model="detailVisible"
      title="日志详情"
      width="520px"
      destroy-on-close
    >
      <template v-if="detailLog">
        <el-descriptions :column="1" border>
          <el-descriptions-item label="日志 ID">
            {{ detailLog.id }}
          </el-descriptions-item>
          <el-descriptions-item label="操作类型">
            <el-tag :type="getActionTagType(detailLog.action)" size="small">
              {{ getActionLabel(detailLog.action) }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="目标类型">
            {{ detailLog.target_type }}
          </el-descriptions-item>
          <el-descriptions-item label="目标 ID">
            {{ detailLog.target_id ?? '—' }}
          </el-descriptions-item>
          <el-descriptions-item label="目标名称">
            {{ detailLog.target_name ?? '—' }}
          </el-descriptions-item>
          <el-descriptions-item label="IP 地址">
            {{ detailLog.ip_address }}
          </el-descriptions-item>
          <el-descriptions-item label="操作时间">
            <TimeDisplay :time="detailLog.created_at" format="absolute" absolute-format="YYYY-MM-DD HH:mm:ss" />
          </el-descriptions-item>
          <el-descriptions-item v-if="parsedDetails" label="详细信息">
            <pre class="admin-logs-page__details-json">{{ parsedDetails }}</pre>
          </el-descriptions-item>
        </el-descriptions>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useAdminStore } from '@/stores/admin'
import PageState from '@/components/base/PageState.vue'
import Pagination from '@/components/base/Pagination.vue'
import TimeDisplay from '@/components/base/TimeDisplay.vue'
import type { LogItem } from '@/types'

const adminStore = useAdminStore()

// ==================== Filters ====================

const filterAction = ref<string>('')
const filterDateRange = ref<[string, string] | null>(null)

const actionOptions = [
  { label: '上传', value: 'upload' },
  { label: '下载', value: 'download' },
  { label: '删除', value: 'delete' },
  { label: '分享', value: 'share' },
  { label: '移动', value: 'move' },
  { label: '复制', value: 'copy' },
  { label: '重命名', value: 'rename' },
]

// ==================== Detail Dialog ====================

const detailVisible = ref(false)
const detailLog = ref<LogItem | null>(null)

const parsedDetails = computed(() => {
  if (!detailLog.value?.details) return null
  try {
    const obj = JSON.parse(detailLog.value.details)
    return JSON.stringify(obj, null, 2)
  } catch {
    return detailLog.value.details
  }
})

// ==================== Computed ====================

const pageState = computed<'loading' | 'empty' | 'content'>(() => {
  if (adminStore.loading && adminStore.logs.length === 0) return 'loading'
  if (adminStore.logs.length === 0) return 'empty'
  return 'content'
})

// ==================== Helpers ====================

function getActionTagType(action: string): 'primary' | 'success' | 'danger' | 'warning' | 'info' {
  const map: Record<string, 'primary' | 'success' | 'danger' | 'warning' | 'info'> = {
    upload: 'primary',
    download: 'success',
    delete: 'danger',
    share: 'warning',
    move: 'info',
    copy: 'info',
    rename: 'info',
  }
  return map[action] ?? 'info'
}

function getActionLabel(action: string): string {
  const map: Record<string, string> = {
    upload: '上传',
    download: '下载',
    delete: '删除',
    share: '分享',
    move: '移动',
    copy: '复制',
    rename: '重命名',
  }
  return map[action] ?? action
}

// ==================== Data Loading ====================

function loadLogs(page?: number): void {
  const params: Record<string, unknown> = {
    page: page ?? adminStore.logPagination?.page ?? 1,
    page_size: adminStore.logPagination?.page_size ?? 20,
  }
  if (filterAction.value) {
    params.action = filterAction.value
  }
  if (filterDateRange.value) {
    params.start_date = filterDateRange.value[0]
    params.end_date = filterDateRange.value[1]
  }
  adminStore.fetchLogs(params as Parameters<typeof adminStore.fetchLogs>[0])
}

function onFilterChange(): void {
  loadLogs(1)
}

function onPageChange(page: number): void {
  loadLogs(page)
}

// ==================== Row Click ====================

function handleRowClick(row: LogItem): void {
  detailLog.value = row
  detailVisible.value = true
}

// ==================== Init ====================

onMounted(() => {
  loadLogs(1)
})
</script>

<style scoped>
.admin-logs-page {
  height: 100%;
  display: flex;
  flex-direction: column;
}

.admin-logs-page__header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.admin-logs-page__title {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.admin-logs-page__filters {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.admin-logs-page__table {
  flex: 1;
}

.admin-logs-page__table :deep(.el-table__row) {
  cursor: pointer;
}

.admin-logs-page__details-json {
  margin: 0;
  padding: 8px;
  max-height: 240px;
  overflow: auto;
  font-size: 12px;
  line-height: 1.5;
  background: var(--el-fill-color-light);
  border-radius: 4px;
  white-space: pre-wrap;
  word-break: break-all;
}
</style>
