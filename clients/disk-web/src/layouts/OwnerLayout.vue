<template>
  <el-container class="owner-shell">
    <!-- ==================== Sidebar ==================== -->
    <el-aside :width="uiStore.sidebarCollapsed ? '64px' : '220px'" class="owner-aside">
      <div class="aside-inner">
        <!-- Logo -->
        <div class="aside-logo">
          <span v-if="!uiStore.sidebarCollapsed" class="logo-text">Disk</span>
          <span v-else class="logo-text logo-text--collapsed">D</span>
        </div>

        <!-- Navigation Menu -->
        <el-menu
          :default-active="activeMenu"
          :collapse="uiStore.sidebarCollapsed"
          :collapse-transition="false"
          router
          class="aside-menu"
        >
          <el-menu-item index="drive" route="/drive">
            <el-icon><Document /></el-icon>
            <template #title>我的文件</template>
          </el-menu-item>

          <el-menu-item index="shares" route="/shares">
            <el-icon><Share /></el-icon>
            <template #title>我的分享</template>
          </el-menu-item>

          <el-menu-item index="trash" route="/trash">
            <el-icon><Delete /></el-icon>
            <template #title>回收站</template>
          </el-menu-item>

          <el-menu-item index="transfers" route="/transfers">
            <el-icon><Upload /></el-icon>
            <template #title>传输中心</template>
          </el-menu-item>

          <el-menu-item index="settings" route="/settings">
            <el-icon><Setting /></el-icon>
            <template #title>设置</template>
          </el-menu-item>

          <el-menu-item v-if="authStore.isAdmin" index="admin" route="/admin/users">
            <el-icon><Monitor /></el-icon>
            <template #title>管理后台</template>
          </el-menu-item>
        </el-menu>

        <!-- Storage Stats -->
        <div class="aside-storage">
          <template v-if="!uiStore.sidebarCollapsed">
            <el-progress
              :percentage="profileStore.storagePercentage"
              :stroke-width="6"
              :show-text="false"
              color="#409eff"
            />
            <span class="storage-text">{{ profileStore.quotaFormatted }}</span>
          </template>
          <el-tooltip v-else content="存储空间" placement="right">
            <el-icon class="storage-icon-collapsed"><Coin /></el-icon>
          </el-tooltip>
        </div>
      </div>
    </el-aside>

    <!-- ==================== Main Area ==================== -->
    <el-container class="owner-main-container">
      <!-- Header -->
      <el-header class="owner-header" height="56px">
        <div class="header-left">
          <el-button
            class="collapse-btn"
            :icon="uiStore.sidebarCollapsed ? Expand : Fold"
            text
            @click="uiStore.toggleSidebar()"
          />
          <!-- Breadcrumb placeholder area -->
          <div class="header-breadcrumb" />
        </div>

        <div class="header-right">
          <!-- Search input -->
          <el-input
            v-model="headerSearchQuery"
            placeholder="搜索文件..."
            :prefix-icon="Search"
            clearable
            class="header-search"
            @keyup.enter="handleSearch"
            @clear="handleClearSearch"
          />

          <!-- User dropdown -->
          <el-dropdown trigger="click" @command="handleUserCommand">
            <div class="header-user">
              <el-avatar :size="32" :src="avatarUrl">
                {{ userInitial }}
              </el-avatar>
              <span v-if="authStore.user" class="user-name">
                {{ authStore.user.nickname || authStore.user.username }}
              </span>
            </div>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="profile">
                  <el-icon><User /></el-icon>
                  个人资料
                </el-dropdown-item>
                <el-dropdown-item command="logout" divided>
                  <el-icon><SwitchButton /></el-icon>
                  退出登录
                </el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </el-header>

      <!-- Content -->
      <el-main class="owner-content">
        <router-view />
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useUiStore } from '@/stores/ui'
import { useAuthStore } from '@/stores/auth'
import { useProfileStore } from '@/stores/profile'
import {
  Document,
  Share,
  Delete,
  Upload,
  Setting,
  Fold,
  Expand,
  User,
  SwitchButton,
  Search,
  Monitor,
  Coin,
} from '@element-plus/icons-vue'

const router = useRouter()
const route = useRoute()
const uiStore = useUiStore()
const authStore = useAuthStore()
const profileStore = useProfileStore()

const headerSearchQuery = ref('')

// Active menu derived from current route path
const activeMenu = computed(() => {
  const path = route.path
  if (path.startsWith('/admin')) return 'admin'
  if (path.startsWith('/trash')) return 'trash'
  if (path.startsWith('/shares')) return 'shares'
  if (path.startsWith('/transfers')) return 'transfers'
  if (path.startsWith('/settings')) return 'settings'
  if (path.startsWith('/drive')) return 'drive'
  return 'drive'
})

// User avatar display
const avatarUrl = computed(() => authStore.user?.avatar ?? '')
const userInitial = computed(() => {
  const name = authStore.user?.nickname || authStore.user?.username || ''
  return name.charAt(0).toUpperCase()
})

function handleSearch(): void {
  const q = headerSearchQuery.value.trim()
  if (q) {
    router.push({ path: '/drive', query: { q } })
  }
}

function handleClearSearch(): void {
  headerSearchQuery.value = ''
  if (route.query.q) {
    router.push({ path: '/drive' })
  }
}

function handleUserCommand(command: string) {
  if (command === 'profile') {
    router.push('/settings')
  } else if (command === 'logout') {
    authStore.logout()
  }
}
</script>

<style scoped>
/* ==================== Layout Shell ==================== */
.owner-shell {
  height: 100vh;
  min-width: 1366px;
  min-height: 768px;
  overflow: hidden;
}

/* ==================== Sidebar ==================== */
.owner-aside {
  background: #fff;
  border-right: 1px solid #e8e8e8;
  transition: width 0.2s ease;
  overflow: hidden;
}

.aside-inner {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.aside-logo {
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-bottom: 1px solid #f0f0f0;
  flex-shrink: 0;
}

.logo-text {
  font-size: 22px;
  font-weight: 700;
  color: #303133;
  letter-spacing: 1px;
  user-select: none;
}

.logo-text--collapsed {
  font-size: 24px;
}

.aside-menu {
  flex: 1;
  border-right: none;
  padding-top: 8px;
  overflow-y: auto;
}

/* Remove el-menu default border */
.aside-menu:not(.el-menu--collapse) {
  width: 220px;
}

/* ==================== Storage Stats ==================== */
.aside-storage {
  padding: 16px;
  border-top: 1px solid #f0f0f0;
  flex-shrink: 0;
}

.storage-text {
  display: block;
  margin-top: 8px;
  font-size: 12px;
  color: #909399;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.storage-icon-collapsed {
  display: flex;
  justify-content: center;
  font-size: 18px;
  color: #909399;
}

/* ==================== Main Container ==================== */
.owner-main-container {
  flex-direction: column;
  overflow: hidden;
}

/* ==================== Header ==================== */
.owner-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: #fff;
  border-bottom: 1px solid #e8e8e8;
  padding: 0 16px;
  flex-shrink: 0;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.collapse-btn {
  font-size: 18px;
  color: #606266;
}

.header-breadcrumb {
  min-width: 120px;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.header-search {
  width: 280px;
}

.header-search :deep(.el-input__wrapper) {
  background: #f5f7fa;
  border-radius: 8px;
  box-shadow: none;
}

.header-search :deep(.el-input__wrapper:hover),
.header-search :deep(.el-input__wrapper.is-focus) {
  box-shadow: 0 0 0 1px var(--el-color-primary) inset;
}

.header-user {
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  padding: 4px 8px;
  border-radius: 6px;
  transition: background 0.15s;
}

.header-user:hover {
  background: #f5f7fa;
}

.user-name {
  font-size: 14px;
  color: #303133;
  max-width: 120px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

/* ==================== Content ==================== */
.owner-content {
  background: #f5f7fa;
  overflow-y: auto;
  padding: 20px;
}
</style>
