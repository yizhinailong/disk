<template>
  <div class="admin-system-page">
    <!-- Page Header -->
    <div class="page-header">
      <h2 class="page-title">系统监控</h2>
      <el-button
        type="primary"
        :icon="Refresh"
        :loading="adminStore.loading"
        @click="refreshAll"
      >
        刷新
      </el-button>
    </div>

    <PageState :state="pageState" error-text="加载系统信息失败" @retry="refreshAll">
      <!-- ==================== Overview Stats ==================== -->
      <section class="section">
        <h3 class="section-title">概览统计</h3>
        <el-row :gutter="16">
          <el-col :xs="12" :sm="12" :md="6">
            <div class="stat-card stat-card--blue">
              <div class="stat-card__icon">
                <el-icon :size="28"><User /></el-icon>
              </div>
              <div class="stat-card__body">
                <span class="stat-card__label">总用户数</span>
                <span class="stat-card__value">
                  {{ overview?.user_count ?? '-' }}
                </span>
              </div>
            </div>
          </el-col>

          <el-col :xs="12" :sm="12" :md="6">
            <div class="stat-card stat-card--green">
              <div class="stat-card__icon">
                <el-icon :size="28"><Document /></el-icon>
              </div>
              <div class="stat-card__body">
                <span class="stat-card__label">总文件数</span>
                <span class="stat-card__value">
                  {{ overview?.file_count ?? '-' }}
                </span>
              </div>
            </div>
          </el-col>

          <el-col :xs="12" :sm="12" :md="6">
            <div class="stat-card stat-card--orange">
              <div class="stat-card__icon">
                <el-icon :size="28"><Coin /></el-icon>
              </div>
              <div class="stat-card__body">
                <span class="stat-card__label">存储总量</span>
                <span class="stat-card__value">
                  <SizeDisplay
                    v-if="overview?.storage_size != null"
                    :bytes="overview.storage_size"
                  />
                  <template v-else>-</template>
                </span>
              </div>
            </div>
          </el-col>

          <el-col :xs="12" :sm="12" :md="6">
            <div class="stat-card stat-card--purple">
              <div class="stat-card__icon">
                <el-icon :size="28"><Share /></el-icon>
              </div>
              <div class="stat-card__body">
                <span class="stat-card__label">分享总数</span>
                <span class="stat-card__value">
                  {{ overview?.share_count ?? '-' }}
                </span>
              </div>
            </div>
          </el-col>
        </el-row>
      </section>

      <!-- ==================== System Status ==================== -->
      <section class="section">
        <h3 class="section-title">系统状态</h3>
        <div class="status-grid">
          <!-- Version & Uptime -->
          <div class="status-card">
            <div class="status-card__header">
              <el-icon><Cpu /></el-icon>
              <span>基本信息</span>
            </div>
            <div class="status-card__body">
              <div class="status-row">
                <span class="status-row__label">版本</span>
                <span class="status-row__value">
                  {{ system?.version ?? '-' }}
                </span>
              </div>
              <div class="status-row">
                <span class="status-row__label">运行时间</span>
                <span class="status-row__value">
                  {{ formatUptime(system?.uptime) }}
                </span>
              </div>
            </div>
          </div>

          <!-- MySQL -->
          <div class="status-card">
            <div class="status-card__header">
              <el-icon><Coin /></el-icon>
              <span>MySQL</span>
              <el-tag
                size="small"
                :type="system?.mysql?.connected ? 'success' : 'danger'"
                class="status-tag"
              >
                {{ system?.mysql?.connected ? '已连接' : '未连接' }}
              </el-tag>
            </div>
            <div class="status-card__body">
              <div class="status-row">
                <span class="status-row__label">延迟</span>
                <span class="status-row__value">
                  {{ system?.mysql?.latency_ms != null ? `${system.mysql.latency_ms} ms` : '-' }}
                </span>
              </div>
              <div class="status-row">
                <span class="status-row__label">连接数</span>
                <span class="status-row__value">
                  {{ system?.mysql?.connection_count ?? '-' }}
                </span>
              </div>
            </div>
          </div>

          <!-- Redis -->
          <div class="status-card">
            <div class="status-card__header">
              <el-icon><Odometer /></el-icon>
              <span>Redis</span>
              <el-tag
                size="small"
                :type="system?.redis?.connected ? 'success' : 'danger'"
                class="status-tag"
              >
                {{ system?.redis?.connected ? '已连接' : '未连接' }}
              </el-tag>
            </div>
            <div class="status-card__body">
              <div class="status-row">
                <span class="status-row__label">延迟</span>
                <span class="status-row__value">
                  {{ system?.redis?.latency_ms != null ? `${system.redis.latency_ms} ms` : '-' }}
                </span>
              </div>
            </div>
          </div>

          <!-- Disk Usage -->
          <div class="status-card">
            <div class="status-card__header">
              <el-icon><Files /></el-icon>
              <span>磁盘使用</span>
            </div>
            <div class="status-card__body">
              <el-progress
                :percentage="system?.disk?.percentage ?? 0"
                :stroke-width="12"
                :color="diskColor"
                class="disk-progress"
              />
              <div class="status-row">
                <span class="status-row__label">已用 / 总量</span>
                <span class="status-row__value">
                  <template v-if="system?.disk">
                    <SizeDisplay :bytes="system.disk.used" /> /
                    <SizeDisplay :bytes="system.disk.total" />
                  </template>
                  <template v-else>-</template>
                </span>
              </div>
              <div class="status-row">
                <span class="status-row__label">可用空间</span>
                <span class="status-row__value">
                  <SizeDisplay v-if="system?.disk" :bytes="system.disk.free" />
                  <template v-else>-</template>
                </span>
              </div>
            </div>
          </div>
        </div>
      </section>

      <!-- ==================== Storage Stats ==================== -->
      <section class="section">
        <h3 class="section-title">存储统计</h3>
        <el-row :gutter="16">
          <el-col :xs="12" :sm="8" :md="4">
            <div class="mini-stat">
              <span class="mini-stat__value">
                {{ storage?.total_users ?? '-' }}
              </span>
              <span class="mini-stat__label">总用户</span>
            </div>
          </el-col>
          <el-col :xs="12" :sm="8" :md="4">
            <div class="mini-stat">
              <span class="mini-stat__value">
                {{ storage?.active_user_count ?? '-' }}
              </span>
              <span class="mini-stat__label">活跃用户</span>
            </div>
          </el-col>
          <el-col :xs="12" :sm="8" :md="4">
            <div class="mini-stat">
              <span class="mini-stat__value">
                {{ storage?.total_files ?? '-' }}
              </span>
              <span class="mini-stat__label">总文件</span>
            </div>
          </el-col>
          <el-col :xs="12" :sm="8" :md="4">
            <div class="mini-stat">
              <span class="mini-stat__value">
                {{ storage?.total_folders ?? '-' }}
              </span>
              <span class="mini-stat__label">总目录</span>
            </div>
          </el-col>
          <el-col :xs="12" :sm="8" :md="4">
            <div class="mini-stat">
              <span class="mini-stat__value">
                <SizeDisplay
                  v-if="storage?.total_size != null"
                  :bytes="storage.total_size"
                />
                <template v-else>-</template>
              </span>
              <span class="mini-stat__label">总存储量</span>
            </div>
          </el-col>
          <el-col :xs="12" :sm="8" :md="4">
            <div class="mini-stat">
              <span class="mini-stat__value">
                {{ storage?.user_count ?? '-' }}
              </span>
              <span class="mini-stat__label">已注册用户</span>
            </div>
          </el-col>
        </el-row>
      </section>
    </PageState>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted } from 'vue'
import {
  Refresh,
  User,
  Document,
  Coin,
  Share,
  Cpu,
  Files,
  Odometer,
} from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import { useAdminStore } from '@/stores/admin'
import PageState from '@/components/base/PageState.vue'
import SizeDisplay from '@/components/base/SizeDisplay.vue'

const adminStore = useAdminStore()

const overview = computed(() => adminStore.statsOverview)
const system = computed(() => adminStore.statsSystem)
const storage = computed(() => adminStore.storageStats)

const pageState = computed(() => {
  if (adminStore.loading && !overview.value && !system.value && !storage.value) {
    return 'loading' as const
  }
  if (!overview.value && !system.value && !storage.value) {
    return 'empty' as const
  }
  return 'content' as const
})

function formatUptime(seconds?: number): string {
  if (seconds == null) return '-'
  const d = Math.floor(seconds / 86400)
  const h = Math.floor((seconds % 86400) / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const parts: string[] = []
  if (d > 0) parts.push(`${d}天`)
  if (h > 0) parts.push(`${h}小时`)
  if (m > 0) parts.push(`${m}分钟`)
  if (parts.length === 0) parts.push('刚刚启动')
  return parts.join(' ')
}

const diskColor = computed(() => {
  const pct = system.value?.disk?.percentage ?? 0
  if (pct >= 90) return '#f56c6c'
  if (pct >= 70) return '#e6a23c'
  return '#67c23a'
})

async function refreshAll() {
  try {
    await Promise.all([
      adminStore.fetchStatsOverview(),
      adminStore.fetchStatsSystem(),
      adminStore.fetchStorageStats(),
    ])
  } catch {
    ElMessage.error('加载系统信息失败')
  }
}

onMounted(() => {
  refreshAll()
})
</script>

<style scoped>
.admin-system-page {
  display: flex;
  flex-direction: column;
  gap: 0;
}

/* ==================== Page Header ==================== */
.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 20px;
}

.page-title {
  font-size: 20px;
  font-weight: 600;
  color: #303133;
  margin: 0;
}

/* ==================== Sections ==================== */
.section {
  margin-bottom: 24px;
}

.section-title {
  font-size: 16px;
  font-weight: 600;
  color: #303133;
  margin: 0 0 16px;
  padding-left: 10px;
  border-left: 3px solid var(--el-color-primary);
}

/* ==================== Overview Stat Cards ==================== */
.stat-card {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 20px;
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.06);
  transition: box-shadow 0.2s;
}

.stat-card:hover {
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.1);
}

.stat-card__icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 52px;
  height: 52px;
  border-radius: 12px;
  flex-shrink: 0;
}

.stat-card--blue .stat-card__icon {
  background: #ecf5ff;
  color: #409eff;
}

.stat-card--green .stat-card__icon {
  background: #f0f9eb;
  color: #67c23a;
}

.stat-card--orange .stat-card__icon {
  background: #fdf6ec;
  color: #e6a23c;
}

.stat-card--purple .stat-card__icon {
  background: #f3e8ff;
  color: #9b59b6;
}

.stat-card__body {
  display: flex;
  flex-direction: column;
  gap: 4px;
  min-width: 0;
}

.stat-card__label {
  font-size: 13px;
  color: #909399;
}

.stat-card__value {
  font-size: 22px;
  font-weight: 700;
  color: #303133;
  line-height: 1.2;
  white-space: nowrap;
}

/* ==================== System Status Grid ==================== */
.status-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
}

@media (max-width: 900px) {
  .status-grid {
    grid-template-columns: 1fr;
  }
}

.status-card {
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.06);
  overflow: hidden;
}

.status-card__header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 14px 20px;
  background: #fafbfc;
  border-bottom: 1px solid #ebeef5;
  font-size: 14px;
  font-weight: 600;
  color: #303133;
}

.status-tag {
  margin-left: auto;
}

.status-card__body {
  padding: 16px 20px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.status-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.status-row__label {
  font-size: 13px;
  color: #909399;
  flex-shrink: 0;
}

.status-row__value {
  font-size: 14px;
  color: #303133;
  font-weight: 500;
  text-align: right;
  display: flex;
  align-items: center;
  gap: 4px;
}

.disk-progress {
  margin-bottom: 4px;
}

/* ==================== Storage Stats Mini Cards ==================== */
.mini-stat {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  padding: 20px 12px;
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.06);
  text-align: center;
}

.mini-stat__value {
  font-size: 20px;
  font-weight: 700;
  color: #303133;
  line-height: 1.3;
}

.mini-stat__label {
  font-size: 13px;
  color: #909399;
}
</style>
