<template>
  <el-dialog
    :model-value="visible"
    title="保存到我的网盘"
    width="480px"
    :close-on-click-modal="false"
    destroy-on-close
    @update:model-value="$emit('update:visible', $event)"
    @closed="onClosed"
    @open="onOpen"
  >
    <div class="save-share__hint">
      已选择 {{ fileCount }} 个文件、{{ folderCount }} 个文件夹
    </div>

    <div class="save-share__tree-container">
      <el-tree
        ref="treeRef"
        :data="folderTreeData"
        :props="{ label: 'name', children: 'children' }"
        node-key="id"
        highlight-current
        default-expand-all
        :expand-on-click-node="false"
        @node-click="onTreeNodeClick"
      >
        <template #default="{ data }">
          <span class="save-share__tree-node">
            <el-icon><Folder /></el-icon>
            <span>{{ data.name }}</span>
          </span>
        </template>
      </el-tree>

      <div v-if="treeLoading" class="save-share__tree-loading">
        <el-icon class="is-loading"><Loading /></el-icon>
        <span>加载文件夹中...</span>
      </div>
    </div>

    <template #footer>
      <el-button @click="$emit('update:visible', false)">取消</el-button>
      <el-button type="primary" :loading="saving" @click="handleConfirm">
        保存到此处
      </el-button>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref } from 'vue';
import { ElMessage } from 'element-plus';
import { Folder, Loading } from '@element-plus/icons-vue';
import type { ElTree } from 'element-plus';
import type { FolderTreeNode } from '@/types';
import { getFolderTree } from '@/api/folder';

defineProps<{
  visible: boolean;
  fileCount: number;
  folderCount: number;
}>();

const emit = defineEmits<{
  (e: 'update:visible', value: boolean): void;
  (e: 'confirm', targetFolderId: number): void;
}>();

const treeRef = ref<InstanceType<typeof ElTree> | null>(null);
const treeLoading = ref(false);
const saving = ref(false);
const selectedFolderId = ref<number>(0);
const folderTreeData = ref<FolderTreeNode[]>([]);

async function onOpen(): Promise<void> {
  selectedFolderId.value = 0;
  treeLoading.value = true;
  try {
    const tree = await getFolderTree();
    folderTreeData.value = [
      { id: tree.id, name: tree.name || '根目录', children: [...tree.children] },
    ];
  } catch {
    folderTreeData.value = [];
    ElMessage.error('加载文件夹失败');
  } finally {
    treeLoading.value = false;
  }
}

function onTreeNodeClick(data: FolderTreeNode): void {
  selectedFolderId.value = data.id;
}

function onClosed(): void {
  selectedFolderId.value = 0;
  folderTreeData.value = [];
}

async function handleConfirm(): Promise<void> {
  saving.value = true;
  try {
    emit('confirm', selectedFolderId.value);
    emit('update:visible', false);
  } finally {
    saving.value = false;
  }
}
</script>

<style scoped>
.save-share__hint {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  margin-bottom: 12px;
}

.save-share__tree-container {
  max-height: 360px;
  overflow-y: auto;
  border: 1px solid var(--el-border-color-lighter);
  border-radius: var(--el-border-radius-base);
  padding: 8px;
  position: relative;
}

.save-share__tree-node {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 14px;
}

.save-share__tree-loading {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  padding: 20px 12px;
  color: var(--el-text-color-secondary);
  font-size: 13px;
}
</style>
