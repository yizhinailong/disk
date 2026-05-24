<template>
  <div class="share-browse-page">
    <div class="share-browse__header">
      <h2 class="share-browse__title">分享文件</h2>
      <div class="share-browse__header-actions">
        <el-button
          v-if="selectedItems.length > 0"
          type="primary"
          :disabled="!authStore.isAuthenticated"
          @click="handleSaveToMyDrive"
        >
          保存到我的网盘 ({{ selectedItems.length }})
        </el-button>
        <el-button text @click="goToVerify">返回验证</el-button>
      </div>
    </div>

    <div class="share-browse__breadcrumb">
      <el-breadcrumb separator="/">
        <el-breadcrumb-item @click="goToShareRoot">
          <span class="share-browse__breadcrumb-root">分享根目录</span>
        </el-breadcrumb-item>
        <el-breadcrumb-item
          v-for="crumb in visitorStore.breadcrumb"
          :key="crumb.id"
          @click="navigateToFolder(crumb.id)"
        >
          <span class="share-browse__breadcrumb-link">{{ crumb.name }}</span>
        </el-breadcrumb-item>
      </el-breadcrumb>
    </div>

    <PageState
      :state="pageState"
      empty-text="此文件夹为空"
      error-text="加载分享内容失败"
      @retry="refreshView"
    >
      <el-table
        v-loading="visitorStore.loading"
        :data="visitorStore.browseItems"
        class="share-browse__table"
        @row-click="onRowClick"
        @selection-change="onSelectionChange"
      >
        <el-table-column type="selection" width="40" />

        <el-table-column label="名称" min-width="320">
          <template #default="{ row }">
            <div class="share-browse__name-cell">
              <FileIcon :is-folder="row.type === 'folder'" :size="24" />
              <span class="share-browse__name-text">{{ row.name }}</span>
            </div>
          </template>
        </el-table-column>

        <el-table-column label="大小" width="140">
          <template #default="{ row }">
            <SizeDisplay v-if="row.size != null" :bytes="row.size" />
            <span v-else class="share-browse__dash">—</span>
          </template>
        </el-table-column>

        <el-table-column label="类型" width="120">
          <template #default="{ row }">
            {{ row.type === 'folder' ? '文件夹' : '文件' }}
          </template>
        </el-table-column>

        <el-table-column label="操作" width="120" align="center">
          <template #default="{ row }">
            <el-button
              v-if="row.type === 'file'"
              type="primary"
              text
              size="small"
              :loading="downloadingFileId === row.id"
              @click.stop="handleDownload(row)"
            >
              下载
            </el-button>
            <span v-else class="share-browse__dash">—</span>
          </template>
        </el-table-column>
      </el-table>
    </PageState>

    <SaveShareDialog
      v-model:visible="showSaveDialog"
      :file-count="selectedFileIds.length"
      :folder-count="selectedFolderIds.length"
      @confirm="confirmSave"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { ElMessage } from 'element-plus';
import { useVisitorStore } from '@/stores/visitor';
import { useAuthStore } from '@/stores/auth';
import PageState from '@/components/base/PageState.vue';
import FileIcon from '@/components/base/FileIcon.vue';
import SizeDisplay from '@/components/base/SizeDisplay.vue';
import SaveShareDialog from '@/components/share/SaveShareDialog.vue';
import type { BrowseItem } from '@/types';

const route = useRoute();
const router = useRouter();
const visitorStore = useVisitorStore();
const authStore = useAuthStore();
const downloadingFileId = ref<number | null>(null);
const showSaveDialog = ref(false);
const selectedItems = ref<BrowseItem[]>([]);

const shareId = computed(() => route.params.shareId as string);
const currentFolderId = computed(() => {
  const fid = route.query.folderId;
  return fid ? Number(fid) : undefined;
});

const selectedFileIds = computed(() =>
  selectedItems.value.filter((item) => item.type === 'file').map((item) => item.id),
);

const selectedFolderIds = computed(() =>
  selectedItems.value.filter((item) => item.type === 'folder').map((item) => item.id),
);

const pageState = computed<'loading' | 'empty' | 'error' | 'content'>(() => {
  if (visitorStore.loading) return 'loading';
  if (visitorStore.browseItems.length === 0) return 'empty';
  return 'content';
});

function onSelectionChange(items: BrowseItem[]): void {
  selectedItems.value = items;
}

function goToVerify(): void {
  visitorStore.clearToken();
  router.push({ name: 'share-verify', params: { shareId: shareId.value } });
}

function goToShareRoot(): void {
  router.replace({ name: 'share-browse', params: { shareId: shareId.value } });
}

function navigateToFolder(folderId: number): void {
  router.push({
    name: 'share-browse',
    params: { shareId: shareId.value },
    query: { folderId: String(folderId) },
  });
}

function onRowClick(row: BrowseItem): void {
  if (row.type === 'folder') {
    navigateToFolder(row.id);
  }
}

async function handleDownload(row: BrowseItem): Promise<void> {
  downloadingFileId.value = row.id;
  try {
    await visitorStore.downloadFile(shareId.value, row.id, row.name);
  } finally {
    downloadingFileId.value = null;
  }
}

function handleSaveToMyDrive(): void {
  if (!authStore.isAuthenticated) {
    ElMessage.warning('请先登录后再保存到网盘');
    return;
  }
  showSaveDialog.value = true;
}

async function confirmSave(targetFolderId: number): Promise<void> {
  try {
    await visitorStore.saveToMyDrive(
      shareId.value,
      selectedFileIds.value,
      selectedFolderIds.value,
      targetFolderId,
    );
    selectedItems.value = [];
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : '保存失败';
    ElMessage.error(msg);
  }
}

async function refreshView(): Promise<void> {
  if (!visitorStore.shareToken) {
    router.replace({ name: 'share-verify', params: { shareId: shareId.value } });
    return;
  }
  selectedItems.value = [];
  await visitorStore.fetchBrowseItems(shareId.value, currentFolderId.value);
}

watch(() => route.query.folderId, () => {
  refreshView();
});

onMounted(() => {
  if (!visitorStore.shareToken) {
    router.replace({ name: 'share-verify', params: { shareId: shareId.value } });
    return;
  }
  refreshView();
});
</script>

<style scoped>
.share-browse-page {
  height: 100%;
  display: flex;
  flex-direction: column;
}

.share-browse__header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.share-browse__header-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.share-browse__title {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.share-browse__breadcrumb {
  padding: 12px 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.share-browse__breadcrumb-root {
  cursor: pointer;
  font-weight: 500;
}

.share-browse__breadcrumb-link {
  cursor: pointer;
}

.share-browse__table {
  flex: 1;
}

.share-browse__table :deep(.el-table__row) {
  cursor: default;
}

.share-browse__name-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.share-browse__name-text {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.share-browse__dash {
  color: var(--el-text-color-placeholder);
}
</style>
