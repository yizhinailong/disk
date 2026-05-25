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
        <el-option label="已取消" :value="0" />
        <el-option label="有效" :value="1" />
        <el-option label="已过期" :value="2" />
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
        <el-table-column prop="id" label="分享ID" min-width="120" show-overflow-tooltip />
        <el-table-column prop="username" label="用户" min-width="100" show-overflow-tooltip />
        <el-table-column prop="file_name" label="文件名" min-width="160" show-overflow-tooltip />
        <el-table-column label="密码" width="70" align="center">
          <template #default="{ row }">
            <el-icon v-if="row.password_set" :size="16" color="var(--el-color-warning)">
              <Lock />
            </el-icon>
            <el-icon v-else :size="16" color="var(--el-color-info)">
              <Open />
            </el-icon>
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
            <el-button link type="primary" size="small" @click="openDetail(row.id)">
              详情
            </el-button>
            <el-button
              v-if="row.status === 1"
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
            {{ adminStore.currentShareDetail.id }}
          </el-descriptions-item>
          <el-descriptions-item label="用户">
            {{ adminStore.currentShareDetail.username }}
          </el-descriptions-item>
          <el-descriptions-item label="文件ID">
            {{ adminStore.currentShareDetail.file_id }}
          </el-descriptions-item>
          <el-descriptions-item label="文件名">
            {{ adminStore.currentShareDetail.file_name }}
          </el-descriptions-item>
          <el-descriptions-item label="分享码">
            <div style="display: flex; align-items: center; gap: 8px;">
              <span class="share-link-text">{{ adminStore.currentShareDetail.share_code }}</span>
              <el-button link type="primary" size="small" @click="copyLink">
                复制分享码
              </el-button>
            </div>
          </el-descriptions-item>
          <el-descriptions-item label="访问密码">
            <el-icon v-if="adminStore.currentShareDetail.password_set" :size="16" color="var(--el-color-warning)">
              <Lock />
            </el-icon>
            <span v-else>无</span>
          </el-descriptions-item>
          <el-descriptions-item label="状态">
            <el-tag :type="statusTagType(adminStore.currentShareDetail.status)" size="small">
              {{ statusLabel(adminStore.currentShareDetail.status) }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="访问次数">
            {{ adminStore.currentShareDetail.access_count }}
          </el-descriptions-item>
          <el-descriptions-item label="创建时间">
            <TimeDisplay :time="adminStore.currentShareDetail.created_at" format="absolute" />
          </el-descriptions-item>
          <el-descriptions-item label="过期时间">
            <TimeDisplay :time="adminStore.currentShareDetail.expires_at" format="absolute" />
          </el-descriptions-item>
        </el-descriptions>
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
    case 0: return '已取消';
    case 1: return '有效';
    case 2: return '已过期';
    default: return '未知';
  }
}

function statusTagType(status: number): 'success' | 'warning' | 'info' | 'danger' {
  switch (status) {
    case 0: return 'info';
    case 1: return 'success';
    case 2: return 'warning';
    default: return 'danger';
  }
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

async function openDetail(shareId: number) {
  detailVisible.value = true;
  await adminStore.fetchShareDetail(shareId);
}

async function handleForceCancel(row: { id: number; file_name: string }) {
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
    await adminStore.deleteShare(row.id);
    ElMessage.success('已强制取消分享');
  } catch {
    // 用户取消或删除失败，静默处理
  }
}

async function copyLink() {
  if (!adminStore.currentShareDetail?.share_code) return;
  try {
    await navigator.clipboard.writeText(adminStore.currentShareDetail.share_code);
    ElMessage.success('分享码已复制');
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
