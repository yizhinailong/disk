<template>
  <div class="share-verify-page">
    <div class="share-verify-card">
      <div v-if="pageState === 'error'" class="share-verify__error">
        <el-icon :size="48" class="share-verify__error-icon">
          <CircleCloseFilled />
        </el-icon>
        <p class="share-verify__error-text">{{ errorMessage }}</p>
        <el-button type="primary" @click="initShare">重试</el-button>
      </div>

      <div v-else-if="pageState === 'loading'" class="share-verify__loading">
        <el-skeleton :rows="4" animated />
      </div>

      <template v-else>
        <h2 class="share-verify__title">分享链接</h2>

        <div class="share-verify__info">
          <div class="share-verify__info-row">
            <span class="share-verify__info-label">权限</span>
            <el-tag :type="sharePermission === 'download' ? 'success' : 'info'" size="small">
              {{ sharePermission === 'download' ? '可下载' : '仅预览' }}
            </el-tag>
          </div>
          <div v-if="shareExpiresAt" class="share-verify__info-row">
            <span class="share-verify__info-label">有效期至</span>
            <TimeDisplay :time="shareExpiresAt" format="absolute" />
          </div>
        </div>

        <el-form
          v-if="needsPassword"
          ref="formRef"
          :model="form"
          :rules="rules"
          label-position="top"
          @submit.prevent="handleVerify"
        >
          <el-form-item label="访问密码" prop="password">
            <el-input
              v-model="form.password"
              type="password"
              placeholder="请输入访问密码"
              show-password
              :prefix-icon="Lock"
              size="large"
              @keyup.enter="handleVerify"
            />
          </el-form-item>

          <el-form-item>
            <el-button
              type="primary"
              size="large"
              class="share-verify__submit"
              :loading="verifying"
              @click="handleVerify"
            >
              验证
            </el-button>
          </el-form-item>
        </el-form>

        <div v-if="!needsPassword" class="share-verify__auto">
          <el-icon class="share-verify__auto-icon is-loading">
            <Loading />
          </el-icon>
          <span>正在验证并跳转...</span>
        </div>
      </template>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { ElMessage, type FormInstance, type FormRules } from 'element-plus';
import { Lock, CircleCloseFilled, Loading } from '@element-plus/icons-vue';
import { useVisitorStore } from '@/stores/visitor';
import { ApiError } from '@/api/client';
import TimeDisplay from '@/components/base/TimeDisplay.vue';

const route = useRoute();
const router = useRouter();
const visitorStore = useVisitorStore();

const formRef = ref<FormInstance>();
const verifying = ref(false);
const needsPassword = ref(false);
const sharePermission = ref<string>('download');
const shareExpiresAt = ref<string>('');
const errorMessage = ref('');

const form = reactive({
  password: '',
});

const rules: FormRules = {
  password: [
    { required: true, message: '请输入访问密码', trigger: 'blur' },
  ],
};

const pageState = computed<'loading' | 'ready' | 'error'>(() => {
  if (visitorStore.loading && !needsPassword.value && !errorMessage.value) return 'loading';
  if (errorMessage.value) return 'error';
  return 'ready';
});

async function initShare(): Promise<void> {
  const shareId = route.params.shareId as string;
  if (!shareId) {
    errorMessage.value = '无效的分享链接';
    return;
  }

  errorMessage.value = '';

  try {
    const result = await visitorStore.verifyShare(shareId);
    sharePermission.value = result.permission;

    await router.push({ name: 'share-browse', params: { shareId } });
  } catch (err) {
    if (err instanceof ApiError && err.code === 60003) {
      needsPassword.value = true;
      sharePermission.value = 'download';
    } else if (err instanceof ApiError && (err.code === 60001 || err.code === 60002)) {
      errorMessage.value = err.message;
    } else if (err instanceof Error) {
      errorMessage.value = err.message;
    } else {
      errorMessage.value = '访问分享失败';
    }
  }
}

async function handleVerify(): Promise<void> {
  const valid = await formRef.value?.validate().catch(() => false);
  if (!valid) return;

  const shareId = route.params.shareId as string;
  verifying.value = true;

  try {
    await visitorStore.verifyShare(shareId, form.password);
    ElMessage.success('验证成功');
    await router.push({ name: 'share-browse', params: { shareId } });
  } catch (err) {
    if (err instanceof Error) {
      ElMessage.error(err.message);
    } else {
      ElMessage.error('验证失败');
    }
  } finally {
    verifying.value = false;
  }
}

onMounted(() => {
  const shareId = route.params.shareId as string;
  if (visitorStore.shareToken && visitorStore.shareInfo) {
    router.push({ name: 'share-browse', params: { shareId } });
    return;
  }
  initShare();
});
</script>

<style scoped>
.share-verify-page {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  background: #f5f7fa;
}

.share-verify-card {
  width: 400px;
  padding: 40px;
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
}

.share-verify__title {
  margin: 0 0 24px;
  font-size: 24px;
  font-weight: 600;
  text-align: center;
  color: #303133;
}

.share-verify__info {
  margin-bottom: 24px;
  padding: 12px 16px;
  background: #f5f7fa;
  border-radius: 6px;
}

.share-verify__info-row {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
  color: #606266;
}

.share-verify__info-row + .share-verify__info-row {
  margin-top: 8px;
}

.share-verify__info-label {
  color: #909399;
  min-width: 64px;
}

.share-verify__submit {
  width: 100%;
}

.share-verify__auto {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 16px 0;
  color: #909399;
  font-size: 14px;
}

.share-verify__error {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  padding: 24px 0;
}

.share-verify__error-icon {
  color: var(--el-color-danger);
}

.share-verify__error-text {
  margin: 0;
  color: var(--el-text-color-secondary);
  font-size: 14px;
}

.share-verify__loading {
  padding: 24px;
}
</style>
