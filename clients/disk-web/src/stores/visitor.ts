import { ref, computed } from 'vue';
import { defineStore } from 'pinia';
import { ElMessage } from 'element-plus';
import type {
  AccessShareResponse,
  BrowseItem,
  BrowseBreadcrumb,
  SaveShareResponse,
} from '@/types';
import {
  accessShare as apiAccessShare,
  browseShare as apiBrowseShare,
  downloadShareFile as apiDownloadShareFile,
  saveShareItems as apiSaveShareItems,
} from '@/api/share';

const SESSION_TOKEN_KEY = 'share_token';

export const useVisitorStore = defineStore('visitor', () => {
  // ==================== State ====================
  const shareToken = ref<string | null>(null);
  const shareInfo = ref<AccessShareResponse | null>(null);
  const browseItems = ref<BrowseItem[]>([]);
  const breadcrumb = ref<BrowseBreadcrumb[]>([]);
  const loading = ref(false);

  // ==================== Init: restore from sessionStorage ====================
  const stored = sessionStorage.getItem(SESSION_TOKEN_KEY);
  if (stored) {
    shareToken.value = stored;
  }

  const hasToken = computed(() => !!shareToken.value);

  // ==================== Actions ====================
  async function verifyShare(shareId: string, password?: string): Promise<AccessShareResponse> {
    loading.value = true;
    try {
      const result = await apiAccessShare(shareId, { password });
      shareToken.value = result.share_token;
      shareInfo.value = result;
      sessionStorage.setItem(SESSION_TOKEN_KEY, result.share_token);
      return result;
    } catch (err: unknown) {
      shareToken.value = null;
      shareInfo.value = null;
      sessionStorage.removeItem(SESSION_TOKEN_KEY);
      const msg = err instanceof Error ? err.message : '访问分享失败';
      throw new Error(msg, { cause: err });
    } finally {
      loading.value = false;
    }
  }

  async function fetchBrowseItems(shareId: string, folderId?: number): Promise<void> {
    if (!shareToken.value) return;
    loading.value = true;
    try {
      const result = await apiBrowseShare(shareId, { folder_id: folderId }, shareToken.value);
      browseItems.value = [...result.items];
      breadcrumb.value = [...result.breadcrumb];
    } catch (err: unknown) {
      browseItems.value = [];
      breadcrumb.value = [];
      const msg = err instanceof Error ? err.message : '加载分享内容失败';
      ElMessage.error(msg);
    } finally {
      loading.value = false;
    }
  }

  async function downloadFile(shareId: string, fileId: number, fileName: string): Promise<void> {
    if (!shareToken.value) return;
    try {
      const blob = await apiDownloadShareFile(shareId, fileId, shareToken.value);
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = fileName;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : '下载失败';
      ElMessage.error(msg);
    }
  }

  async function saveToMyDrive(
    shareId: string,
    fileIds: readonly number[],
    folderIds: readonly number[],
    targetFolderId: number,
  ): Promise<SaveShareResponse> {
    if (!shareToken.value) throw new Error('未获取分享令牌');

    const result = await apiSaveShareItems(
      shareId,
      {
        file_ids: fileIds.length > 0 ? fileIds : undefined,
        folder_ids: folderIds.length > 0 ? folderIds : undefined,
        target_folder_id: targetFolderId || undefined,
      },
      shareToken.value,
    );

    const { summary } = result;
    if (summary.failed > 0) {
      ElMessage.warning(`已保存 ${summary.succeeded} 项，${summary.failed} 项失败`);
    } else {
      ElMessage.success(`已保存 ${summary.succeeded} 项到我的网盘`);
    }
    return result;
  }

  function clearToken(): void {
    shareToken.value = null;
    shareInfo.value = null;
    browseItems.value = [];
    breadcrumb.value = [];
    sessionStorage.removeItem(SESSION_TOKEN_KEY);
  }

  return {
    // state
    shareToken,
    shareInfo,
    browseItems,
    breadcrumb,
    loading,
    // getters
    hasToken,
    // actions
    verifyShare,
    fetchBrowseItems,
    downloadFile,
    saveToMyDrive,
    clearToken,
  };
});
