import { ref, computed } from 'vue';
import { defineStore } from 'pinia';

export const useUiStore = defineStore('ui', () => {
  // ==================== State ====================
  const sidebarCollapsed = ref<boolean>(false);
  const currentViewMode = ref<'myfiles' | 'shared' | 'trash'>('myfiles');
  const globalLoading = ref<boolean>(false);
  const globalError = ref<string | null>(null);
  const contextMenuVisible = ref<boolean>(false);
  const contextMenuPosition = ref<{ x: number; y: number }>({ x: 0, y: 0 });

  // ==================== Getters ====================
  const viewModeLabel = computed(() => {
    const labels: Record<string, string> = {
      myfiles: '我的文件',
      shared: '我的分享',
      trash: '回收站',
    };
    return labels[currentViewMode.value] ?? '';
  });

  // ==================== Actions ====================
  function toggleSidebar(): void {
    sidebarCollapsed.value = !sidebarCollapsed.value;
  }

  function setSidebarCollapsed(collapsed: boolean): void {
    sidebarCollapsed.value = collapsed;
  }

  function setViewMode(mode: 'myfiles' | 'shared' | 'trash'): void {
    currentViewMode.value = mode;
  }

  function setGlobalLoading(loading: boolean): void {
    globalLoading.value = loading;
  }

  function setGlobalError(error: string | null): void {
    globalError.value = error;
  }

  function showContextMenu(x: number, y: number): void {
    contextMenuPosition.value = { x, y };
    contextMenuVisible.value = true;
  }

  function hideContextMenu(): void {
    contextMenuVisible.value = false;
  }

  return {
    // state
    sidebarCollapsed,
    currentViewMode,
    globalLoading,
    globalError,
    contextMenuVisible,
    contextMenuPosition,
    // getters
    viewModeLabel,
    // actions
    toggleSidebar,
    setSidebarCollapsed,
    setViewMode,
    setGlobalLoading,
    setGlobalError,
    showContextMenu,
    hideContextMenu,
  };
});
