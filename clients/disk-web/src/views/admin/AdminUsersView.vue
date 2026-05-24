<template>
  <div class="admin-users-page">
    <!-- 顶部工具栏：搜索 + 筛选 -->
    <div class="admin-users-page__toolbar">
      <div class="admin-users-page__filters">
        <el-input
          v-model="searchText"
          class="admin-users-page__search"
          placeholder="搜索用户名或邮箱"
          clearable
          :prefix-icon="Search"
          @keyup.enter="onSearch"
          @clear="onSearch"
        />
        <el-select
          v-model="filterStatus"
          placeholder="状态"
          clearable
          class="admin-users-page__filter-select"
          @change="onSearch"
        >
          <el-option label="正常" :value="1" />
          <el-option label="禁用" :value="0" />
          <el-option label="锁定" :value="2" />
        </el-select>
        <el-select
          v-model="filterRole"
          placeholder="角色"
          clearable
          class="admin-users-page__filter-select"
          @change="onSearch"
        >
          <el-option label="普通用户" :value="0" />
          <el-option label="管理员" :value="1" />
        </el-select>
      </div>
    </div>

    <!-- 用户列表 -->
    <PageState
      :state="pageState"
      empty-text="暂无用户数据"
      error-text="加载用户列表失败"
      @retry="loadUsers"
    >
      <el-table
        v-loading="store.loading"
        :data="store.users"
        class="admin-users-page__table"
      >
        <el-table-column label="ID" width="70" prop="id" />

        <el-table-column label="用户名" min-width="120" prop="username" />

        <el-table-column label="邮箱" min-width="180" prop="email">
          <template #default="{ row }">
            <span class="admin-users-page__email">{{ row.email }}</span>
          </template>
        </el-table-column>

        <el-table-column label="昵称" min-width="100" prop="nickname" />

        <el-table-column label="状态" width="90" align="center">
          <template #default="{ row }">
            <el-tag
              :type="statusTagType(row.status)"
              size="small"
              disable-transitions
            >
              {{ statusLabel(row.status) }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="角色" width="100" align="center">
          <template #default="{ row }">
            <el-tag
              :type="roleTagType(row.role)"
              size="small"
              disable-transitions
            >
              {{ roleLabel(row.role) }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="已用空间" width="120" align="right">
          <template #default="{ row }">
            <SizeDisplay :bytes="row.storage_used" />
          </template>
        </el-table-column>

        <el-table-column label="注册时间" width="170" prop="created_at">
          <template #default="{ row }">
            <TimeDisplay :time="row.created_at" format="absolute" />
          </template>
        </el-table-column>

        <el-table-column label="操作" width="220" fixed="right" align="center">
          <template #default="{ row }">
            <el-dropdown trigger="click" @command="(cmd: string | number | object) => onStatusCommand(row, cmd as number)">
              <el-button text type="primary" size="small">
                状态 <el-icon class="el-icon--right"><ArrowDown /></el-icon>
              </el-button>
              <template #dropdown>
                <el-dropdown-menu>
                  <el-dropdown-item :command="1" :disabled="row.status === 1">正常</el-dropdown-item>
                  <el-dropdown-item :command="0" :disabled="row.status === 0">禁用</el-dropdown-item>
                  <el-dropdown-item :command="2" :disabled="row.status === 2">锁定</el-dropdown-item>
                </el-dropdown-menu>
              </template>
            </el-dropdown>

            <el-dropdown trigger="click" @command="(cmd: string | number | object) => onRoleCommand(row, cmd as number)">
              <el-button text type="primary" size="small">
                角色 <el-icon class="el-icon--right"><ArrowDown /></el-icon>
              </el-button>
              <template #dropdown>
                <el-dropdown-menu>
                  <el-dropdown-item :command="0" :disabled="row.role === 0">普通用户</el-dropdown-item>
                  <el-dropdown-item :command="1" :disabled="row.role === 1">管理员</el-dropdown-item>
                </el-dropdown-menu>
              </template>
            </el-dropdown>

            <el-button text type="primary" size="small" @click="showDetail(row.id)">
              详情
            </el-button>

            <el-button text type="danger" size="small" @click="onDelete(row)">
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <!-- 分页 -->
      <Pagination
        v-if="store.userPagination"
        :total="store.userPagination.total"
        :page="store.userPagination.page"
        :page-size="store.userPagination.page_size"
        :page-sizes="[10, 20, 50]"
        @change="onPageChange"
      />
    </PageState>

    <!-- 用户详情对话框 -->
    <el-dialog
      v-model="detailVisible"
      title="用户详情"
      width="520px"
      destroy-on-close
    >
      <template v-if="store.currentUserDetail">
        <el-descriptions :column="1" border>
          <el-descriptions-item label="ID">
            {{ store.currentUserDetail.id }}
          </el-descriptions-item>
          <el-descriptions-item label="用户名">
            {{ store.currentUserDetail.username }}
          </el-descriptions-item>
          <el-descriptions-item label="邮箱">
            {{ store.currentUserDetail.email }}
          </el-descriptions-item>
          <el-descriptions-item label="昵称">
            {{ store.currentUserDetail.nickname }}
          </el-descriptions-item>
          <el-descriptions-item label="状态">
            <el-tag :type="statusTagType(store.currentUserDetail.status)" size="small" disable-transitions>
              {{ statusLabel(store.currentUserDetail.status) }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="角色">
            <el-tag :type="roleTagType(store.currentUserDetail.role)" size="small" disable-transitions>
              {{ roleLabel(store.currentUserDetail.role) }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="已用空间">
            <SizeDisplay :bytes="store.currentUserDetail.storage_used" />
          </el-descriptions-item>
          <el-descriptions-item label="存储配额">
            <SizeDisplay :bytes="store.currentUserDetail.storage_quota" />
          </el-descriptions-item>
          <el-descriptions-item label="文件数">
            {{ store.currentUserDetail.file_count }}
          </el-descriptions-item>
          <el-descriptions-item label="文件夹数">
            {{ store.currentUserDetail.folder_count }}
          </el-descriptions-item>
          <el-descriptions-item label="注册时间">
            <TimeDisplay :time="store.currentUserDetail.created_at" format="absolute" />
          </el-descriptions-item>
          <el-descriptions-item label="更新时间">
            <TimeDisplay :time="store.currentUserDetail.updated_at" format="absolute" />
          </el-descriptions-item>
        </el-descriptions>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { Search, ArrowDown } from '@element-plus/icons-vue';
import { ElMessage, ElMessageBox } from 'element-plus';
import { useAdminStore } from '@/stores/admin';
import PageState from '@/components/base/PageState.vue';
import SizeDisplay from '@/components/base/SizeDisplay.vue';
import TimeDisplay from '@/components/base/TimeDisplay.vue';
import Pagination from '@/components/base/Pagination.vue';
import type { AdminUserItem } from '@/types';

const store = useAdminStore();

const searchText = ref('');
const filterStatus = ref<number | undefined>(undefined);
const filterRole = ref<number | undefined>(undefined);
const detailVisible = ref(false);
const currentPage = ref(1);
const currentPageSize = ref(20);

const pageState = computed<'loading' | 'empty' | 'content'>(() => {
  if (store.loading) return 'loading';
  if (store.users.length === 0) return 'empty';
  return 'content';
});

function statusLabel(status: number): string {
  if (status === 1) return '正常';
  if (status === 0) return '禁用';
  if (status === 2) return '锁定';
  return '未知';
}

function statusTagType(status: number): 'success' | 'danger' | 'warning' | 'info' {
  if (status === 1) return 'success';
  if (status === 0) return 'danger';
  if (status === 2) return 'warning';
  return 'info';
}

function roleLabel(role: number): string {
  if (role === 1) return '管理员';
  return '普通用户';
}

function roleTagType(role: number): 'warning' | 'info' {
  if (role === 1) return 'warning';
  return 'info';
}

function loadUsers(): void {
  const params: Record<string, unknown> = {
    page: currentPage.value,
    page_size: currentPageSize.value,
  };
  const trimmed = searchText.value.trim();
  if (trimmed) {
    // Backend supports username or email; use username for combined search
    params.username = trimmed;
  }
  if (filterStatus.value !== undefined && filterStatus.value !== null) {
    params.status = filterStatus.value;
  }
  if (filterRole.value !== undefined && filterRole.value !== null) {
    params.role = filterRole.value;
  }
  store.fetchUsers(params);
}

function onSearch(): void {
  currentPage.value = 1;
  loadUsers();
}

function onPageChange(page: number, pageSize: number): void {
  currentPage.value = page;
  currentPageSize.value = pageSize;
  loadUsers();
}

async function onStatusCommand(row: AdminUserItem, status: number): Promise<void> {
  try {
    await store.changeUserStatus(row.id, status);
    ElMessage.success(`用户 ${row.username} 状态已修改为「${statusLabel(status)}」`);
  } catch {
    ElMessage.error('修改用户状态失败');
  }
}

async function onRoleCommand(row: AdminUserItem, role: number): Promise<void> {
  try {
    await store.changeUserRole(row.id, role);
    ElMessage.success(`用户 ${row.username} 角色已修改为「${roleLabel(role)}」`);
  } catch {
    ElMessage.error('修改用户角色失败');
  }
}

async function showDetail(userId: number): Promise<void> {
  try {
    await store.fetchUserDetail(userId);
    detailVisible.value = true;
  } catch {
    ElMessage.error('加载用户详情失败');
  }
}

async function onDelete(row: AdminUserItem): Promise<void> {
  try {
    await ElMessageBox.confirm(
      `确定要删除用户「${row.username}」吗？此操作不可恢复。`,
      '删除确认',
      {
        confirmButtonText: '删除',
        cancelButtonText: '取消',
        type: 'warning',
        confirmButtonClass: 'el-button--danger',
      },
    );
    await store.deleteUser(row.id);
    ElMessage.success(`用户 ${row.username} 已删除`);
  } catch (e) {
    // User cancelled or API error
    if (e !== 'cancel') {
      ElMessage.error('删除用户失败');
    }
  }
}

onMounted(() => {
  loadUsers();
});
</script>

<style scoped>
.admin-users-page {
  height: 100%;
  display: flex;
  flex-direction: column;
}

.admin-users-page__toolbar {
  padding: 12px 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.admin-users-page__filters {
  display: flex;
  gap: 12px;
  align-items: center;
}

.admin-users-page__search {
  width: 260px;
}

.admin-users-page__filter-select {
  width: 130px;
}

.admin-users-page__table {
  flex: 1;
}

.admin-users-page__table :deep(.el-table__row) {
  cursor: default;
}

.admin-users-page__email {
  color: var(--el-text-color-secondary);
  font-size: 13px;
}
</style>
