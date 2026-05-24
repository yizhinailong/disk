<template>
  <el-dialog
    v-model="dialogVisible"
    title="创建分享链接"
    width="520px"
    :close-on-click-modal="false"
    destroy-on-close
    @closed="onClosed"
  >
    <!-- Create Form -->
    <template v-if="!shareResult">
      <el-form
        ref="formRef"
        :model="form"
        :rules="formRules"
        label-width="80px"
        label-position="left"
      >
        <el-form-item label="有效期" prop="expireDays">
          <el-radio-group v-model="form.expireDays">
            <el-radio :value="1">1 天</el-radio>
            <el-radio :value="7">7 天</el-radio>
            <el-radio :value="30">30 天</el-radio>
            <el-radio :value="0">永久</el-radio>
          </el-radio-group>
        </el-form-item>

        <el-form-item label="权限">
          <el-radio-group v-model="form.permission">
            <el-radio value="view">仅查看</el-radio>
            <el-radio value="download">可下载</el-radio>
          </el-radio-group>
        </el-form-item>

        <el-form-item label="访问密码">
          <div class="password-row">
            <el-switch v-model="form.enablePassword" />
            <el-input
              v-if="form.enablePassword"
              v-model="form.password"
              placeholder="4-8 位密码"
              maxlength="8"
              show-password
              style="flex: 1; margin-left: 12px"
            />
          </div>
        </el-form-item>
      </el-form>
    </template>

    <!-- Result Section -->
    <template v-else>
      <el-result icon="success" title="分享创建成功" :sub-title="undefined">
        <template #extra>
          <div class="share-result-content">
            <div class="share-result-field">
              <label>分享链接</label>
              <div class="share-result-value">
                <el-input
                  :model-value="shareResult.share_link"
                  readonly
                />
                <el-button type="primary" link @click="copyText(shareResult.share_link)">
                  复制
                </el-button>
              </div>
            </div>

            <div v-if="shareResult.password" class="share-result-field">
              <label>访问密码</label>
              <div class="share-result-value">
                <el-input
                  :model-value="shareResult.password"
                  readonly
                />
                <el-button type="primary" link @click="copyText(shareResult.password!)">
                  复制
                </el-button>
              </div>
            </div>

            <div class="share-result-field">
              <label>有效期至</label>
              <span class="share-result-text">
                {{ shareResult.expires_at ? formatExpiry(shareResult.expires_at) : '永久有效' }}
              </span>
            </div>
          </div>
        </template>
      </el-result>
    </template>

    <template #footer>
      <template v-if="!shareResult">
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button
          type="primary"
          :loading="loading"
          @click="handleSubmit"
        >
          创建
        </el-button>
      </template>
      <template v-else>
        <el-button type="primary" @click="dialogVisible = false">
          完成
        </el-button>
      </template>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { ElMessage } from 'element-plus'
import type { FormInstance, FormRules } from 'element-plus'
import type { CreateShareResponse, SharePermission } from '@/types'
import { useShareStore } from '@/stores'

interface ShareFileEntry {
  id: number
  type: 'file' | 'folder'
}

const props = defineProps<{
  visible: boolean
  files?: ShareFileEntry[]
}>()

const emit = defineEmits<{
  'update:visible': [value: boolean]
  created: [result: CreateShareResponse]
}>()

const shareStore = useShareStore()

const dialogVisible = computed({
  get: () => props.visible,
  set: (val) => emit('update:visible', val),
})

const formRef = ref<FormInstance | null>(null)
const loading = ref(false)
const shareResult = ref<CreateShareResponse | null>(null)

const form = ref({
  expireDays: 7,
  permission: 'download' as SharePermission,
  enablePassword: false,
  password: '',
})

const formRules: FormRules = {
  expireDays: [{ required: true, message: '请选择有效期', trigger: 'change' }],
}

watch(() => props.visible, (val) => {
  if (val) {
    shareResult.value = null
    form.value = {
      expireDays: 7,
      permission: 'download',
      enablePassword: false,
      password: '',
    }
  }
})

function onClosed(): void {
  formRef.value?.resetFields()
  shareResult.value = null
}

async function handleSubmit(): Promise<void> {
  if (!formRef.value) return
  try {
    await formRef.value.validate()
  } catch {
    return
  }

  if (form.value.enablePassword && (form.value.password.length < 4 || form.value.password.length > 8)) {
    ElMessage.warning('密码长度需为 4-8 位')
    return
  }

  loading.value = true
  try {
    const fileIds = props.files
      ?.filter((f) => f.type === 'file')
      .map((f) => f.id)
    const folderIds = props.files
      ?.filter((f) => f.type === 'folder')
      .map((f) => f.id)

    const result = await shareStore.createShare({
      file_ids: fileIds?.length ? fileIds : undefined,
      folder_ids: folderIds?.length ? folderIds : undefined,
      expire_days: form.value.expireDays,
      password: form.value.enablePassword ? form.value.password : undefined,
      permission: form.value.permission,
    })

    shareResult.value = result
    emit('created', result)
  } catch {
    // store already shows error
  } finally {
    loading.value = false
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

function formatExpiry(dateStr: string): string {
  try {
    return new Date(dateStr).toLocaleString('zh-CN')
  } catch {
    return dateStr
  }
}
</script>

<style scoped>
.password-row {
  display: flex;
  align-items: center;
  width: 100%;
}

.share-result-content {
  display: flex;
  flex-direction: column;
  gap: 16px;
  text-align: left;
}

.share-result-field {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.share-result-field label {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.share-result-value {
  display: flex;
  align-items: center;
  gap: 8px;
}

.share-result-text {
  font-size: 14px;
  color: var(--el-text-color-primary);
}
</style>
