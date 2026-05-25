import { ref } from 'vue';
import { defineStore } from 'pinia';
import { ElMessage } from 'element-plus';
import {
  listUsers,
  getUserDetail,
  changeUserStatus as apiChangeUserStatus,
  changeUserRole as apiChangeUserRole,
  deleteUser as apiDeleteUser,
  listShares,
  getShareDetail,
  deleteShare as apiDeleteShare,
  getAdminLogs,
  getStatsOverview,
  getStatsSystem,
  getStorageStats,
} from '@/api/admin';
import type {
  AdminUserItem,
  AdminUserDetailResponse,
  AdminShareItem,
  AdminShareDetailResponse,
  AdminStatsOverviewResponse,
  AdminStatsSystemResponse,
  AdminStorageStatsResponse,
  AdminListUsersQuery,
  AdminLogListQuery,
  AdminLogItem,
  Pagination,
} from '@/types';

export const useAdminStore = defineStore('admin', () => {
  // ==================== State ====================
  const users = ref<AdminUserItem[]>([]);
  const currentUserDetail = ref<AdminUserDetailResponse | null>(null);
  const shares = ref<AdminShareItem[]>([]);
  const currentShareDetail = ref<AdminShareDetailResponse | null>(null);
  const adminLogs = ref<AdminLogItem[]>([]);
  const statsOverview = ref<AdminStatsOverviewResponse | null>(null);
  const statsSystem = ref<AdminStatsSystemResponse | null>(null);
  const storageStats = ref<AdminStorageStatsResponse | null>(null);
  const loading = ref<boolean>(false);
  const userPagination = ref<Pagination | null>(null);
  const sharePagination = ref<Pagination | null>(null);
  const logPagination = ref<Pagination | null>(null);

  // ==================== Actions ====================
  async function fetchUsers(params?: AdminListUsersQuery): Promise<void> {
    loading.value = true;
    try {
      const res = await listUsers(params ?? {});
      users.value = [...res.items];
      userPagination.value = res.pagination;
    } finally {
      loading.value = false;
    }
  }

  async function fetchUserDetail(userId: number): Promise<void> {
    loading.value = true;
    try {
      currentUserDetail.value = await getUserDetail(userId);
    } finally {
      loading.value = false;
    }
  }

  async function changeUserStatus(
    userId: number,
    status: number,
  ): Promise<void> {
    await apiChangeUserStatus(userId, { status });
    const idx = users.value.findIndex((u) => u.id === userId);
    if (idx !== -1) {
      users.value[idx] = { ...users.value[idx], status };
    }
    ElMessage.success('状态修改成功');
  }

  async function changeUserRole(
    userId: number,
    role: number,
  ): Promise<void> {
    await apiChangeUserRole(userId, { role });
    const idx = users.value.findIndex((u) => u.id === userId);
    if (idx !== -1) {
      users.value[idx] = { ...users.value[idx], role };
    }
    ElMessage.success('角色修改成功');
  }

  async function deleteUser(userId: number): Promise<void> {
    await apiDeleteUser(userId);
    users.value = users.value.filter((u) => u.id !== userId);
    if (userPagination.value) {
      userPagination.value = {
        ...userPagination.value,
        total: userPagination.value.total - 1,
      };
    }
  }

  async function fetchShares(params: {
    page?: number;
    pageSize?: number;
    username?: string;
    status?: number;
  } = {}): Promise<void> {
    loading.value = true;
    try {
      const query: Record<string, unknown> = {
        page: params.page ?? 1,
        page_size: params.pageSize ?? 20,
      };
      if (params.username) {
        query.username = params.username;
      }
      if (params.status !== undefined) {
        query.status = params.status;
      }
      const res = await listShares(query as import('@/types').AdminListSharesQuery);
      shares.value = [...res.items];
      sharePagination.value = { ...res.pagination };
    } finally {
      loading.value = false;
    }
  }

  async function fetchShareDetail(shareId: number): Promise<void> {
    loading.value = true;
    try {
      currentShareDetail.value = await getShareDetail(shareId);
    } finally {
      loading.value = false;
    }
  }

  async function deleteShare(shareId: number): Promise<void> {
    loading.value = true;
    try {
      await apiDeleteShare(shareId);
      shares.value = shares.value.filter((s) => s.id !== shareId);
      if (currentShareDetail.value?.id === shareId) {
        currentShareDetail.value = null;
      }
      if (sharePagination.value) {
        sharePagination.value = {
          ...sharePagination.value,
          total: sharePagination.value.total - 1,
        };
      }
    } finally {
      loading.value = false;
    }
  }

  async function fetchAdminLogs(params: AdminLogListQuery): Promise<void> {
    loading.value = true;
    try {
      const res = await getAdminLogs(params);
      adminLogs.value = [...res.items];
      logPagination.value = { ...res.pagination };
    } catch {
      ElMessage.error('加载操作日志失败');
    } finally {
      loading.value = false;
    }
  }

  async function fetchStatsOverview(): Promise<void> {
    loading.value = true;
    try {
      statsOverview.value = await getStatsOverview();
    } finally {
      loading.value = false;
    }
  }

  async function fetchStatsSystem(): Promise<void> {
    loading.value = true;
    try {
      statsSystem.value = await getStatsSystem();
    } finally {
      loading.value = false;
    }
  }

  async function fetchStorageStats(): Promise<void> {
    loading.value = true;
    try {
      storageStats.value = await getStorageStats();
    } finally {
      loading.value = false;
    }
  }

  return {
    // state
    users,
    currentUserDetail,
    shares,
    currentShareDetail,
    adminLogs,
    statsOverview,
    statsSystem,
    storageStats,
    loading,
    userPagination,
    sharePagination,
    logPagination,
    // actions
    fetchUsers,
    fetchUserDetail,
    changeUserStatus,
    changeUserRole,
    deleteUser,
    fetchShares,
    fetchShareDetail,
    deleteShare,
    fetchAdminLogs,
    fetchStatsOverview,
    fetchStatsSystem,
    fetchStorageStats,
  };
});
