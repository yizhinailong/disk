import { ref } from 'vue';
import { defineStore } from 'pinia';
import { ElMessage } from 'element-plus';
import type {
  ShareItem,
  ShareDetailResponse,
  Pagination,
  CreateShareRequest,
  CreateShareResponse,
  UpdateShareRequest,
  AccessShareResponse,
} from '@/types';
import {
  createShare as apiCreateShare,
  listShares,
  getShareDetail,
  updateShare as apiUpdateShare,
  cancelShares as apiCancelShares,
  accessShare as apiAccessShare,
} from '@/api/share';

export const useShareStore = defineStore('share', () => {
  // ==================== State ====================
  const shares = ref<ShareItem[]>([]);
  const currentShare = ref<ShareDetailResponse | null>(null);
  const shareToken = ref<string | null>(null);
  const loading = ref<boolean>(false);
  const pagination = ref<Pagination | null>(null);
  const statusFilter = ref<string>('all');

  // ==================== Actions ====================
  async function fetchShares(page?: number): Promise<void> {
    loading.value = true;
    try {
      const result = await listShares({
        page: page ?? 1,
        page_size: 20,
        status: statusFilter.value as 'all' | 'active' | 'expired' | 'cancelled',
      });
      shares.value = [...result.items];
      pagination.value = result.pagination;
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : '获取分享列表失败';
      ElMessage.error(msg);
    } finally {
      loading.value = false;
    }
  }

  async function fetchShareDetail(shareId: string): Promise<void> {
    loading.value = true;
    try {
      currentShare.value = await getShareDetail(shareId);
    } catch (err: unknown) {
      currentShare.value = null;
      const msg = err instanceof Error ? err.message : '获取分享详情失败';
      ElMessage.error(msg);
    } finally {
      loading.value = false;
    }
  }

  async function createShare(data: CreateShareRequest): Promise<CreateShareResponse> {
    loading.value = true;
    try {
      const result = await apiCreateShare(data);
      return result;
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : '创建分享失败';
      ElMessage.error(msg);
      throw err;
    } finally {
      loading.value = false;
    }
  }

  async function updateShare(shareId: string, data: UpdateShareRequest): Promise<void> {
    loading.value = true;
    try {
      await apiUpdateShare(shareId, data);
      ElMessage.success('分享已更新');
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : '更新分享失败';
      ElMessage.error(msg);
      throw err;
    } finally {
      loading.value = false;
    }
  }

  async function cancelShares(shareIds: string[]): Promise<void> {
    loading.value = true;
    try {
      const result = await apiCancelShares({ share_ids: shareIds });
      const { succeeded, failed } = result.summary;
      if (failed > 0) {
        ElMessage.warning(`已取消 ${succeeded} 项，${failed} 项取消失败`);
      } else {
        ElMessage.success(`已取消 ${succeeded} 个分享`);
      }
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : '取消分享失败';
      ElMessage.error(msg);
      throw err;
    } finally {
      loading.value = false;
    }
  }

  async function accessShare(shareId: string, password?: string): Promise<AccessShareResponse> {
    loading.value = true;
    try {
      const result = await apiAccessShare(shareId, { password });
      shareToken.value = result.share_token;
      return result;
    } catch (err: unknown) {
      shareToken.value = null;
      const msg = err instanceof Error ? err.message : '访问分享失败';
      ElMessage.error(msg);
      throw err;
    } finally {
      loading.value = false;
    }
  }

  function clearShareToken(): void {
    shareToken.value = null;
  }

  return {
    // state
    shares,
    currentShare,
    shareToken,
    loading,
    pagination,
    statusFilter,
    // actions
    fetchShares,
    fetchShareDetail,
    createShare,
    updateShare,
    cancelShares,
    accessShare,
    clearShareToken,
  };
});
