<template>
  <el-container class="admin-shell">
    <!-- ==================== Sidebar ==================== -->
    <el-aside width="200px" class="admin-aside">
      <div class="aside-inner">
        <!-- Logo -->
        <div class="aside-logo">
          <span class="logo-text">Disk 管理后台</span>
        </div>

        <!-- Navigation Menu -->
        <el-menu
          :default-active="activeMenu"
          :collapse="false"
          :collapse-transition="false"
          router
          class="aside-menu"
          background-color="#1d1e1f"
          text-color="#bfcbd9"
          active-text-color="#409eff"
        >
          <el-menu-item index="admin-users" route="/admin/users">
            <el-icon><User /></el-icon>
            <template #title>用户管理</template>
          </el-menu-item>

          <el-menu-item index="admin-shares" route="/admin/shares">
            <el-icon><Share /></el-icon>
            <template #title>分享管理</template>
          </el-menu-item>

          <el-menu-item index="admin-logs" route="/admin/logs">
            <el-icon><Document /></el-icon>
            <template #title>操作日志</template>
          </el-menu-item>

          <el-menu-item index="admin-system" route="/admin/system">
            <el-icon><Monitor /></el-icon>
            <template #title>系统监控</template>
          </el-menu-item>
        </el-menu>
      </div>
    </el-aside>

    <!-- ==================== Main Area ==================== -->
    <el-container class="admin-main-container">
      <!-- Header -->
      <el-header class="admin-header" height="56px">
        <div class="header-left">
          <el-button
            class="back-btn"
            :icon="Back"
            text
            @click="router.push('/drive')"
          />
          <span class="header-title">管理后台</span>
        </div>

        <div class="header-right">
          <!-- User dropdown -->
          <el-dropdown trigger="click" @command="handleUserCommand">
            <div class="header-user">
              <el-avatar :size="32" :src="avatarUrl">
                {{ userInitial }}
              </el-avatar>
              <span v-if="authStore.user" class="user-name">
                {{ authStore.user.nickname || authStore.user.username }}
              </span>
              <el-tag size="small" type="warning" class="admin-tag">管理员</el-tag>
            </div>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="drive">
                  <el-icon><FolderOpened /></el-icon>
                  返回网盘
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
      <el-main class="admin-content">
        <router-view />
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '@/stores/auth'
import {
  User,
  Share,
  Document,
  Monitor,
  Back,
  SwitchButton,
  FolderOpened,
} from '@element-plus/icons-vue'

const router = useRouter()
const route = useRoute()
const authStore = useAuthStore()

// Active menu derived from current route name
const activeMenu = computed(() => {
  const name = route.name as string
  if (name === 'admin-users') return 'admin-users'
  if (name === 'admin-shares') return 'admin-shares'
  if (name === 'admin-logs') return 'admin-logs'
  if (name === 'admin-system') return 'admin-system'
  return 'admin-users'
})

// User avatar display
const avatarUrl = computed(() => authStore.user?.avatar ?? '')
const userInitial = computed(() => {
  const name = authStore.user?.nickname || authStore.user?.username || ''
  return name.charAt(0).toUpperCase()
})

function handleUserCommand(command: string) {
  if (command === 'drive') {
    router.push('/drive')
  } else if (command === 'logout') {
    authStore.logout()
  }
}
</script>

<style scoped>
/* ==================== Layout Shell ==================== */
.admin-shell {
  height: 100vh;
  min-width: 1366px;
  min-height: 768px;
  overflow: hidden;
}

/* ==================== Sidebar ==================== */
.admin-aside {
  background: #1d1e1f;
  overflow: hidden;
  flex-shrink: 0;
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
  border-bottom: 1px solid #333;
  flex-shrink: 0;
}

.logo-text {
  font-size: 16px;
  font-weight: 700;
  color: #e0e0e0;
  letter-spacing: 1px;
  user-select: none;
  white-space: nowrap;
}

.aside-menu {
  flex: 1;
  border-right: none;
  padding-top: 8px;
  overflow-y: auto;
}

.aside-menu:not(.el-menu--collapse) {
  width: 200px;
}

/* ==================== Main Container ==================== */
.admin-main-container {
  flex-direction: column;
  overflow: hidden;
}

/* ==================== Header ==================== */
.admin-header {
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

.back-btn {
  font-size: 18px;
  color: #606266;
}

.header-title {
  font-size: 16px;
  font-weight: 600;
  color: #303133;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 12px;
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

.admin-tag {
  flex-shrink: 0;
}

/* ==================== Content ==================== */
.admin-content {
  background: #f5f7fa;
  overflow-y: auto;
  padding: 20px;
}
</style>
