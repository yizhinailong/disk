import { createRouter, createWebHistory } from 'vue-router'
import type { RouteRecordRaw } from 'vue-router'
import { useAuthStore } from '@/stores/auth'
import { getAccessToken } from '@/api/client'

const routes: RouteRecordRaw[] = [
  // ==================== Public Routes ====================
  {
    path: '/login',
    name: 'login',
    component: () => import('@/views/auth/LoginView.vue'),
    meta: { requiresAuth: false },
  },
  {
    path: '/register',
    name: 'register',
    component: () => import('@/views/auth/RegisterView.vue'),
    meta: { requiresAuth: false },
  },

  // ==================== Owner Routes ====================
  {
    path: '/',
    component: () => import('@/layouts/OwnerLayout.vue'),
    meta: { requiresAuth: true },
    children: [
      {
        path: '',
        redirect: '/drive',
      },
      {
        path: 'drive',
        name: 'drive',
        component: () => import('@/views/drive/DriveView.vue'),
        meta: { requiresNonAdmin: true },
      },
      {
        path: 'transfers',
        name: 'transfers',
        component: () => import('@/views/transfer/TransferView.vue'),
        meta: { requiresNonAdmin: true },
      },
      {
        path: 'settings',
        name: 'settings',
        component: () => import('@/views/settings/SettingsView.vue'),
        meta: { requiresNonAdmin: true },
      },
      {
        path: 'shares',
        name: 'shares',
        component: () => import('@/views/drive/SharesView.vue'),
        meta: { requiresNonAdmin: true },
      },
      {
        path: 'trash',
        name: 'trash',
        component: () => import('@/views/drive/TrashView.vue'),
        meta: { requiresNonAdmin: true },
      },
    ],
  },

  // ==================== Visitor Share Routes ====================
  {
    path: '/s/:shareId/verify',
    name: 'share-verify',
    component: () => import('@/views/share/ShareVerifyView.vue'),
    meta: { requiresAuth: false },
  },
  {
    path: '/s/:shareId/browse',
    name: 'share-browse',
    component: () => import('@/views/share/ShareBrowseView.vue'),
    meta: { requiresAuth: false },
  },

  // ==================== Admin Routes ====================
  {
    path: '/admin',
    component: () => import('@/layouts/AdminLayout.vue'),
    meta: { requiresAuth: true, requiresAdmin: true },
    children: [
      {
        path: '',
        redirect: '/admin/users',
      },
      {
        path: 'users',
        name: 'admin-users',
        component: () => import('@/views/admin/AdminUsersView.vue'),
      },
      {
        path: 'shares',
        name: 'admin-shares',
        component: () => import('@/views/admin/AdminSharesView.vue'),
      },
      {
        path: 'logs',
        name: 'admin-logs',
        component: () => import('@/views/admin/AdminLogsView.vue'),
      },
      {
        path: 'system',
        name: 'admin-system',
        component: () => import('@/views/admin/AdminSystemView.vue'),
      },
    ],
  },

  // ==================== Catch-all 404 ====================
  {
    path: '/:pathMatch(.*)*',
    redirect: '/drive',
  },
]

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes,
})

// ==================== Navigation Guards ====================

router.beforeEach((to) => {
  const hasToken = !!getAccessToken()

  if (hasToken) {
    const authStore = useAuthStore()
    if (!authStore.isAuthenticated) {
      authStore.loadTokensFromStorage()
    }
  }

  if (to.meta.requiresAuth === false && hasToken) {
    const authStore = useAuthStore()
    if (authStore.isAuthenticated) {
      return authStore.isAdmin ? '/admin' : '/drive'
    }
  }

  const requiresAuth = to.matched.some((record) => record.meta.requiresAuth !== false)
  if (requiresAuth && !hasToken) {
    return {
      path: '/login',
      query: { redirect: to.fullPath },
    }
  }

  if (to.matched.some((record) => record.meta.requiresNonAdmin)) {
    const authStore = useAuthStore()
    if (authStore.isAdmin) {
      return '/admin'
    }
  }

  if (to.matched.some((record) => record.meta.requiresAdmin)) {
    const authStore = useAuthStore()
    if (!authStore.isAdmin) {
      return '/drive'
    }
  }
})

export default router
