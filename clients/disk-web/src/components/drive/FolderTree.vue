<template>
  <div class="folder-tree-panel">
    <!-- Panel header with collapse toggle -->
    <div class="tree-header" @click="collapsed = !collapsed">
      <span class="tree-header-title">文件夹</span>
      <el-icon class="tree-header-arrow" :class="{ 'is-collapsed': collapsed }">
        <ArrowRight />
      </el-icon>
    </div>

    <!-- Tree content -->
    <el-collapse-transition>
      <div v-show="!collapsed" class="tree-body">
        <el-tree
          ref="treeRef"
          :data="treeData"
          :props="treeProps"
          node-key="id"
          highlight-current
          :default-expanded-keys="defaultExpanded"
          :current-node-key="driveStore.currentFolderId"
          :expand-on-click-node="false"
          class="folder-tree"
          @node-click="handleNodeClick"
        >
          <template #default="{ data }">
            <div
              class="tree-node"
              :title="data.name"
              :aria-current="data.id === driveStore.currentFolderId ? 'page' : undefined"
            >
              <el-icon class="tree-node-icon">
                <Folder />
              </el-icon>
              <span class="tree-node-label">{{ data.name }}</span>
            </div>
          </template>
        </el-tree>

        <div v-if="driveStore.folderTreeError" class="tree-error" role="alert">
          <span>{{ driveStore.folderTreeError }}</span>
          <el-button text size="small" @click="retryFolderTree">重试</el-button>
        </div>

        <!-- Empty state -->
        <div
          v-if="!driveStore.folderTreeLoading && !driveStore.folderTreeError && treeData.length === 0"
          class="tree-empty"
        >
          <span>暂无文件夹</span>
        </div>

        <!-- Loading state -->
        <div v-if="driveStore.folderTreeLoading" class="tree-loading">
          <el-icon class="is-loading"><Loading /></el-icon>
          <span>加载中...</span>
        </div>
      </div>
    </el-collapse-transition>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, nextTick } from 'vue'
import type { ElTree } from 'element-plus'
import { Folder, ArrowRight, Loading } from '@element-plus/icons-vue'
import { useRouter } from 'vue-router'
import { useDriveStore } from '@/stores/drive'
import type { FolderTreeNode } from '@/types'

const driveStore = useDriveStore()
const router = useRouter()

// ==================== State ====================
const treeRef = ref<InstanceType<typeof ElTree> | null>(null)
const collapsed = ref(false)
const defaultExpanded = ref<number[]>([])

// ==================== Tree Configuration ====================
const treeProps = {
  label: 'name',
  children: 'children',
}

const treeData = computed<FolderTreeNode[]>(() => {
  return driveStore.folderTree ? [...driveStore.folderTree.children] : []
})

function findAncestorPath(
  nodes: readonly FolderTreeNode[],
  targetId: number,
  path: number[] = [],
): number[] | null {
  for (const node of nodes) {
    const nextPath = [...path, node.id]
    if (node.id === targetId) return path

    const childPath = findAncestorPath(node.children, targetId, nextPath)
    if (childPath) return childPath
  }
  return null
}

function syncCurrentFolder(folderId: number): void {
  const rootIds = treeData.value.map((node) => node.id)
  const ancestors = folderId === 0 ? [] : findAncestorPath(treeData.value, folderId) ?? []
  const current = folderId === 0 ? [] : [folderId]
  defaultExpanded.value = [...new Set([...rootIds, ...ancestors, ...current])]

  nextTick(() => {
    for (const id of defaultExpanded.value) {
      const node = treeRef.value?.getNode(id)
      if (node) node.expanded = true
    }
    if (folderId === 0) {
      treeRef.value?.setCurrentKey(null)
    } else {
      treeRef.value?.setCurrentKey(folderId)
    }
  })
}

// ==================== Node Click ====================
function handleNodeClick(data: FolderTreeNode) {
  if (data.id !== driveStore.currentFolderId) {
    router.push({ path: '/drive', query: { folderId: String(data.id) } })
  }
}

async function retryFolderTree(): Promise<void> {
  await driveStore.refreshFolderTree().catch(() => undefined)
}

// ==================== Sync Current Folder Highlight ====================
watch(
  () => [driveStore.currentFolderId, driveStore.folderTree] as const,
  ([folderId]) => {
    syncCurrentFolder(folderId)
  },
)

// ==================== Initialize ====================
onMounted(async () => {
  if (!driveStore.folderTree) {
    await driveStore.refreshFolderTree().catch(() => undefined)
  }
  syncCurrentFolder(driveStore.currentFolderId)
})
</script>

<style scoped>
.folder-tree-panel {
  background: #fff;
  border-radius: 8px;
  border: 1px solid #e8e8e8;
  overflow: hidden;
}

.tree-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 12px;
  cursor: pointer;
  user-select: none;
  transition: background 0.15s;
}

.tree-header:hover {
  background: #f5f7fa;
}

.tree-header-title {
  font-size: 13px;
  font-weight: 600;
  color: #303133;
}

.tree-header-arrow {
  font-size: 12px;
  color: #909399;
  transition: transform 0.2s ease;
}

.tree-header-arrow.is-collapsed {
  transform: rotate(0deg);
}

.tree-header-arrow:not(.is-collapsed) {
  transform: rotate(90deg);
}

.tree-body {
  min-height: 40px;
}

/* ==================== Tree Overrides ==================== */
.folder-tree {
  background: transparent;
  --el-tree-node-content-height: 32px;
}

.folder-tree :deep(.el-tree-node__content) {
  padding-left: 8px !important;
  border-radius: 4px;
  margin: 1px 4px;
  transition: background 0.15s;
}

.folder-tree :deep(.el-tree-node__content:hover) {
  background: #f0f5ff;
}

.folder-tree :deep(.el-tree-node.is-current > .el-tree-node__content) {
  background: #ecf5ff;
  color: #409eff;
}

.folder-tree :deep(.el-tree-node__expand-icon) {
  font-size: 14px;
  color: #c0c4cc;
}

/* ==================== Tree Node ==================== */
.tree-node {
  display: flex;
  align-items: center;
  gap: 6px;
  overflow: hidden;
  flex: 1;
  min-width: 0;
}

.tree-node-icon {
  color: #e6a23c;
  font-size: 16px;
  flex-shrink: 0;
}

.tree-node-label {
  font-size: 13px;
  color: #303133;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

/* ==================== States ==================== */
.tree-empty {
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 20px 12px;
  color: #c0c4cc;
  font-size: 13px;
}

.tree-error {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin: 6px 8px 8px;
  padding: 8px 10px;
  border-left: 3px solid var(--el-color-danger);
  background: var(--el-color-danger-light-9);
  color: var(--el-color-danger-dark-2);
  font-size: 12px;
}

.tree-error span {
  min-width: 0;
  overflow-wrap: anywhere;
}

.tree-loading {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  padding: 20px 12px;
  color: #909399;
  font-size: 13px;
}
</style>
