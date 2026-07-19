<template>
  <div class="drive-toolbar">
    <div class="toolbar-left">
      <!-- Upload -->
      <el-button :icon="Upload" type="primary" @click="handleUpload">
        上传
      </el-button>

      <!-- New Folder -->
      <el-button :icon="FolderAdd" @click="showNewFolderDialog = true">
        新建文件夹
      </el-button>

      <el-divider direction="vertical" />

      <!-- Download -->
      <el-button
        :icon="Download"
        :disabled="!canDownload"
        @click="handleDownload"
      >
        下载
      </el-button>

      <!-- Rename -->
      <el-button
        :icon="Edit"
        :disabled="!canRename"
        @click="handleRename"
      >
        重命名
      </el-button>

      <!-- Move -->
      <el-button
        :icon="Rank"
        :disabled="!canBatch"
        @click="handleMove"
      >
        移动
      </el-button>

      <!-- Copy -->
      <el-button
        :icon="CopyDocument"
        :disabled="!canBatch"
        @click="handleCopy"
      >
        复制
      </el-button>

      <!-- Delete -->
      <el-button
        :icon="Delete"
        :disabled="!canBatch"
        type="danger"
        plain
        @click="handleDelete"
      >
        删除
      </el-button>
    </div>

    <div class="toolbar-right">
      <span v-if="driveStore.selectedCount > 0" class="selection-info">
        已选择 {{ driveStore.selectedCount }} 项
      </span>
    </div>

    <!-- ==================== New Folder Dialog ==================== -->
    <el-dialog
      v-model="showNewFolderDialog"
      title="新建文件夹"
      width="420px"
      :close-on-click-modal="false"
      destroy-on-close
      @closed="newFolderName = ''"
    >
      <el-form @submit.prevent="confirmNewFolder">
        <el-form-item label="文件夹名称">
          <el-input
            ref="newFolderInputRef"
            v-model="newFolderName"
            placeholder="请输入文件夹名称"
            maxlength="255"
            clearable
            @keyup.enter="confirmNewFolder"
          />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showNewFolderDialog = false">取消</el-button>
        <el-button
          type="primary"
          :loading="operationLoading"
          :disabled="!newFolderName.trim()"
          @click="confirmNewFolder"
        >
          创建
        </el-button>
      </template>
    </el-dialog>

    <!-- ==================== Rename Dialog ==================== -->
    <el-dialog
      v-model="showRenameDialog"
      title="重命名"
      width="420px"
      :close-on-click-modal="false"
      destroy-on-close
      @closed="renameName = ''"
    >
      <el-form @submit.prevent="confirmRename">
        <el-form-item label="新名称">
          <el-input
            ref="renameInputRef"
            v-model="renameName"
            placeholder="请输入新名称"
            maxlength="255"
            clearable
            @keyup.enter="confirmRename"
          />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showRenameDialog = false">取消</el-button>
        <el-button
          type="primary"
          :loading="operationLoading"
          :disabled="!renameName.trim()"
          @click="confirmRename"
        >
          确认
        </el-button>
      </template>
    </el-dialog>

    <!-- ==================== Move/Copy Dialog ==================== -->
    <el-dialog
      v-model="showMoveCopyDialog"
      :title="moveCopyMode === 'move' ? '移动到' : '复制到'"
      width="480px"
      :close-on-click-modal="false"
      destroy-on-close
      @closed="onMoveCopyDialogClosed"
    >
      <div class="folder-tree-container">
        <el-tree
          ref="folderTreeRef"
          :data="folderTreeData"
          :props="{ label: 'name', children: 'children' }"
          node-key="id"
          highlight-current
          default-expand-all
          :expand-on-click-node="false"
          @node-click="onTreeNodeClick"
        >
          <template #default="{ data }">
            <span class="tree-node-label">
              <el-icon><Folder /></el-icon>
              <span>{{ data.name }}</span>
            </span>
          </template>
        </el-tree>
      </div>
      <template #footer>
        <el-button @click="showMoveCopyDialog = false">取消</el-button>
        <el-button
          type="primary"
          :loading="operationLoading"
          :disabled="targetFolderId === null"
          @click="confirmMoveCopy"
        >
          确认
        </el-button>
      </template>
    </el-dialog>

    <!-- ==================== Upload Dialog ==================== -->
    <UploadDialog
      v-model:visible="showUploadDialog"
      @uploaded="onUploadCompleted"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, nextTick } from 'vue'
import {
  Upload,
  FolderAdd,
  Download,
  Edit,
  Rank,
  CopyDocument,
  Delete,
  Folder,
} from '@element-plus/icons-vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import type { ElInput, ElTree } from 'element-plus'
import type { FolderTreeNode } from '@/types'

import { useDriveStore } from '@/stores'
import { useTransferStore } from '@/stores'
import {
  renameFile,
  moveFiles,
  copyFiles,
  deleteFiles,
  createFolder,
  renameFolder,
} from '@/api'
import type {
  FileItem,
} from '@/types'
import UploadDialog from '@/components/transfer/UploadDialog.vue'

// ==================== Store ====================

const driveStore = useDriveStore()
const transferStore = useTransferStore()

// ==================== Selection State ====================

const selectedItems = computed<readonly FileItem[]>(() => {
  const ids = driveStore.selectedIds
  return driveStore.files.filter((f: FileItem) => ids.has(f.id))
})

/** Exactly 1 selected, and it's a file (not folder) */
const canDownload = computed(() => {
  const items = selectedItems.value
  return items.length === 1 && items[0].type === 'file'
})

/** Exactly 1 selected (file or folder) */
const canRename = computed(() => selectedItems.value.length === 1)

/** At least 1 selected */
const canBatch = computed(() => selectedItems.value.length >= 1)

// ==================== Shared Loading ====================

const operationLoading = ref(false)

// ==================== Upload ====================

const showUploadDialog = ref(false)

function handleUpload(): void {
  showUploadDialog.value = true
}

async function onUploadCompleted(): Promise<void> {
  await driveStore.refreshCurrentView()
}

// ==================== New Folder ====================

const showNewFolderDialog = ref(false)
const newFolderName = ref('')
const newFolderInputRef = ref<InstanceType<typeof ElInput> | null>(null)

async function confirmNewFolder(): Promise<void> {
  const name = newFolderName.value.trim()
  if (!name) return

  operationLoading.value = true
  try {
    const folder = await createFolder({
      name,
      parent_id: driveStore.currentFolderId || undefined,
    })
    driveStore.applyCreatedFolder(folder)
    ElMessage.success(`文件夹「${name}」创建成功`)
    showNewFolderDialog.value = false
    await driveStore.refreshNavigationMetadata()
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : '创建文件夹失败'
    ElMessage.error(msg)
  } finally {
    operationLoading.value = false
  }
}

// ==================== Download ====================

function handleDownload(): void {
  const item = selectedItems.value[0]
  if (!item || item.type !== 'file') return

  transferStore.addDownloadTask(item.id, item.name, item.size ?? 0)
}

// ==================== Rename ====================

const showRenameDialog = ref(false)
const renameName = ref('')
const renameInputRef = ref<InstanceType<typeof ElInput> | null>(null)

function handleRename(): void {
  const item = selectedItems.value[0]
  if (!item) return
  renameName.value = item.name
  showRenameDialog.value = true
  nextTick(() => {
    renameInputRef.value?.focus()
  })
}

async function confirmRename(): Promise<void> {
  const newName = renameName.value.trim()
  const item = selectedItems.value[0]
  if (!item || !newName) return

  if (newName === item.name) {
    showRenameDialog.value = false
    return
  }

  operationLoading.value = true
  try {
    const result = item.type === 'folder'
      ? await renameFolder(item.id, { new_name: newName })
      : await renameFile(item.id, { new_name: newName })
    driveStore.applyItemRename(result)
    ElMessage.success('重命名成功')
    showRenameDialog.value = false
    await driveStore.refreshNavigationMetadata()
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : '重命名失败'
    ElMessage.error(msg)
  } finally {
    operationLoading.value = false
  }
}

// ==================== Move / Copy ====================

const showMoveCopyDialog = ref(false)
const moveCopyMode = ref<'move' | 'copy'>('move')
const targetFolderId = ref<number | null>(null)
const folderTreeData = ref<FolderTreeNode[]>([])
const folderTreeRef = ref<InstanceType<typeof ElTree> | null>(null)

function splitSelectedIds(): {
  fileIds: number[]
  folderIds: number[]
} {
  const fileIds: number[] = []
  const folderIds: number[] = []
  for (const item of selectedItems.value) {
    if (item.type === 'file') {
      fileIds.push(item.id)
    } else {
      folderIds.push(item.id)
    }
  }
  return { fileIds, folderIds }
}

async function loadFolderTree(): Promise<void> {
  try {
    const tree = driveStore.folderTree ?? await driveStore.fetchFolderTree()
    folderTreeData.value = [
      {
        id: tree.id,
        name: tree.name || '根目录',
        children: [...tree.children],
      },
    ]
  } catch {
    folderTreeData.value = []
  }
}

async function handleMove(): Promise<void> {
  moveCopyMode.value = 'move'
  targetFolderId.value = null
  showMoveCopyDialog.value = true
  await loadFolderTree()
}

async function handleCopy(): Promise<void> {
  moveCopyMode.value = 'copy'
  targetFolderId.value = null
  showMoveCopyDialog.value = true
  await loadFolderTree()
}

function onTreeNodeClick(data: FolderTreeNode): void {
  targetFolderId.value = data.id
}

function onMoveCopyDialogClosed(): void {
  targetFolderId.value = null
  folderTreeData.value = []
}

async function confirmMoveCopy(): Promise<void> {
  const tid = targetFolderId.value
  if (tid === null) return

  const { fileIds, folderIds } = splitSelectedIds()
  const movedIds = [...fileIds, ...folderIds]

  operationLoading.value = true
  try {
    if (moveCopyMode.value === 'move') {
      const result = await moveFiles({
        target_folder_id: tid,
        file_ids: fileIds.length > 0 ? fileIds : undefined,
        folder_ids: folderIds.length > 0 ? folderIds : undefined,
      })
      driveStore.applyItemsRemoved(movedIds)
      ElMessage.success(`已移动 ${result.moved_count} 项`)
    } else {
      const result = await copyFiles({
        target_folder_id: tid,
        file_ids: fileIds.length > 0 ? fileIds : undefined,
        folder_ids: folderIds.length > 0 ? folderIds : undefined,
      })
      ElMessage.success(`已复制 ${result.copied_count} 项`)
    }
    showMoveCopyDialog.value = false
    if (moveCopyMode.value === 'move') {
      await driveStore.refreshAfterFolderMove(folderIds, tid)
    } else {
      await driveStore.refreshHierarchyView()
    }
  } catch (err: unknown) {
    const msg = err instanceof Error
      ? err.message
      : moveCopyMode.value === 'move' ? '移动失败' : '复制失败'
    ElMessage.error(msg)
  } finally {
    operationLoading.value = false
  }
}

// ==================== Delete ====================

async function handleDelete(): Promise<void> {
  const items = selectedItems.value
  if (items.length === 0) return

  const names = items.map((i) => i.name)
  const displayNames = names.length <= 3
    ? names.join('、')
    : `${names.slice(0, 3).join('、')} 等 ${names.length} 项`

  try {
    await ElMessageBox.confirm(
      `确定要删除「${displayNames}」吗？删除后可在回收站中恢复。`,
      '确认删除',
      {
        confirmButtonText: '删除',
        cancelButtonText: '取消',
        type: 'warning',
        confirmButtonClass: 'el-button--danger',
      },
    )
  } catch {
    // User cancelled
    return
  }

  const { fileIds, folderIds } = splitSelectedIds()
  const deletedIds = [...fileIds, ...folderIds]

  operationLoading.value = true
  try {
    const result = await deleteFiles({
      file_ids: fileIds.length > 0 ? fileIds : undefined,
      folder_ids: folderIds.length > 0 ? folderIds : undefined,
    })
    driveStore.applyItemsRemoved(deletedIds)
    ElMessage.success(`已删除 ${result.deleted_count} 项`)
    await driveStore.refreshNavigationMetadata()
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : '删除失败'
    ElMessage.error(msg)
  } finally {
    operationLoading.value = false
  }
}
</script>

<style scoped>
.drive-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  background: var(--el-bg-color);
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.toolbar-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.toolbar-right {
  display: flex;
  align-items: center;
}

.selection-info {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.folder-tree-container {
  max-height: 360px;
  overflow-y: auto;
  border: 1px solid var(--el-border-color-lighter);
  border-radius: var(--el-border-radius-base);
  padding: 8px;
}

.tree-node-label {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 14px;
}
</style>
