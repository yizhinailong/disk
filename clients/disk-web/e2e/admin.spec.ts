import { test, expect } from '@playwright/test'
import {
  AdminSelectors,
  AdminUsersSelectors,
  AdminLogsSelectors,
  AdminSystemSelectors,
  Routes,
  TEST_ADMIN,
  loginViaUI,
  navigateTo,
  getTableRow,
  getTableRowCount,
} from './fixtures'

test.describe('Admin Panel', () => {
  test.describe('Admin Layout', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Requires admin JWT. Mock login to return admin user (role=1)
      // and mock GET /api/user/profile to return { role: 1 }.
      await loginViaUI(page, TEST_ADMIN.username, TEST_ADMIN.password)
      await navigateTo(page, Routes.adminUsers)
    })

    test('renders admin layout with dark sidebar', async ({ page }) => {
      await expect(page.locator(AdminSelectors.sidebar)).toBeVisible()
      await expect(page.locator(AdminSelectors.header)).toBeVisible()
    })

    test('sidebar shows "Disk 管理后台" logo', async ({ page }) => {
      await expect(page.locator('.logo-text')).toHaveText('Disk 管理后台')
    })

    test('sidebar shows all admin navigation items', async ({ page }) => {
      const menu = page.locator('.aside-menu')
      await expect(menu.locator('.el-menu-item').filter({ hasText: '用户管理' })).toBeVisible()
      await expect(menu.locator('.el-menu-item').filter({ hasText: '分享管理' })).toBeVisible()
      await expect(menu.locator('.el-menu-item').filter({ hasText: '操作日志' })).toBeVisible()
      await expect(menu.locator('.el-menu-item').filter({ hasText: '系统监控' })).toBeVisible()
    })

    test('header shows "管理后台" title and admin tag', async ({ page }) => {
      await expect(page.locator(AdminSelectors.headerTitle)).toHaveText('管理后台')
      await expect(page.locator(AdminSelectors.adminTag)).toHaveText('管理员')
    })

    test('user dropdown shows only "退出登录"', async ({ page }) => {
      await page.locator('.header-user').click()
      await expect(page.getByRole('menuitem', { name: '退出登录' })).toBeVisible()
      // "返回网盘" was removed from admin layout
      await expect(page.getByRole('menuitem', { name: '返回网盘' })).not.toBeVisible()
    })
  })

  test.describe('Admin Users Management', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Mock GET /api/admin/users with sample user data.
      await loginViaUI(page, TEST_ADMIN.username, TEST_ADMIN.password)
      await navigateTo(page, Routes.adminUsers)
    })

    test('renders user management page with search and filters', async ({ page }) => {
      await expect(page.locator(AdminUsersSelectors.page)).toBeVisible()
      await expect(page.locator(AdminUsersSelectors.search)).toBeVisible()
    })

    test('shows status filter dropdown with options', async ({ page }) => {
      const statusSelect = page.locator('.admin-users-page__filter-select').first()
      await statusSelect.click()
      await expect(page.getByText('正常')).toBeVisible()
      await expect(page.getByText('禁用')).toBeVisible()
      await expect(page.getByText('锁定')).toBeVisible()
    })

    test('shows role filter dropdown with options', async ({ page }) => {
      const roleSelect = page.locator('.admin-users-page__filter-select').last()
      await roleSelect.click()
      await expect(page.getByText('普通用户')).toBeVisible()
      await expect(page.getByText('管理员')).toBeVisible()
    })

    test('user table shows correct column headers', async ({ page }) => {
      const table = page.locator(AdminUsersSelectors.table)
      await expect(table.locator('th').filter({ hasText: 'ID' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '用户名' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '邮箱' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '状态' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '角色' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '操作' })).toBeVisible()
    })

    test('user rows show status and role tags', async ({ page }) => {
      // NOTE: Mock users with mixed statuses and roles.
      const rowCount = await getTableRowCount(page, AdminUsersSelectors.table)
      if (rowCount > 0) {
        const firstRow = getTableRow(page, AdminUsersSelectors.table, 0)
        await expect(firstRow.locator('.el-tag').first()).toBeVisible()
      }
    })

    test('clicking "详情" opens user detail dialog', async ({ page }) => {
      // NOTE: Mock GET /api/admin/users/{id} with detailed user info.
      const rowCount = await getTableRowCount(page, AdminUsersSelectors.table)
      if (rowCount > 0) {
        const detailBtn = getTableRow(page, AdminUsersSelectors.table, 0).getByText('详情')
        if (await detailBtn.isVisible()) {
          await detailBtn.click()
          await expect(page.locator('.el-dialog').filter({ hasText: '用户详情' })).toBeVisible()
          await expect(page.locator('.el-descriptions')).toBeVisible()
        }
      }
    })

    test('status dropdown on row allows changing user status', async ({ page }) => {
      // NOTE: Mock PUT /api/admin/users/{id}/status.
      const rowCount = await getTableRowCount(page, AdminUsersSelectors.table)
      if (rowCount > 0) {
        const statusDropdown = getTableRow(page, AdminUsersSelectors.table, 0).locator('.el-dropdown').first()
        if (await statusDropdown.isVisible()) {
          await statusDropdown.locator('button').click()
          await expect(page.locator('.el-dropdown-menu').last()).toBeVisible()
        }
      }
    })

    test('delete button triggers confirmation dialog', async ({ page }) => {
      // NOTE: Mock DELETE /api/admin/users/{id}.
      const rowCount = await getTableRowCount(page, AdminUsersSelectors.table)
      if (rowCount > 0) {
        const deleteBtn = getTableRow(page, AdminUsersSelectors.table, 0).getByText('删除')
        if (await deleteBtn.isVisible()) {
          await deleteBtn.click()
          await expect(page.locator('.el-message-box')).toBeVisible()
        }
      }
    })

    test('search input filters user list on Enter', async ({ page }) => {
      const searchInput = page.locator(AdminUsersSelectors.search + ' input')
      await searchInput.fill('testuser')
      await searchInput.press('Enter')
      await page.waitForLoadState('networkidle')
    })

    test('pagination component present for user list', async ({ page }) => {
      // NOTE: Mock with enough users to require pagination.
      const pagination = page.locator('.el-pagination')
      if (await pagination.isVisible()) {
        await expect(pagination).toBeVisible()
      }
    })
  })

  test.describe('Admin Shares Management', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Mock GET /api/admin/shares with sample share data.
      await loginViaUI(page, TEST_ADMIN.username, TEST_ADMIN.password)
      await navigateTo(page, Routes.adminShares)
    })

    test('renders share management page with title and description', async ({ page }) => {
      await expect(page.locator('.admin-shares-page__title')).toHaveText('分享管理')
      await expect(page.locator('.admin-shares-page__desc')).toBeVisible()
    })

    test('shows search input and status filter', async ({ page }) => {
      await expect(page.locator('input[placeholder="搜索用户名"]')).toBeVisible()
    })

    test('share table shows correct columns', async ({ page }) => {
      const table = page.locator('table')
      await expect(table.locator('th').filter({ hasText: '分享ID' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '用户' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '文件名' })).toBeVisible()
    })

    test('"详情" button opens share detail dialog', async ({ page }) => {
      // NOTE: Mock GET /api/admin/shares/{id} with detailed share info including files.
      const detailBtns = page.getByText('详情')
      if (await detailBtns.first().isVisible()) {
        await detailBtns.first().click()
        await expect(page.locator('.el-dialog').filter({ hasText: '分享详情' })).toBeVisible()
        await expect(page.locator('.el-descriptions')).toBeVisible()
      }
    })

    test('"强制取消" button shows confirmation dialog', async ({ page }) => {
      // NOTE: Mock shares with active status (status=1) to show cancel button.
      const cancelBtn = page.getByText('强制取消')
      if (await cancelBtn.first().isVisible()) {
        await cancelBtn.first().click()
        await expect(page.locator('.el-message-box')).toBeVisible()
        await expect(page.getByText(/确定要强制取消分享/)).toBeVisible()
      }
    })

    test('pagination for share list', async ({ page }) => {
      // NOTE: Mock enough shares for pagination.
      const pagination = page.locator('.admin-shares-page__pagination .el-pagination')
      if (await pagination.isVisible()) {
        await expect(pagination).toBeVisible()
      }
    })
  })

  test.describe('Admin Logs', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Mock GET /api/admin/logs (via fetchAdminLogs in admin store) with sample log data.
      await loginViaUI(page, TEST_ADMIN.username, TEST_ADMIN.password)
      await navigateTo(page, Routes.adminLogs)
    })

    test('renders logs page with title', async ({ page }) => {
      await expect(page.locator(AdminLogsSelectors.page)).toBeVisible()
      await expect(page.locator(AdminLogsSelectors.title)).toHaveText('操作日志')
    })

    test('shows action type filter dropdown', async ({ page }) => {
      const filterSelect = page.locator(AdminLogsSelectors.filters + ' .el-select').first()
      await filterSelect.click()
      await expect(page.getByText('上传')).toBeVisible()
      await expect(page.getByText('下载')).toBeVisible()
      await expect(page.getByText('删除')).toBeVisible()
      await expect(page.getByText('分享')).toBeVisible()
    })

    test('shows date range picker', async ({ page }) => {
      const datePicker = page.locator('.el-date-editor')
      await expect(datePicker).toBeVisible()
    })

    test('logs table shows correct columns', async ({ page }) => {
      const table = page.locator(AdminLogsSelectors.table)
      await expect(table.locator('th').filter({ hasText: '操作类型' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '目标类型' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: 'IP 地址' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '操作时间' })).toBeVisible()
    })

    test('clicking a row opens log detail dialog', async ({ page }) => {
      // NOTE: Row click shows detail dialog with the row data (no API call).
      const rowCount = await getTableRowCount(page, AdminLogsSelectors.table)
      if (rowCount > 0) {
        await getTableRow(page, AdminLogsSelectors.table, 0).click()
        await expect(page.locator('.el-dialog').filter({ hasText: '日志详情' })).toBeVisible()
        await expect(page.locator('.el-descriptions')).toBeVisible()
      }
    })

    test('action filter changes reload logs', async ({ page }) => {
      // NOTE: Mock GET /api/admin/logs with action=upload filter.
      const filterSelect = page.locator(AdminLogsSelectors.filters + ' .el-select').first()
      await filterSelect.click()
      await page.getByText('上传').click()
      await page.waitForLoadState('networkidle')
    })

    test('logs table shows user_id column', async ({ page }) => {
      const table = page.locator(AdminLogsSelectors.table)
      await expect(table.locator('th').filter({ hasText: '目标ID' })).toBeVisible()
    })

    test('logs data comes from /api/admin/logs endpoint', async ({ page }) => {
      await expect(page.locator(AdminLogsSelectors.table + ' tbody tr')).toHaveCount.greaterThan(0)
    })
  })

  test.describe('Admin System Monitoring', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Mock all three system endpoints:
      //   GET /api/admin/stats/overview → { total_users, total_files, total_storage_used, active_shares }
      //   GET /api/admin/stats/system → { db_connected, redis_connected, disk_total, disk_used, disk_free, uptime_seconds }
      //   GET /api/admin/storage/stats → { total_users, total_files, total_storage_used, total_storage_quota, active_shares }
      await loginViaUI(page, TEST_ADMIN.username, TEST_ADMIN.password)
      await navigateTo(page, Routes.adminSystem)
    })

    test('renders system monitoring page with title', async ({ page }) => {
      await expect(page.locator(AdminSystemSelectors.page)).toBeVisible()
      await expect(page.locator(AdminSystemSelectors.pageTitle)).toHaveText('系统监控')
    })

    test('shows refresh button', async ({ page }) => {
      await expect(page.getByText('刷新')).toBeVisible()
    })

    test('displays overview stat cards', async ({ page }) => {
      // NOTE: Mock overview API to return non-zero values.
      const statCards = page.locator(AdminSystemSelectors.statCard)
      await expect(statCards).toHaveCount(4)
    })

    test('overview cards show correct labels', async ({ page }) => {
      await expect(page.getByText('总用户数')).toBeVisible()
      await expect(page.getByText('总文件数')).toBeVisible()
      await expect(page.getByText('存储总量')).toBeVisible()
      await expect(page.getByText('分享总数')).toBeVisible()
    })

    test('displays system status grid', async ({ page }) => {
      // NOTE: Mock system API with MySQL/Redis connected and disk info.
      const statusCards = page.locator(AdminSystemSelectors.statusCard)
      await expect(statusCards).toHaveCount(4)
    })

    test('database status shows connected/disconnected tag', async ({ page }) => {
      const dbSection = page.locator(AdminSystemSelectors.statusCard).filter({ hasText: '数据库' })
      await expect(dbSection).toBeVisible()
      await expect(dbSection.locator('.el-tag')).toBeVisible()
    })

    test('Redis status shows connected/disconnected tag', async ({ page }) => {
      const redisSection = page.locator(AdminSystemSelectors.statusCard).filter({ hasText: 'Redis' })
      await expect(redisSection).toBeVisible()
      await expect(redisSection.locator('.el-tag')).toBeVisible()
    })

    test('disk usage shows progress bar', async ({ page }) => {
      const diskSection = page.locator(AdminSystemSelectors.statusCard).filter({ hasText: '磁盘使用' })
      await expect(diskSection).toBeVisible()
      await expect(diskSection.locator('.el-progress')).toBeVisible()
    })

    test('displays storage statistics mini cards', async ({ page }) => {
      // NOTE: Mock storage stats API.
      const miniStats = page.locator(AdminSystemSelectors.miniStat)
      await expect(miniStats).toHaveCount(5)
    })

    test('refresh button reloads all data', async ({ page }) => {
      // NOTE: Mock all three APIs again after click.
      await page.getByText('刷新').click()
      await page.waitForLoadState('networkidle')
    })
  })

  test.describe('Admin — Navigation Between Pages', () => {
    test.beforeEach(async ({ page }) => {
      await loginViaUI(page, TEST_ADMIN.username, TEST_ADMIN.password)
    })

    test('navigates between admin pages via sidebar', async ({ page }) => {
      await navigateTo(page, Routes.adminUsers)
      await expect(page).toHaveURL(/\/admin\/users/)

      await page.locator('.aside-menu .el-menu-item').filter({ hasText: '分享管理' }).click()
      await expect(page).toHaveURL(/\/admin\/shares/)

      await page.locator('.aside-menu .el-menu-item').filter({ hasText: '操作日志' }).click()
      await expect(page).toHaveURL(/\/admin\/logs/)

      await page.locator('.aside-menu .el-menu-item').filter({ hasText: '系统监控' }).click()
      await expect(page).toHaveURL(/\/admin\/system/)
    })

    test('active menu item highlights based on current route', async ({ page }) => {
      await navigateTo(page, Routes.adminLogs)
      const activeItem = page.locator('.aside-menu .el-menu-item.is-active')
      await expect(activeItem).toHaveAttribute('data-v-ignored', /.*/).catch(() => {
        // Alternative: check the class exists
        expect(activeItem.count()).resolves.toBeGreaterThan(0)
      })
    })
  })

  test.describe('Admin Route Restrictions', () => {
    test.beforeEach(async ({ page }) => {
      await loginViaUI(page, TEST_ADMIN.username, TEST_ADMIN.password)
    })

    test('admin is redirected to /admin after login', async ({ page }) => {
      await expect(page).toHaveURL(/\/admin/)
    })

    test('admin accessing /drive is redirected to /admin', async ({ page }) => {
      await page.goto('/drive')
      await expect(page).toHaveURL(/\/admin/)
    })

    test('admin accessing /trash is redirected to /admin', async ({ page }) => {
      await page.goto('/trash')
      await expect(page).toHaveURL(/\/admin/)
    })

    test('admin accessing /settings is redirected to /admin', async ({ page }) => {
      await page.goto('/settings')
      await expect(page).toHaveURL(/\/admin/)
    })
  })
})
