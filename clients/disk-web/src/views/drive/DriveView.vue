<template>
  <div class="drive-page">
    <!-- 搜索结果模式 -->
    <template v-if="store.isSearching">
      <div class="drive-page__search-header">
        <div class="drive-page__search-info">
          <span class="drive-page__search-label">
            搜索结果: 「{{ store.searchQuery }}」
          </span>
          <span class="drive-page__search-count">
            （{{ store.searchResults.length }} 个结果）
          </span>
        </div>
        <el-button text @click="clearSearchAndGoBack">
          返回文件列表
        </el-button>
      </div>

      <PageState
        :state="searchPageState"
        empty-text="未找到匹配的文件"
        error-text="搜索失败"
        @retry="executeSearch"
      >
        <el-table
          v-loading="store.loading"
          :data="store.searchResults"
          class="drive-page__table"
          @row-click="onSearchRowClick"
        >
          <el-table-column label="名称" min-width="260" prop="name">
            <template #default="{ row }">
              <div class="drive-page__name-cell">
                <FileIcon :is-folder="row.type === 'folder'" :mime-type="row.mime_type ?? ''" :size="24" />
                <span class="drive-page__name-text">{{ row.name }}</span>
              </div>
            </template>
          </el-table-column>

          <el-table-column label="路径" min-width="200">
            <template #default="{ row }">
              <span class="drive-page__path-text">{{ row.path }}</span>
            </template>
          </el-table-column>

          <el-table-column label="大小" width="140" prop="size">
            <template #default="{ row }">
              <SizeDisplay v-if="row.size != null" :bytes="row.size" />
              <span v-else class="drive-page__dash">—</span>
            </template>
          </el-table-column>

          <el-table-column label="修改时间" width="200" prop="updated_at">
            <template #default="{ row }">
              <TimeDisplay :time="row.updated_at" format="absolute" />
            </template>
          </el-table-column>
        </el-table>
      </PageState>
    </template>

    <!-- 正常文件列表模式 -->
    <template v-else>
      <DriveToolbar />
      <div class="drive-page__workspace">
        <aside class="drive-page__tree" aria-label="文件夹树">
          <FolderTree />
        </aside>

        <section class="drive-page__content" :aria-label="`当前文件夹 ${currentFolderName}`">
          <!-- 面包屑导航 -->
          <nav class="drive-page__breadcrumb" aria-label="文件夹路径">
            <el-tooltip v-if="!store.isRoot" content="返回上一级" placement="bottom">
              <el-button
                :icon="ArrowUpBold"
                text
                circle
                aria-label="返回上一级"
                @click="goToParent"
              />
            </el-tooltip>
            <el-breadcrumb separator="/">
              <el-breadcrumb-item @click="goToRoot">
                <span class="drive-page__breadcrumb-root">全部文件</span>
              </el-breadcrumb-item>
              <el-breadcrumb-item
                v-for="crumb in store.breadcrumbs"
                :key="crumb.id"
                @click="goToBreadcrumb(crumb.id)"
              >
                <span class="drive-page__breadcrumb-link">{{ crumb.name }}</span>
              </el-breadcrumb-item>
            </el-breadcrumb>
          </nav>

          <!-- 文件列表 -->
          <PageState
            :state="pageState"
            empty-text="此文件夹为空"
            error-text="加载文件列表失败"
            @retry="store.refreshCurrentView()"
          >
            <el-table
              v-loading="store.loading"
              :data="store.sortedFiles"
              class="drive-page__table"
              @selection-change="onSelectionChange"
              @sort-change="onSortChange"
              @row-click="onRowClick"
              @row-dblclick="onRowDblclick"
            >
              <el-table-column type="selection" width="48" />
              <el-table-column :label="'名称'" min-width="320" prop="name" sortable="custom">
                <template #default="{ row }">
                  <div class="drive-page__name-cell">
                    <FileIcon :is-folder="row.type === 'folder'" :mime-type="row.mime_type ?? ''" :size="24" />
                    <span class="drive-page__name-text">{{ row.name }}</span>
                  </div>
                </template>
              </el-table-column>

              <el-table-column :label="'大小'" width="140" prop="size" sortable="custom">
                <template #default="{ row }">
                  <SizeDisplay v-if="row.size != null" :bytes="row.size" />
                  <span v-else class="drive-page__dash">—</span>
                </template>
              </el-table-column>

              <el-table-column :label="'修改时间'" width="200" prop="updated_at" sortable="custom">
                <template #default="{ row }">
                  <TimeDisplay :time="row.updated_at" format="absolute" />
                </template>
              </el-table-column>

              <el-table-column :label="'类型'" width="120">
                <template #default="{ row }">
                  {{ row.type === 'folder' ? '文件夹' : formatMimeType(row.mime_type) }}
                </template>
              </el-table-column>
            </el-table>

            <!-- 分页 -->
            <Pagination
              v-if="store.pagination"
              :total="store.pagination.total"
              :page="store.pagination.page"
              :page-size="store.pagination.page_size"
              @change="onPageChange"
            />
          </PageState>
        </section>
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useDriveStore } from '@/stores/drive';
import { useTransferStore } from '@/stores';
import PageState from '@/components/base/PageState.vue';
import FileIcon from '@/components/base/FileIcon.vue';
import SizeDisplay from '@/components/base/SizeDisplay.vue';
import TimeDisplay from '@/components/base/TimeDisplay.vue';
import Pagination from '@/components/base/Pagination.vue';
import DriveToolbar from '@/components/drive/DriveToolbar.vue';
import FolderTree from '@/components/drive/FolderTree.vue';
import { ArrowUpBold } from '@element-plus/icons-vue';
import type { FileItem, SearchResultItem } from '@/types';

const store = useDriveStore();
const transferStore = useTransferStore();
const route = useRoute();
const router = useRouter();

const pageState = computed<'loading' | 'empty' | 'content'>(() => {
  if (store.loading) return 'loading';
  if (store.files.length === 0) return 'empty';
  return 'content';
});

const searchPageState = computed<'loading' | 'empty' | 'content'>(() => {
  if (store.loading) return 'loading';
  if (store.searchResults.length === 0) return 'empty';
  return 'content';
});

const currentFolderName = computed(() => {
  return store.breadcrumbs.at(-1)?.name ?? '全部文件';
});

function goToRoot(): void {
  router.replace({ path: '/drive' });
}

function goToBreadcrumb(folderId: number): void {
  router.push({ path: '/drive', query: folderId === 0 ? {} : { folderId: String(folderId) } });
}

function goToParent(): void {
  const parentId = store.breadcrumbs.at(-2)?.id ?? 0;
  goToBreadcrumb(parentId);
}

function onSelectionChange(items: FileItem[]): void {
  store.setSelection(items.map((item) => item.id));
}

function onSortChange({ prop, order }: { prop: string; order: string | null }): void {
  if (!prop || !order) {
    store.setSortBy('updated_at');
    store.setSortOrder('desc');
  } else {
    store.setSortBy(prop);
    store.setSortOrder(order === 'ascending' ? 'asc' : 'desc');
  }
  store.fetchFiles(1);
}

function onRowClick(row: FileItem): void {
  if (row.type === 'folder') {
    router.push({ path: '/drive', query: { folderId: String(row.id) } });
  }
}

function onRowDblclick(row: FileItem): void {
  if (row.type === 'file') {
    transferStore.addDownloadTask(row.id, row.name, row.size ?? 0);
  }
}

function onSearchRowClick(row: SearchResultItem): void {
  store.clearSearch();
  if (row.type === 'folder') {
    router.push({ path: '/drive', query: { folderId: String(row.id) } });
  } else {
    // For files, just clear search and stay in current folder view
    router.replace({ path: '/drive' });
  }
}

function onPageChange(page: number, _pageSize: number): void {
  store.fetchFiles(page);
}

function formatMimeType(mime?: string): string {
  if (!mime) return '文件';
  if (mime.startsWith('image/')) return '图片';
  if (mime.startsWith('video/')) return '视频';
  if (mime.startsWith('audio/')) return '音频';
  if (mime === 'application/pdf') return 'PDF';
  if (
    mime === 'application/zip' ||
    mime.startsWith('application/x-rar') ||
    mime.startsWith('application/x-7z') ||
    mime.startsWith('application/x-tar') ||
    mime.startsWith('application/gzip')
  ) return '压缩包';
  if (mime.startsWith('application/vnd.')) return '文档';
  if (mime.startsWith('text/')) return '文本';
  return '文件';
}

function clearSearchAndGoBack(): void {
  store.clearSearch();
  router.replace({ path: '/drive' });
}

async function executeSearch(): Promise<void> {
  const q = route.query.q;
  if (typeof q === 'string' && q.trim()) {
    await store.searchFiles(q.trim());
  }
}

async function initFromRoute(): Promise<void> {
  const searchQ = route.query.q;
  if (typeof searchQ === 'string' && searchQ.trim()) {
    await store.searchFiles(searchQ.trim());
    return;
  }

  // Clear search if no q param
  if (store.isSearching) {
    store.clearSearch();
  }

  const folderIdParam = route.query.folderId;
  const folderId = folderIdParam ? Number(folderIdParam) : 0;
  if (!Number.isNaN(folderId)) {
    await store.navigateToFolder(folderId);
  }
}

watch(() => [route.query.folderId, route.query.q], () => {
  initFromRoute();
});

onMounted(() => {
  initFromRoute();
});
</script>

<style scoped>
.drive-page {
  height: 100%;
  display: flex;
  flex-direction: column;
}

.drive-page__workspace {
  display: grid;
  grid-template-columns: 220px minmax(0, 1fr);
  flex: 1;
  min-height: 0;
  background: var(--el-bg-color);
}

.drive-page__tree {
  min-width: 0;
  padding: 12px;
  overflow: auto;
  border-right: 1px solid var(--el-border-color-lighter);
}

.drive-page__content {
  display: flex;
  flex-direction: column;
  min-width: 0;
  min-height: 0;
}

.drive-page__breadcrumb {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.drive-page__breadcrumb-root {
  cursor: pointer;
  font-weight: 500;
}

.drive-page__breadcrumb-link {
  cursor: pointer;
}

.drive-page__table {
  flex: 1;
}

.drive-page__table :deep(.el-table__row) {
  cursor: default;
}

.drive-page__table :deep(.el-table__row:hover) {
  cursor: default;
}

.drive-page__name-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.drive-page__name-text {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.drive-page__dash {
  color: var(--el-text-color-placeholder);
}

/* ==================== Search ==================== */
.drive-page__search-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.drive-page__search-info {
  display: flex;
  align-items: baseline;
  gap: 4px;
}

.drive-page__search-label {
  font-size: 15px;
  font-weight: 500;
  color: var(--el-text-color-primary);
}

.drive-page__search-count {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.drive-page__path-text {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
</style>
