<template>
  <Teleport to="body">
    <Transition name="ctx-fade">
      <ul
        v-if="visible"
        ref="menuRef"
        class="drive-ctx-menu"
        :style="menuStyle"
        role="menu"
        @keydown.escape="emit('close')"
      >
        <!-- 打开（文件夹 / 单选） -->
        <li
          v-if="showOpen"
          class="drive-ctx-menu__item"
          role="menuitem"
          tabindex="0"
          @click="handleAction('open')"
        >
          <el-icon :size="16"><FolderOpened /></el-icon>
          <span>打开</span>
        </li>

        <!-- 下载 -->
        <li
          v-if="showDownload"
          class="drive-ctx-menu__item"
          role="menuitem"
          tabindex="0"
          @click="handleAction('download')"
        >
          <el-icon :size="16"><Download /></el-icon>
          <span>下载</span>
        </li>

        <li v-if="showOpen || showDownload" class="drive-ctx-menu__divider" role="separator" />

        <!-- 重命名（仅单选） -->
        <li
          v-if="showRename"
          class="drive-ctx-menu__item"
          role="menuitem"
          tabindex="0"
          @click="handleAction('rename')"
        >
          <el-icon :size="16"><EditPen /></el-icon>
          <span>重命名</span>
        </li>

        <!-- 移动到 -->
        <li
          v-if="showBulkOps"
          class="drive-ctx-menu__item"
          role="menuitem"
          tabindex="0"
          @click="handleAction('move')"
        >
          <el-icon :size="16"><Rank /></el-icon>
          <span>移动到</span>
        </li>

        <!-- 复制到 -->
        <li
          v-if="showBulkOps"
          class="drive-ctx-menu__item"
          role="menuitem"
          tabindex="0"
          @click="handleAction('copy')"
        >
          <el-icon :size="16"><CopyDocument /></el-icon>
          <span>复制到</span>
        </li>

        <li v-if="showRename || showBulkOps" class="drive-ctx-menu__divider" role="separator" />

        <!-- 删除 -->
        <li
          v-if="showBulkOps"
          class="drive-ctx-menu__item drive-ctx-menu__item--danger"
          role="menuitem"
          tabindex="0"
          @click="handleAction('delete')"
        >
          <el-icon :size="16"><Delete /></el-icon>
          <span>删除</span>
        </li>

        <li v-if="showBulkOps" class="drive-ctx-menu__divider" role="separator" />

        <!-- 分享（仅单选） -->
        <li
          v-if="showShare"
          class="drive-ctx-menu__item"
          role="menuitem"
          tabindex="0"
          @click="handleAction('share')"
        >
          <el-icon :size="16"><Share /></el-icon>
          <span>分享</span>
        </li>

        <!-- 属性（仅单选） -->
        <li
          v-if="showProperties"
          class="drive-ctx-menu__item"
          role="menuitem"
          tabindex="0"
          @click="handleAction('properties')"
        >
          <el-icon :size="16"><InfoFilled /></el-icon>
          <span>属性</span>
        </li>
      </ul>
    </Transition>
  </Teleport>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import {
  FolderOpened,
  Download,
  EditPen,
  Rank,
  CopyDocument,
  Delete,
  Share,
  InfoFilled,
} from '@element-plus/icons-vue'
import type { FileItem } from '@/types'

// ==================== Props & Emits ====================

const props = withDefaults(
  defineProps<{
    /** 菜单是否可见 */
    visible: boolean
    /** 右键坐标 */
    position: { x: number; y: number }
    /** 右键目标项，null 表示空白区域右键 */
    item: FileItem | null
    /** 当前选中数量 */
    selectedCount: number
  }>(),
  {
    visible: false,
    position: () => ({ x: 0, y: 0 }),
    item: null,
    selectedCount: 0,
  },
)

const emit = defineEmits<{
  close: []
  open: [item: FileItem]
  download: [item: FileItem]
  rename: [item: FileItem]
  move: [item: FileItem]
  copy: [item: FileItem]
  delete: [item: FileItem]
  share: [item: FileItem]
  properties: [item: FileItem]
}>()

// ==================== Internal State ====================

const menuRef = ref<HTMLUListElement | null>(null)

// ==================== Computed ====================

/** 有效的选中数量：右键某个项时至少为 1 */
const effectiveCount = computed(() => {
  if (props.item) return Math.max(props.selectedCount, 1)
  return props.selectedCount
})

/** 是否为单选（且有一个明确的目标项） */
const isSingle = computed(() => props.item !== null && effectiveCount.value === 1)

/** 是否为多选 */
const isMultiple = computed(() => effectiveCount.value > 1)

/** 显示"打开"：文件夹（单选或多选中第一个是文件夹均可打开） */
const showOpen = computed(() => props.item !== null && props.item.type === 'folder')

/** 显示"下载"：单选文件，或多选时 */
const showDownload = computed(() => {
  if (isSingle.value && props.item!.type === 'file') return true
  return isMultiple.value
})

/** 显示"重命名"：仅单选 */
const showRename = computed(() => isSingle.value)

/** 显示批量操作（移动/复制/删除）：>= 1 选中 */
const showBulkOps = computed(() => effectiveCount.value >= 1)

/** 显示"分享"：仅单选 */
const showShare = computed(() => isSingle.value)

/** 显示"属性"：仅单选 */
const showProperties = computed(() => isSingle.value)

/** 菜单定位样式 */
const menuStyle = computed<Record<string, string>>(() => {
  const { x, y } = props.position
  return {
    left: `${x}px`,
    top: `${y}px`,
  }
})

// ==================== Methods ====================

function handleAction(action: string) {
  if (!props.item) return
  emit(action as keyof typeof emit, props.item)
  emit('close')
}

/** 点击外部关闭 */
function onDocumentClick(e: MouseEvent) {
  if (!props.visible) return
  if (menuRef.value && menuRef.value.contains(e.target as Node)) return
  emit('close')
}

/** ESC 关闭 */
function onDocumentKeydown(e: KeyboardEvent) {
  if (e.key === 'Escape' && props.visible) {
    emit('close')
  }
}

// ==================== Lifecycle ====================

onMounted(() => {
  document.addEventListener('click', onDocumentClick, true)
  document.addEventListener('keydown', onDocumentKeydown, true)
})

onUnmounted(() => {
  document.removeEventListener('click', onDocumentClick, true)
  document.removeEventListener('keydown', onDocumentKeydown, true)
})

/** 菜单出现后校正溢出边界 */
watch(
  () => props.visible,
  async (visible) => {
    if (!visible || !menuRef.value) return
    await nextTick()
    const el = menuRef.value
    const rect = el.getBoundingClientRect()
    const vw = window.innerWidth
    const vh = window.innerHeight

    if (rect.right > vw) {
      el.style.left = `${Math.max(0, props.position.x - rect.width)}px`
    }
    if (rect.bottom > vh) {
      el.style.top = `${Math.max(0, props.position.y - rect.height)}px`
    }
  },
)
</script>

<style scoped>
.drive-ctx-menu {
  position: fixed;
  z-index: 3000;
  min-width: 180px;
  max-width: 260px;
  margin: 0;
  padding: 4px 0;
  list-style: none;
  background: var(--el-bg-color-overlay);
  border: 1px solid var(--el-border-color-light);
  border-radius: var(--el-border-radius-base);
  box-shadow: var(--el-box-shadow-light);
  font-size: var(--el-font-size-base);
  color: var(--el-text-color-primary);
  outline: none;
}

.drive-ctx-menu__item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  cursor: pointer;
  transition: background-color 0.15s;
  user-select: none;
}

.drive-ctx-menu__item:hover,
.drive-ctx-menu__item:focus {
  background-color: var(--el-fill-color-light);
}

.drive-ctx-menu__item--danger {
  color: var(--el-color-danger);
}

.drive-ctx-menu__item--danger:hover,
.drive-ctx-menu__item--danger:focus {
  background-color: var(--el-color-danger-light-9);
  color: var(--el-color-danger);
}

.drive-ctx-menu__divider {
  margin: 4px 8px;
  border-top: 1px solid var(--el-border-color-lighter);
}

/* Transition */
.ctx-fade-enter-active {
  transition: opacity 0.12s ease, transform 0.12s ease;
}
.ctx-fade-leave-active {
  transition: opacity 0.08s ease;
}
.ctx-fade-enter-from {
  opacity: 0;
  transform: scale(0.95);
}
.ctx-fade-leave-to {
  opacity: 0;
}
</style>
