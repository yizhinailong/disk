import { ref } from 'vue';
import { defineStore } from 'pinia';
import type {
  TrashItem,
  Pagination,
  TrashRestoreRequest,
  TrashRestoreResponse,
  TrashDeleteRequest,
  TrashDeleteResponse,
  TrashDeleteAllResponse,
} from '@/types';
import { listTrash, restoreTrash, deleteTrash, deleteAllTrash } from '@/api/trash';

export const useTrashStore = defineStore('trash', () => {
  // ==================== State ====================
  const items = ref<TrashItem[]>([]);
  const loading = ref<boolean>(false);
  const pagination = ref<Pagination | null>(null);

  // ==================== Actions ====================
  async function fetchTrashItems(page?: number): Promise<void> {
    loading.value = true;
    try {
      const res = await listTrash({ page: page ?? 1 });
      items.value = [...res.items];
      pagination.value = res.pagination;
    } finally {
      loading.value = false;
    }
  }

  async function restoreItems(
    data: TrashRestoreRequest,
  ): Promise<TrashRestoreResponse> {
    const res = await restoreTrash(data);
    await fetchTrashItems(pagination.value?.page ?? 1);
    return res;
  }

  async function deletePermanently(
    data: TrashDeleteRequest,
  ): Promise<TrashDeleteResponse> {
    const res = await deleteTrash(data);
    await fetchTrashItems(pagination.value?.page ?? 1);
    return res;
  }

  async function emptyTrash(): Promise<TrashDeleteAllResponse> {
    const res = await deleteAllTrash();
    await fetchTrashItems(1);
    return res;
  }

  return {
    // state
    items,
    loading,
    pagination,
    // actions
    fetchTrashItems,
    restoreItems,
    deletePermanently,
    emptyTrash,
  };
});
