import { ref, computed } from 'vue';
import { defineStore } from 'pinia';
import { listFiles, searchFiles as searchFilesApi } from '@/api/file';
import { getBreadcrumb, getFolderTree } from '@/api/folder';
import type {
  FileItem,
  BreadcrumbItem,
  FolderTreeNode,
  SearchResultItem,
  Pagination,
  SearchQuery,
} from '@/types';

export const useDriveStore = defineStore('drive', () => {
  // ==================== State ====================
  const files = ref<FileItem[]>([]);
  const currentFolderId = ref<number>(0);
  const breadcrumbs = ref<BreadcrumbItem[]>([]);
  const folderTree = ref<FolderTreeNode | null>(null);
  const loading = ref<boolean>(false);
  const searchQuery = ref<string>('');
  const searchResults = ref<SearchResultItem[]>([]);
  const sortBy = ref<string>('updated_at');
  const sortOrder = ref<'asc' | 'desc'>('desc');
  const filterType = ref<string>('all');
  const pagination = ref<Pagination | null>(null);
  const selectedIds = ref<Set<number>>(new Set());

  // ==================== Getters ====================
  const isRoot = computed(() => currentFolderId.value === 0);

  const hasSelection = computed(() => selectedIds.value.size > 0);

  const selectedCount = computed(() => selectedIds.value.size);

  const isSearching = computed(() => searchQuery.value.length > 0);

  const sortedFiles = computed(() => {
    const list = [...files.value];
    const field = sortBy.value as keyof FileItem;
    const order = sortOrder.value === 'asc' ? 1 : -1;

    return list.sort((a, b) => {
      const aVal = a[field];
      const bVal = b[field];
      if (aVal == null || bVal == null) return 0;
      if (aVal < bVal) return -1 * order;
      if (aVal > bVal) return 1 * order;
      return 0;
    });
  });

  // ==================== Actions ====================
  async function fetchFiles(page?: number): Promise<void> {
    loading.value = true;
    try {
      const query: Record<string, string | number | undefined> = {
        parent_id: currentFolderId.value || undefined,
        sort_by: sortBy.value,
        sort_order: sortOrder.value,
        page: page ?? pagination.value?.page ?? 1,
        page_size: pagination.value?.page_size ?? 20,
      };
      if (filterType.value && filterType.value !== 'all') {
        query.type = filterType.value;
      }
      const result = await listFiles(query as Parameters<typeof listFiles>[0]);
      files.value = [...result.items];
      pagination.value = { ...result.pagination };
      clearSelection();
    } finally {
      loading.value = false;
    }
  }

  async function fetchFolderTree(): Promise<void> {
    const result = await getFolderTree();
    folderTree.value = { id: result.id, name: result.name, children: [...result.children] };
  }

  async function fetchBreadcrumb(): Promise<void> {
    if (currentFolderId.value === 0) {
      breadcrumbs.value = [];
      return;
    }
    const result = await getBreadcrumb(currentFolderId.value);
    breadcrumbs.value = [...result.path];
  }

  async function navigateToFolder(folderId: number): Promise<void> {
    currentFolderId.value = folderId;
    await Promise.all([fetchFiles(1), fetchBreadcrumb()]);
  }

  async function searchFiles(keyword: string, type?: string, folderId?: number): Promise<void> {
    if (!keyword.trim()) {
      clearSearch();
      return;
    }
    searchQuery.value = keyword;
    loading.value = true;
    try {
      const params: SearchQuery = { keyword };
      if (type && type !== 'all') params.type = type;
      if (folderId) params.folder_id = folderId;
      const result = await searchFilesApi(params);
      searchResults.value = [...result.items];
    } finally {
      loading.value = false;
    }
  }

  function clearSearch(): void {
    searchQuery.value = '';
    searchResults.value = [];
  }

  function setSortBy(field: string): void {
    sortBy.value = field;
  }

  function setSortOrder(order: 'asc' | 'desc'): void {
    sortOrder.value = order;
  }

  function setFilterType(type: string): void {
    filterType.value = type;
  }

  function toggleSelect(id: number): void {
    if (selectedIds.value.has(id)) {
      selectedIds.value.delete(id);
    } else {
      selectedIds.value.add(id);
    }
  }

  function selectAll(): void {
    selectedIds.value = new Set(files.value.map((f) => f.id));
  }

  function clearSelection(): void {
    selectedIds.value = new Set();
  }

  async function refreshCurrentView(): Promise<void> {
    await fetchFiles(pagination.value?.page ?? 1);
  }

  return {
    // state
    files,
    currentFolderId,
    breadcrumbs,
    folderTree,
    loading,
    searchQuery,
    searchResults,
    sortBy,
    sortOrder,
    filterType,
    pagination,
    selectedIds,
    // getters
    isRoot,
    hasSelection,
    selectedCount,
    isSearching,
    sortedFiles,
    // actions
    fetchFiles,
    fetchFolderTree,
    fetchBreadcrumb,
    navigateToFolder,
    searchFiles,
    clearSearch,
    setSortBy,
    setSortOrder,
    setFilterType,
    toggleSelect,
    selectAll,
    clearSelection,
    refreshCurrentView,
  };
});
