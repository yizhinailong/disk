<template>
  <div class="settings-page">
    <el-tabs v-model="activeTab" class="settings-page__tabs">
      <!-- ==================== 个人资料 ==================== -->
      <el-tab-pane label="个人资料" name="profile">
        <div class="settings-page__panel">
          <el-form
            v-loading="profileStore.loading"
            label-width="100px"
            class="settings-page__form"
          >
            <el-form-item label="头像">
              <el-avatar :size="64" :src="profileForm.avatar">
                {{ userInitial }}
              </el-avatar>
            </el-form-item>

            <el-form-item label="头像 URL">
              <el-input
                v-model="profileForm.avatar"
                placeholder="输入头像图片 URL"
                clearable
              />
            </el-form-item>

            <el-form-item label="用户名">
              <el-input :model-value="profileStore.profile?.username" disabled />
            </el-form-item>

            <el-form-item label="邮箱">
              <el-input :model-value="profileStore.profile?.email" disabled />
            </el-form-item>

            <el-form-item label="昵称">
              <el-input
                v-model="profileForm.nickname"
                placeholder="输入昵称"
                maxlength="64"
                show-word-limit
                clearable
              />
            </el-form-item>

            <el-form-item label="注册时间">
              <el-input :model-value="formattedCreatedAt" disabled />
            </el-form-item>

            <el-form-item>
              <el-button type="primary" :loading="profileStore.loading" @click="handleSaveProfile">
                保存修改
              </el-button>
            </el-form-item>
          </el-form>
        </div>
      </el-tab-pane>

      <!-- ==================== 修改密码 ==================== -->
      <el-tab-pane label="修改密码" name="password">
        <div class="settings-page__panel">
          <el-form
            ref="passwordFormRef"
            :model="passwordForm"
            :rules="passwordRules"
            label-width="120px"
            class="settings-page__form"
          >
            <el-form-item label="当前密码" prop="oldPassword">
              <el-input
                v-model="passwordForm.oldPassword"
                type="password"
                show-password
                placeholder="输入当前密码"
              />
            </el-form-item>

            <el-form-item label="新密码" prop="newPassword">
              <el-input
                v-model="passwordForm.newPassword"
                type="password"
                show-password
                placeholder="8-64 位，需含大小写字母和数字"
              />
            </el-form-item>

            <el-form-item label="确认新密码" prop="confirmPassword">
              <el-input
                v-model="passwordForm.confirmPassword"
                type="password"
                show-password
                placeholder="再次输入新密码"
              />
            </el-form-item>

            <el-form-item>
              <el-button type="primary" :loading="passwordSubmitting" @click="handleChangePassword">
                修改密码
              </el-button>
            </el-form-item>
          </el-form>
        </div>
      </el-tab-pane>

      <!-- ==================== 存储空间 ==================== -->
      <el-tab-pane label="存储空间" name="storage">
        <div class="settings-page__panel">
          <div v-loading="!profileStore.storageStats" class="settings-page__storage">
            <template v-if="profileStore.storageStats">
              <div class="storage-overview">
                <div class="storage-overview__title">存储使用概况</div>
                <el-progress
                  :percentage="profileStore.storagePercentage"
                  :stroke-width="12"
                  :format="() => profileStore.quotaFormatted"
                />
              </div>

              <div class="storage-numbers">
                <div class="storage-numbers__item">
                  <span class="storage-numbers__label">已使用</span>
                  <span class="storage-numbers__value">{{ formatBytes(profileStore.storageStats.used) }}</span>
                </div>
                <div class="storage-numbers__item">
                  <span class="storage-numbers__label">总容量</span>
                  <span class="storage-numbers__value">{{ formatBytes(profileStore.storageStats.quota) }}</span>
                </div>
              </div>

              <div v-if="profileStore.profile" class="storage-counts">
                <div class="storage-counts__item">
                  <span class="storage-counts__label">文件数</span>
                  <span class="storage-counts__value">{{ profileStore.profile.file_count }}</span>
                </div>
                <div class="storage-counts__item">
                  <span class="storage-counts__label">文件夹数</span>
                  <span class="storage-counts__value">{{ profileStore.profile.folder_count }}</span>
                </div>
              </div>

              <div v-if="profileStore.storageStats.categories.length > 0" class="storage-categories">
                <div class="storage-categories__title">分类占用</div>
                <div
                  v-for="cat in profileStore.storageStats.categories"
                  :key="cat.type"
                  class="storage-categories__row"
                >
                  <span class="storage-categories__name">{{ categoryName(cat.type) }}</span>
                  <el-progress
                    :percentage="categoryPercentage(cat.size)"
                    :stroke-width="8"
                    :show-text="false"
                    class="storage-categories__bar"
                  />
                  <span class="storage-categories__size">{{ formatBytes(cat.size) }}</span>
                  <span class="storage-categories__count">{{ cat.count }} 个文件</span>
                </div>
              </div>
            </template>
          </div>
        </div>
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import type { FormInstance, FormRules } from 'element-plus'
import { useProfileStore } from '@/stores/profile'

const profileStore = useProfileStore()

const activeTab = ref('profile')

const profileForm = reactive({
  nickname: '',
  avatar: '',
})

const passwordFormRef = ref<FormInstance>()
const passwordSubmitting = ref(false)

const passwordForm = reactive({
  oldPassword: '',
  newPassword: '',
  confirmPassword: '',
})

const validateConfirmPassword = (_rule: unknown, value: string, callback: (error?: Error) => void) => {
  if (value !== passwordForm.newPassword) {
    callback(new Error('两次输入的密码不一致'))
  } else {
    callback()
  }
}

const passwordRules: FormRules = {
  oldPassword: [
    { required: true, message: '请输入当前密码', trigger: 'blur' },
  ],
  newPassword: [
    { required: true, message: '请输入新密码', trigger: 'blur' },
    { min: 8, max: 64, message: '密码长度为 8-64 个字符', trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: '请确认新密码', trigger: 'blur' },
    { validator: validateConfirmPassword, trigger: 'blur' },
  ],
}

const userInitial = computed(() => {
  const name = profileForm.nickname || 'U'
  return name.charAt(0).toUpperCase()
})

const formattedCreatedAt = computed(() => {
  if (!profileStore.profile?.created_at) return ''
  return new Date(profileStore.profile.created_at).toLocaleString('zh-CN')
})

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.min(
    Math.floor(Math.log(bytes) / Math.log(1024)),
    units.length - 1,
  )
  return `${(bytes / Math.pow(1024, i)).toFixed(i === 0 ? 0 : 1)} ${units[i]}`
}

function categoryName(type: string): string {
  const map: Record<string, string> = {
    document: '文档',
    image: '图片',
    video: '视频',
    audio: '音频',
    other: '其他',
  }
  return map[type] ?? type
}

function categoryPercentage(size: number): number {
  if (!profileStore.storageStats || profileStore.storageStats.quota === 0) return 0
  return Math.round((size / profileStore.storageStats.quota) * 1000) / 10
}

async function handleSaveProfile(): Promise<void> {
  try {
    await profileStore.updateProfile({
      nickname: profileForm.nickname || undefined,
      avatar: profileForm.avatar || undefined,
    })
    ElMessage.success('个人资料已更新')
  } catch (err) {
    ElMessage.error(err instanceof Error ? err.message : '更新失败')
  }
}

async function handleChangePassword(): Promise<void> {
  if (!passwordFormRef.value) return
  const valid = await passwordFormRef.value.validate().catch(() => false)
  if (!valid) return

  passwordSubmitting.value = true
  try {
    await profileStore.changePassword(passwordForm.oldPassword, passwordForm.newPassword)
    ElMessage.success('密码修改成功')
    passwordForm.oldPassword = ''
    passwordForm.newPassword = ''
    passwordForm.confirmPassword = ''
    passwordFormRef.value.resetFields()
  } catch (err) {
    ElMessage.error(err instanceof Error ? err.message : '修改失败')
  } finally {
    passwordSubmitting.value = false
  }
}

onMounted(async () => {
  await Promise.all([
    profileStore.fetchProfile(),
    profileStore.fetchStorageStats(),
  ])
  if (profileStore.profile) {
    profileForm.nickname = profileStore.profile.nickname
    profileForm.avatar = profileStore.profile.avatar
  }
})
</script>

<style scoped>
.settings-page {
  max-width: 720px;
}

.settings-page__tabs :deep(.el-tabs__header) {
  margin-bottom: 0;
}

.settings-page__panel {
  padding: 24px 0;
}

.settings-page__form {
  max-width: 520px;
}

.settings-page__storage {
  min-height: 200px;
}

.storage-overview {
  margin-bottom: 24px;
}

.storage-overview__title {
  font-size: 15px;
  font-weight: 500;
  margin-bottom: 12px;
  color: var(--el-text-color-primary);
}

.storage-numbers {
  display: flex;
  gap: 32px;
  margin-bottom: 24px;
}

.storage-numbers__item {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.storage-numbers__label {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.storage-numbers__value {
  font-size: 18px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.storage-counts {
  display: flex;
  gap: 32px;
  margin-bottom: 24px;
  padding: 16px;
  background: var(--el-fill-color-light);
  border-radius: 8px;
}

.storage-counts__item {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.storage-counts__label {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.storage-counts__value {
  font-size: 16px;
  font-weight: 500;
  color: var(--el-text-color-primary);
}

.storage-categories__title {
  font-size: 15px;
  font-weight: 500;
  margin-bottom: 12px;
  color: var(--el-text-color-primary);
}

.storage-categories__row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 0;
}

.storage-categories__name {
  width: 60px;
  font-size: 14px;
  color: var(--el-text-color-regular);
  flex-shrink: 0;
}

.storage-categories__bar {
  flex: 1;
  min-width: 120px;
}

.storage-categories__size {
  width: 80px;
  font-size: 13px;
  color: var(--el-text-color-regular);
  text-align: right;
  flex-shrink: 0;
}

.storage-categories__count {
  width: 80px;
  font-size: 13px;
  color: var(--el-text-color-secondary);
  flex-shrink: 0;
}
</style>
