<template>
  <div class="admin-shares-page">
    <!-- 页头 -->
    <div class="admin-shares-page__header">
      <h2 class="admin-shares-page__title">分享管理</h2>
      <p class="admin-shares-page__desc">查看和管理系统中所有用户的分享链接</p>
    </div>

    <!-- 搜索和过滤 -->
    <div class="admin-shares-page__toolbar">
      <el-input
        v-model="searchText"
        placeholder="搜索用户名"
        clearable
        style="width: 240px"
        @keyup.enter="handleSearch"
        @clear="handleSearch"
      >
        <template #prefix>
          <el-icon><Search /></el-icon>
        </template>
      </el-input>

      <el-select
        v-model="statusFilter"
        placeholder="分享状态"
        clearable
        style="width: 140px"
        @change="handleFilterChange"
      >
        <el-option label="有效" :value="0" />
        <el-option label="已过期" :value="1" />
        <el-option label="已取消" :value="2" />
      </el-select>
    </div>

    <!-- 内容区域 -->
    <PageState
      :state="pageState"
      empty-text="暂无分享记录"
      error-text="加载分享列表失败"
      @retry="loadShares"
    >
      <el-table
        :data="adminStore.shares"
        v-loading="adminStore.loading"
        stripe
        style="width: 100%"
      >
        <el-table-column prop="share_id" label="分享ID" min-width="120" show-overflow-tooltip />
        <el-table-column prop="username" label="用户" min-width="100" show-overflow-tooltip />
        <el-table-column prop="file_name" label="文件名" min-width="160" show-overflow-tooltip />
        <el-table-column prop="file_count" label="文件数" width="80" align="center" />
        <el-table-column label="密码" width="70" align="center">
          <template #default="{ row }">
            <el-icon v-if="row.has_password" :size="16" color="var(--el-color-warning)">
              <Lock />
            </el-icon>
            <el-icon v-else :size="16" color="var(--el-color-info)">
              <Open />
            </el-icon>
          </template>
        </el-table-column>
        <el-table-column label="权限" width="80" align="center">
          <template #default="{ row }">
            {{ row.permission === 'download' ? '下载' : '查看' }}
          </template>
        </el-table-column>
        <el-table-column label="状态" width="90" align="center">
          <template #default="{ row }">
            <el-tag :type="statusTagType(row.status)" size="small">
              {{ statusLabel(row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="创建时间" width="160">
          <template #default="{ row }">
            <TimeDisplay :time="row.created_at" format="absolute" />
          </template>
        </el-table-column>
        <el-table-column label="过期时间" width="160">
          <template #default="{ row }">
            <TimeDisplay :time="row.expires_at" format="absolute" />
          </template>
        </el-table-column>
        <el-table-column label="操作" width="160" fixed="right">
          <template #default="{ row }">
            <el-button link type="primary" size="small" @click="openDetail(row.share_id)">
              详情
            </el-button>
            <el-button
              v-if="row.status === 0"
              link
              type="danger"
              size="small"
              @click="handleForceCancel(row)"
            >
              强制取消
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <!-- 分页 -->
      <div v-if="adminStore.sharePagination" class="admin-shares-page__pagination">
        <el-pagination
          :current-page="adminStore.sharePagination.page"
          :page-size="adminStore.sharePagination.page_size"
          :total="adminStore.sharePagination.total"
          :page-sizes="[10, 20, 50]"
          layout="total, sizes, prev, pager, next, jumper"
          @current-change="handlePageChange"
          @size-change="handleSizeChange"
        />
      </div>
    </PageState>

    <!-- 分享详情对话框 -->
    <el-dialog
      v-model="detailVisible"
      title="分享详情"
      width="680px"
      destroy-on-close
    >
      <template v-if="adminStore.currentShareDetail">
        <!-- 基本信息 -->
        <el-descriptions :column="2" border>
          <el-descriptions-item label="分享ID">
            {{ adminStore.currentShareDetail.share_id }}
          </el-descriptions-item>
          <el-descriptions-item label="用户">
            {{ adminStore.currentShareDetail.username }}
          </el-descriptions-item>
          <el-descriptions-item label="分享链接">
            <div style="display: flex; align-items: center; gap: 8px;">
              <span class="share-link-text">{{ adminStore.currentShareDetail.share_link }}</span>
              <el-button link type="primary" size="small" @click="copyLink">
                复制链接
              </el-button>
            </div>
          </el-descriptions-item>
          <el-descriptions-item label="访问密码">
            <el-icon v-if="adminStore.currentShareDetail.has_password" :size="16" color="var(--el-color-warning)">
              <Lock />
            </el-icon>
            <span v-else>无</span>
          </el-descriptions-item>
          <el-descriptions-item label="权限">
            {{ adminStore.currentShareDetail.permission === 'download' ? '下载' : '查看' }}
          </el-descriptions-item>
          <el-descriptions-item label="状态">
            <el-tag :type="statusTagType(Number(adminStore.currentShareDetail.status))" size="small">
              {{ statusLabel(Number(adminStore.currentShareDetail.status)) }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="浏览次数">
            {{ adminStore.currentShareDetail.view_count }}
          </el-descriptions-item>
          <el-descriptions-item label="下载次数">
            {{ adminStore.currentShareDetail.download_count }}
          </el-descriptions-item>
          <el-descriptions-item label="创建时间">
            <TimeDisplay :time="adminStore.currentShareDetail.created_at" format="absolute" />
          </el-descriptions-item>
          <el-descriptions-item label="过期时间">
            <TimeDisplay :time="adminStore.currentShareDetail.expires_at" format="absolute" />
          </el-descriptions-item>
        </el-descriptions>

        <!-- 关联文件列表 -->
        <h4 class="detail-files-title">关联文件</h4>
        <el-table
          :data="adminStore.currentShareDetail.files"
          stripe
          size="small"
          max-height="280"
        >
          <el-table-column prop="name" label="文件名" min-width="200" show-overflow-tooltip />
          <el-table-column label="大小" width="100" align="right">
            <template #default="{ row }">
              {{ formatFileSize(row.size) }}
            </template>
          </el-table-column>
          <el-table-column label="类型" width="80" align="center">
            <template #default="{ row }">
              {{ row.type === 'folder' ? '文件夹' : '文件' }}
            </template>
          </el-table-column>
        </el-table>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { Search, Lock, Open } from '@element-plus/icons-vue';
import { ElMessage, ElMessageBox } from 'element-plus';
import { useAdminStore } from '@/stores/admin';
import PageState from '@/components/base/PageState.vue';
import TimeDisplay from '@/components/base/TimeDisplay.vue';

const adminStore = useAdminStore();

const searchText = ref('');
const statusFilter = ref<number | undefined>(undefined);
const detailVisible = ref(false);

// ==================== 计算属性 ====================

const pageState = computed<'loading' | 'empty' | 'error' | 'content'>(() => {
  if (adminStore.loading && adminStore.shares.length === 0) return 'loading';
  if (adminStore.shares.length === 0) return 'empty';
  return 'content';
});

// ==================== 辅助函数 ====================

function statusLabel(status: number): string {
  switch (status) {
    case 0: return '有效';
    case 1: return '已过期';
    case 2: return '已取消';
    default: return '未知';
  }
}

function statusTagType(status: number): 'success' | 'info' | 'danger' {
  switch (status) {
    case 0: return 'success';
    case 1: return 'info';
    case 2: return 'danger';
    default: return 'info';
  }
}

function formatFileSize(bytes: number): string {
  if (bytes === 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(1024));
  return `${(bytes / Math.pow(1024, i)).toFixed(i > 0 ? 1 : 0)} ${units[i]}`;
}

// ==================== 数据加载 ====================

function loadShares(page = 1, pageSize = 20) {
  adminStore.fetchShares({
    page,
    pageSize,
    username: searchText.value || undefined,
    status: statusFilter.value,
  });
}

function handleSearch() {
  loadShares(1, adminStore.sharePagination?.page_size ?? 20);
}

function handleFilterChange() {
  loadShares(1, adminStore.sharePagination?.page_size ?? 20);
}

function handlePageChange(page: number) {
  loadShares(page, adminStore.sharePagination?.page_size ?? 20);
}

function handleSizeChange(size: number) {
  loadShares(1, size);
}

// ==================== 操作 ====================

async function openDetail(shareId: string) {
  detailVisible.value = true;
  await adminStore.fetchShareDetail(shareId);
}

async function handleForceCancel(row: { share_id: string; file_name: string }) {
  try {
    await ElMessageBox.confirm(
      `确定要强制取消分享「${row.file_name}」吗？此操作不可恢复。`,
      '强制取消分享',
      {
        confirmButtonText: '确定取消',
        cancelButtonText: '返回',
        type: 'warning',
        confirmButtonClass: 'el-button--danger',
      },
    );
    await adminStore.deleteShare(row.share_id);
    ElMessage.success('已强制取消分享');
  } catch {
    // 用户取消或删除失败，静默处理
  }
}

async function copyLink() {
  if (!adminStore.currentShareDetail?.share_link) return;
  try {
    await navigator.clipboard.writeText(adminStore.currentShareDetail.share_link);
    ElMessage.success('分享链接已复制');
  } catch {
    ElMessage.error('复制失败，请手动复制');
  }
}

// ==================== 初始化 ====================

onMounted(() => {
  loadShares();
});
</script>

<style scoped>
.admin-shares-page {
  padding: 24px;
}

.admin-shares-page__header {
  margin-bottom: 20px;
}

.admin-shares-page__title {
  margin: 0 0 4px;
  font-size: 20px;
  font-weight: 600;
}

.admin-shares-page__desc {
  margin: 0;
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.admin-shares-page__toolbar {
  display: flex;
  gap: 12px;
  margin-bottom: 16px;
  align-items: center;
}

.admin-shares-page__pagination {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}

.share-link-text {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 260px;
  display: inline-block;
}

.detail-files-title {
  margin: 20px 0 12px;
  font-size: 15px;
  font-weight: 600;
}
</style>
