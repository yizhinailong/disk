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
  CreateFolderResponse,
  RenameResponse,
} from '@/types';

export const useDriveStore = defineStore('drive', () => {
  // ==================== State ====================
  const files = ref<FileItem[]>([]);
  const currentFolderId = ref<number>(0);
  const breadcrumbs = ref<BreadcrumbItem[]>([]);
  const folderTree = ref<FolderTreeNode | null>(null);
  const folderTreeLoading = ref<boolean>(false);
  const folderTreeError = ref<string | null>(null);
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

  async function fetchFolderTree(params?: Parameters<typeof getFolderTree>[0]): Promise<FolderTreeNode> {
    folderTreeLoading.value = true;
    folderTreeError.value = null;
    try {
      const result = await getFolderTree(params);
      const tree = { id: result.id, name: result.name, children: [...result.children] };
      if (!params?.parent_id) {
        folderTree.value = tree;
      }
      return tree;
    } catch (err) {
      folderTreeError.value = err instanceof Error ? err.message : '加载文件夹树失败';
      throw err;
    } finally {
      folderTreeLoading.value = false;
    }
  }

  function invalidateFolderTree(): void {
    folderTree.value = null;
    folderTreeError.value = null;
  }

  async function refreshFolderTree(): Promise<void> {
    await fetchFolderTree();
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

  function setSelection(ids: readonly number[]): void {
    selectedIds.value = new Set(ids);
  }

  function updatePaginationTotal(delta: number): void {
    if (!pagination.value) return;
    const total = Math.max(0, pagination.value.total + delta);
    pagination.value = {
      ...pagination.value,
      total,
      total_pages: pagination.value.page_size > 0
        ? Math.ceil(total / pagination.value.page_size)
        : 0,
    };
  }

  function applyCreatedFolder(folder: CreateFolderResponse): void {
    const index = files.value.findIndex((item) => item.id === folder.id);
    const item: FileItem = {
      id: folder.id,
      name: folder.name,
      type: 'folder',
      item_count: 0,
      created_at: folder.created_at,
      updated_at: folder.created_at,
    };
    if (index >= 0) {
      files.value[index] = item;
      return;
    }
    files.value = [...files.value, item];
    updatePaginationTotal(1);
  }

  function applyItemRename(result: RenameResponse): void {
    files.value = files.value.map((item) => item.id === result.id
      ? { ...item, name: result.name, updated_at: result.updated_at }
      : item);
  }

  function applyItemsRemoved(ids: readonly number[]): void {
    const removedIds = new Set(ids);
    const removedCount = files.value.filter((item) => removedIds.has(item.id)).length;
    files.value = files.value.filter((item) => !removedIds.has(item.id));
    selectedIds.value = new Set([...selectedIds.value].filter((id) => !removedIds.has(id)));
    updatePaginationTotal(-removedCount);
  }

  function relocateFolderNodes(
    folderIds: readonly number[],
    targetFolderId: number,
    fallbackNodes: readonly FolderTreeNode[] = [],
  ): FolderTreeNode[] {
    if (!folderTree.value || folderIds.length === 0) return [...fallbackNodes];

    const movedIds = new Set(folderIds);
    const detachedNodes: FolderTreeNode[] = [];
    const detach = (nodes: readonly FolderTreeNode[]): FolderTreeNode[] => nodes.flatMap((node) => {
      if (movedIds.has(node.id)) {
        detachedNodes.push(node);
        return [];
      }
      return [{ ...node, children: detach(node.children) }];
    });

    const detachedRoot: FolderTreeNode = {
      ...folderTree.value,
      children: detach(folderTree.value.children),
    };
    const nodesToInsert = detachedNodes.length > 0 ? detachedNodes : [...fallbackNodes];
    const insert = (node: FolderTreeNode): FolderTreeNode => {
      const children = node.children.map(insert);
      return node.id === targetFolderId
        ? { ...node, children: [...children, ...nodesToInsert] }
        : { ...node, children };
    };

    folderTree.value = insert(detachedRoot);
    return nodesToInsert;
  }

  async function refreshCurrentView(): Promise<void> {
    await Promise.all([
      fetchFiles(pagination.value?.page ?? 1),
      fetchBreadcrumb(),
      refreshFolderTree().catch(() => undefined),
    ]);
  }

  async function refreshNavigationMetadata(): Promise<void> {
    await Promise.all([
      fetchBreadcrumb(),
      refreshFolderTree().catch(() => undefined),
    ]);
  }

  async function refreshAfterFolderMove(
    folderIds: readonly number[],
    targetFolderId: number,
  ): Promise<void> {
    const movedNodes = relocateFolderNodes(folderIds, targetFolderId);
    await refreshNavigationMetadata();
    relocateFolderNodes(folderIds, targetFolderId, movedNodes);
  }

  async function refreshHierarchyView(): Promise<void> {
    await refreshCurrentView();
  }

  return {
    // state
    files,
    currentFolderId,
    breadcrumbs,
    folderTree,
    folderTreeLoading,
    folderTreeError,
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
    refreshFolderTree,
    invalidateFolderTree,
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
    setSelection,
    applyCreatedFolder,
    applyItemRename,
    applyItemsRemoved,
    refreshCurrentView,
    refreshNavigationMetadata,
    refreshAfterFolderMove,
    refreshHierarchyView,
  };
});
