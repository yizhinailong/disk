<template>
  <div class="trash-page">
    <div class="trash-page__header">
      <h2 class="trash-page__title">回收站</h2>
      <div class="trash-page__toolbar">
        <el-button
          :disabled="selectedRows.length === 0 || store.loading"
          type="primary"
          plain
          @click="handleRestoreSelected"
        >
          恢复（{{ selectedRows.length }}）
        </el-button>
        <el-button
          :disabled="selectedRows.length === 0 || store.loading"
          type="danger"
          plain
          @click="handleDeleteSelected"
        >
          永久删除（{{ selectedRows.length }}）
        </el-button>
        <el-button
          :disabled="store.items.length === 0 || store.loading"
          type="danger"
          @click="handleEmptyTrash"
        >
          清空回收站
        </el-button>
      </div>
    </div>

    <PageState
      :state="pageState"
      empty-text="回收站为空"
      error-text="加载回收站失败"
      @retry="store.fetchTrashItems(1)"
    >
      <el-table
        ref="tableRef"
        v-loading="store.loading"
        :data="store.items"
        class="trash-page__table"
        @selection-change="onSelectionChange"
      >
        <el-table-column type="selection" width="48" />

        <el-table-column label="文件名" min-width="260" prop="name">
          <template #default="{ row }">
            <div class="trash-page__name-cell">
              <FileIcon :is-folder="row.type === 'folder'" :mime-type="row.type === 'file' ? '' : ''" :size="24" />
              <span class="trash-page__name-text">{{ row.name }}</span>
            </div>
          </template>
        </el-table-column>

        <el-table-column label="原始路径" min-width="200" prop="original_path">
          <template #default="{ row }">
            <span class="trash-page__path-text">{{ row.original_path }}</span>
          </template>
        </el-table-column>

        <el-table-column label="大小" width="120" prop="size">
          <template #default="{ row }">
            <SizeDisplay v-if="row.type === 'file' && row.size >= 0" :bytes="row.size" />
            <span v-else class="trash-page__dash">—</span>
          </template>
        </el-table-column>

        <el-table-column label="删除时间" width="180" prop="deleted_at">
          <template #default="{ row }">
            <TimeDisplay :time="row.deleted_at" format="absolute" />
          </template>
        </el-table-column>

        <el-table-column label="过期时间" width="180" prop="expires_at">
          <template #default="{ row }">
            <TimeDisplay :time="row.expires_at" format="absolute" />
          </template>
        </el-table-column>

        <el-table-column label="操作" width="160" fixed="right">
          <template #default="{ row }">
            <el-button link type="primary" @click="handleRestoreSingle(row)">
              恢复
            </el-button>
            <el-button link type="danger" @click="handleDeleteSingle(row)">
              永久删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <Pagination
        v-if="store.pagination"
        :total="store.pagination.total"
        :page="store.pagination.page"
        :page-size="store.pagination.page_size"
        @change="onPageChange"
      />
    </PageState>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { ElMessage, ElMessageBox } from 'element-plus';
import { useTrashStore } from '@/stores/trash';
import PageState from '@/components/base/PageState.vue';
import FileIcon from '@/components/base/FileIcon.vue';
import SizeDisplay from '@/components/base/SizeDisplay.vue';
import TimeDisplay from '@/components/base/TimeDisplay.vue';
import Pagination from '@/components/base/Pagination.vue';
import type { TrashItem } from '@/types';

const store = useTrashStore();

const tableRef = ref<InstanceType<typeof import('element-plus')['ElTable']>>();
const selectedRows = ref<TrashItem[]>([]);

const pageState = computed<'loading' | 'empty' | 'content'>(() => {
  if (store.loading && store.items.length === 0) return 'loading';
  if (store.items.length === 0) return 'empty';
  return 'content';
});

function onSelectionChange(rows: TrashItem[]): void {
  selectedRows.value = rows;
}

function onPageChange(page: number): void {
  store.fetchTrashItems(page);
}

function showBatchResult(
  summary: { total: number; success_count: number; failure_count: number },
  results: readonly { status: string; error?: { message?: string } }[],
): void {
  if (summary.failure_count === 0) {
    ElMessage.success(`操作成功，共处理 ${summary.success_count} 项`);
    return;
  }

  const failedMessages = results
    .filter((r) => r.status === 'failed')
    .map((r) => r.error?.message ?? '未知错误')
    .join('；');

  ElMessage.warning(
    `成功 ${summary.success_count} 项，失败 ${summary.failure_count} 项：${failedMessages}`,
  );
}

async function handleRestoreSingle(row: TrashItem): Promise<void> {
  try {
    const res = await store.restoreItems({ trash_ids: [row.id] });
    showBatchResult(res.summary, res.results);
  } catch {
    ElMessage.error('恢复失败，请稍后重试');
  }
}

async function handleDeleteSingle(row: TrashItem): Promise<void> {
  try {
    await ElMessageBox.confirm(
      `确定要永久删除「${row.name}」吗？此操作不可撤销。`,
      '永久删除',
      { confirmButtonText: '删除', cancelButtonText: '取消', type: 'warning' },
    );
  } catch {
    return;
  }

  try {
    const res = await store.deletePermanently({ trash_ids: [row.id] });
    showBatchResult(res.summary, res.results);
  } catch {
    ElMessage.error('删除失败，请稍后重试');
  }
}

async function handleRestoreSelected(): Promise<void> {
  if (selectedRows.value.length === 0) return;

  const trashIds = selectedRows.value.map((r) => r.id);
  try {
    const res = await store.restoreItems({ trash_ids: trashIds });
    showBatchResult(res.summary, res.results);
    selectedRows.value = [];
  } catch {
    ElMessage.error('恢复失败，请稍后重试');
  }
}

async function handleDeleteSelected(): Promise<void> {
  if (selectedRows.value.length === 0) return;

  try {
    await ElMessageBox.confirm(
      `确定要永久删除选中的 ${selectedRows.value.length} 项吗？此操作不可撤销。`,
      '永久删除',
      { confirmButtonText: '删除', cancelButtonText: '取消', type: 'warning' },
    );
  } catch {
    return;
  }

  const trashIds = selectedRows.value.map((r) => r.id);
  try {
    const res = await store.deletePermanently({ trash_ids: trashIds });
    showBatchResult(res.summary, res.results);
    selectedRows.value = [];
  } catch {
    ElMessage.error('删除失败，请稍后重试');
  }
}

async function handleEmptyTrash(): Promise<void> {
  try {
    await ElMessageBox.confirm(
      '清空回收站将永久删除所有文件，此操作不可撤销。',
      '清空回收站',
      {
        confirmButtonText: '清空',
        cancelButtonText: '取消',
        type: 'error',
      },
    );
  } catch {
    return;
  }

  try {
    const res = await store.emptyTrash();
    ElMessage.success(
      `已清空回收站，共删除 ${res.deleted_count} 项`,
    );
  } catch {
    ElMessage.error('清空回收站失败，请稍后重试');
  }
}

onMounted(() => {
  store.fetchTrashItems(1);
});
</script>

<style scoped>
.trash-page {
  height: 100%;
  display: flex;
  flex-direction: column;
}

.trash-page__header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.trash-page__title {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.trash-page__toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
}

.trash-page__table {
  flex: 1;
}

.trash-page__table :deep(.el-table__row) {
  cursor: default;
}

.trash-page__name-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.trash-page__name-text {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.trash-page__path-text {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.trash-page__dash {
  color: var(--el-text-color-placeholder);
}
</style>
