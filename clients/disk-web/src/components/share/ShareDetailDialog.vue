<template>
  <el-dialog
    v-model="dialogVisible"
    title="分享详情"
    width="600px"
    :close-on-click-modal="false"
    destroy-on-close
    @open="onOpen"
    @closed="onClosed"
  >
    <div v-if="loading" class="detail-loading">
      <el-icon class="is-loading" :size="24"><Loading /></el-icon>
      <span>加载中...</span>
    </div>

    <template v-else-if="detail">
      <el-descriptions :column="2" border>
        <el-descriptions-item label="分享链接" :span="2">
          <div class="link-cell">
            <el-input :model-value="detail.share_link" readonly size="small" />
            <el-button type="primary" link size="small" @click="copyText(detail.share_link)">
              复制
            </el-button>
          </div>
        </el-descriptions-item>

        <el-descriptions-item label="状态">
          <el-tag :type="statusTagType" size="small">{{ statusLabel }}</el-tag>
        </el-descriptions-item>

        <el-descriptions-item label="权限">
          <el-tag size="small" type="info">
            {{ detail.permission === 'download' ? '可下载' : '仅查看' }}
          </el-tag>
        </el-descriptions-item>

        <el-descriptions-item label="密码保护">
          {{ detail.has_password ? '是' : '否' }}
        </el-descriptions-item>

        <el-descriptions-item label="浏览次数">
          {{ detail.view_count }}
        </el-descriptions-item>

        <el-descriptions-item label="创建时间">
          {{ formatDate(detail.created_at) }}
        </el-descriptions-item>

        <el-descriptions-item label="过期时间">
          {{ detail.expires_at ? formatDate(detail.expires_at) : '永久有效' }}
        </el-descriptions-item>

        <el-descriptions-item label="下载次数">
          {{ detail.download_count }}
        </el-descriptions-item>
      </el-descriptions>

      <div class="section-title">分享文件</div>
      <el-table :data="[...detail.files]" size="small" max-height="240">
        <el-table-column prop="name" label="文件名" show-overflow-tooltip />
        <el-table-column prop="type" label="类型" width="80" align="center">
          <template #default="{ row }">
            {{ row.type === 'folder' ? '文件夹' : '文件' }}
          </template>
        </el-table-column>
        <el-table-column label="大小" width="100" align="right">
          <template #default="{ row }">
            {{ row.type === 'folder' ? `${row.item_count ?? 0} 项` : formatSize(row.size) }}
          </template>
        </el-table-column>
      </el-table>

      <div v-if="detail.status === 'active' && !showEditForm" class="action-bar">
        <el-button size="small" @click="showEditForm = true">修改配置</el-button>
        <el-button size="small" type="danger" plain @click="handleCancel">取消分享</el-button>
      </div>

      <el-form
        v-if="showEditForm"
        ref="editFormRef"
        :model="editForm"
        label-width="80px"
        label-position="left"
        class="edit-form"
      >
        <el-divider content-position="left">修改分享配置</el-divider>

        <el-form-item label="有效期">
          <el-radio-group v-model="editForm.expireDays">
            <el-radio :value="1">1 天</el-radio>
            <el-radio :value="7">7 天</el-radio>
            <el-radio :value="30">30 天</el-radio>
            <el-radio :value="0">永久</el-radio>
          </el-radio-group>
        </el-form-item>

        <el-form-item label="权限">
          <el-radio-group v-model="editForm.permission">
            <el-radio value="view">仅查看</el-radio>
            <el-radio value="download">可下载</el-radio>
          </el-radio-group>
        </el-form-item>

        <el-form-item label="新密码">
          <el-input
            v-model="editForm.password"
            placeholder="留空保持不变，空字符串移除密码"
            maxlength="8"
            show-password
            clearable
          />
        </el-form-item>

        <el-form-item>
          <el-button type="primary" :loading="saving" @click="handleUpdate">保存</el-button>
          <el-button @click="showEditForm = false">取消</el-button>
        </el-form-item>
      </el-form>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Loading } from '@element-plus/icons-vue'
import type { FormInstance } from 'element-plus'
import type { ShareDetailResponse, SharePermission } from '@/types'
import { useShareStore } from '@/stores'

const props = defineProps<{
  visible: boolean
  shareId: string | null
}>()

const emit = defineEmits<{
  'update:visible': [value: boolean]
  cancelled: []
  updated: []
}>()

const shareStore = useShareStore()

const dialogVisible = computed({
  get: () => props.visible,
  set: (val) => emit('update:visible', val),
})

const detail = ref<ShareDetailResponse | null>(null)
const loading = ref(false)
const saving = ref(false)
const showEditForm = ref(false)
const editFormRef = ref<FormInstance | null>(null)

const editForm = ref({
  expireDays: 7,
  permission: 'download' as SharePermission,
  password: '',
})

const statusTagType = computed(() => {
  if (!detail.value) return 'info'
  switch (detail.value.status) {
    case 'active': return 'success'
    case 'expired': return 'warning'
    case 'cancelled': return 'danger'
    default: return 'info'
  }
})

const statusLabel = computed(() => {
  if (!detail.value) return ''
  switch (detail.value.status) {
    case 'active': return '有效'
    case 'expired': return '已过期'
    case 'cancelled': return '已取消'
    default: return detail.value.status
  }
})

async function onOpen(): Promise<void> {
  if (!props.shareId) return
  loading.value = true
  try {
    await shareStore.fetchShareDetail(props.shareId)
    detail.value = shareStore.currentShare
  } finally {
    loading.value = false
  }
}

function onClosed(): void {
  detail.value = null
  showEditForm.value = false
}

async function handleUpdate(): Promise<void> {
  if (!props.shareId) return
  saving.value = true
  try {
    const data: Record<string, unknown> = {
      expire_days: editForm.value.expireDays,
      permission: editForm.value.permission,
    }
    if (editForm.value.password !== '') {
      data.password = editForm.value.password
    }
    await shareStore.updateShare(props.shareId, data)
    showEditForm.value = false
    emit('updated')
    await onOpen()
  } catch {
    // store already shows error
  } finally {
    saving.value = false
  }
}

async function handleCancel(): Promise<void> {
  if (!props.shareId) return
  try {
    await ElMessageBox.confirm(
      '取消后分享链接将立即失效，确定要取消吗？',
      '确认取消分享',
      {
        confirmButtonText: '取消分享',
        cancelButtonText: '保留',
        type: 'warning',
        confirmButtonClass: 'el-button--danger',
      },
    )
  } catch {
    return
  }

  try {
    await shareStore.cancelShares([props.shareId])
    ElMessage.success('分享已取消')
    emit('cancelled')
    dialogVisible.value = false
  } catch {
    // store already shows error
  }
}

async function copyText(text: string): Promise<void> {
  try {
    await navigator.clipboard.writeText(text)
    ElMessage.success('已复制到剪贴板')
  } catch {
    ElMessage.error('复制失败，请手动复制')
  }
}

function formatDate(dateStr: string): string {
  try {
    return new Date(dateStr).toLocaleString('zh-CN')
  } catch {
    return dateStr
  }
}

function formatSize(bytes: number): string {
  if (bytes === 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(1024))
  const val = bytes / 1024 ** i
  return `${val.toFixed(i === 0 ? 0 : 1)} ${units[i]}`
}
</script>

<style scoped>
.detail-loading {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 32px 0;
  color: var(--el-text-color-secondary);
}

.link-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.section-title {
  margin: 20px 0 8px;
  font-size: 14px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.action-bar {
  display: flex;
  gap: 8px;
  margin-top: 16px;
}

.edit-form {
  margin-top: 8px;
}
</style>
