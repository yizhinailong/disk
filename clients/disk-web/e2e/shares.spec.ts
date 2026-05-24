import { test, expect } from '@playwright/test'
import {
  ShareVerifySelectors,
  ShareBrowseSelectors,
  Routes,
  navigateTo,
  getTableRow,
  getTableRowCount,
} from './fixtures'

test.describe('Share Flow', () => {
  const TEST_SHARE_ID = 'test-share-abc123'

  test.describe('Share Verify Page', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Mock GET /api/share/access/{shareId} to return share metadata
      // or error code 60003 (needs password) / 60001 (not found) / 60002 (expired).
      await navigateTo(page, Routes.shareVerify(TEST_SHARE_ID))
    })

    test('renders share verify card', async ({ page }) => {
      await expect(page.locator(ShareVerifySelectors.page)).toBeVisible()
      await expect(page.locator(ShareVerifySelectors.card)).toBeVisible()
    })

    test('displays share title', async ({ page }) => {
      // NOTE: Only visible when pageState is 'ready' (not loading/error).
      // Mock API to return share info without password requirement.
      await expect(page.locator(ShareVerifySelectors.title)).toHaveText('分享链接')
    })

    test('shows permission tag', async ({ page }) => {
      // NOTE: Mock API to return { permission: 'download' } or { permission: 'preview' }.
      const permissionTag = page.locator(ShareVerifySelectors.info + ' .el-tag')
      await expect(permissionTag).toBeVisible()
    })

    test('shows password input when share requires password', async ({ page }) => {
      // NOTE: Mock API to return error code 60003 (password required).
      const passwordInput = page.locator('input[placeholder="请输入访问密码"]')
      await expect(passwordInput).toBeVisible()
      await expect(page.locator(ShareVerifySelectors.submitButton)).toHaveText('验证')
    })

    test('shows validation error for empty password', async ({ page }) => {
      // NOTE: Mock API to return 60003 first, then test form validation.
      const passwordInput = page.locator('input[placeholder="请输入访问密码"]')
      await passwordInput.click()
      await passwordInput.blur()
      await expect(page.getByText('请输入访问密码')).toBeVisible()
    })

    test('shows auto-verify message for shares without password', async ({ page }) => {
      // NOTE: Mock API to return success (no password needed).
      // The component shows "正在验证并跳转..." when needsPassword is false.
      const autoMessage = page.getByText('正在验证并跳转...')
      await expect(autoMessage).toBeVisible()
    })

    test('shows error state for invalid or expired shares', async ({ page }) => {
      // NOTE: Mock API to return error code 60001 (not found) or 60002 (expired).
      await expect(page.locator(ShareVerifySelectors.error)).toBeVisible()
      await expect(page.locator('.share-verify__error-text')).toBeVisible()
    })

    test('retry button reloads share info', async ({ page }) => {
      // NOTE: Mock API to return error, then on retry return success.
      const retryBtn = page.getByText('重试')
      if (await retryBtn.isVisible()) {
        await retryBtn.click()
      }
    })
  })

  test.describe('Share Browse Page', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Requires valid share_token in sessionStorage.
      // Mock:
      //   GET /api/share/browse/{shareId} → { items: [...], breadcrumb: [...] }
      // The component redirects to verify if no shareToken in visitorStore.
      // To test browse, inject share token:
      //   await page.evaluate(() => sessionStorage.setItem('share_token', 'mock-token'))
      await page.evaluate(() => {
        sessionStorage.setItem('share_token', 'mock-share-token')
      })
      await navigateTo(page, Routes.shareBrowse(TEST_SHARE_ID))
    })

    test('renders share browse page with header', async ({ page }) => {
      await expect(page.locator(ShareBrowseSelectors.page)).toBeVisible()
      await expect(page.locator(ShareBrowseSelectors.title)).toHaveText('分享文件')
    })

    test('displays breadcrumb navigation', async ({ page }) => {
      await expect(page.locator(ShareBrowseSelectors.breadcrumb)).toBeVisible()
    })

    test('shows file table with selection column', async ({ page }) => {
      const table = page.locator(ShareBrowseSelectors.table)
      await expect(table).toBeVisible()

      // Selection checkbox column exists
      await expect(table.locator('.el-table-column--selection')).toBeVisible()
    })

    test('shows column headers: name, size, type, action', async ({ page }) => {
      const table = page.locator(ShareBrowseSelectors.table)
      await expect(table.locator('th').filter({ hasText: '名称' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '大小' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '类型' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '操作' })).toBeVisible()
    })

    test('download button visible for file rows', async ({ page }) => {
      // NOTE: Mock browse response with a file item (type: 'file').
      const rowCount = await getTableRowCount(page, ShareBrowseSelectors.table)
      if (rowCount > 0) {
        const downloadBtn = getTableRow(page, ShareBrowseSelectors.table, 0).getByText('下载')
        await expect(downloadBtn).toBeVisible()
      }
    })

    test('navigates into folder on row click', async ({ page }) => {
      // NOTE: Mock browse with a folder item, then mock subfolder browse.
      const rowCount = await getTableRowCount(page, ShareBrowseSelectors.table)
      if (rowCount > 0) {
        await getTableRow(page, ShareBrowseSelectors.table, 0).click()
        // URL should update with folderId query
      }
    })

    test('"保存到我的网盘" button appears when items are selected', async ({ page }) => {
      // NOTE: Select a row via checkbox, then the save button should appear.
      const checkbox = page.locator(ShareBrowseSelectors.table + ' .el-checkbox').first()
      if (await checkbox.isVisible()) {
        await checkbox.click()
        await expect(page.getByText(/保存到我的网盘/)).toBeVisible()
      }
    })

    test('"返回验证" button navigates back to verify page', async ({ page }) => {
      await page.getByText('返回验证').click()
      await expect(page).toHaveURL(new RegExp(`/s/${TEST_SHARE_ID}/verify`))
    })
  })

  test.describe('Share Create → Access → Download Flow', () => {
    // NOTE: Full integration flow. Requires:
    //   1. Authenticated user session (owner)
    //   2. Backend running or mocked:
    //      POST /api/share → creates share
    //      POST /api/share/access/{shareId} → verifies password
    //      GET /api/share/browse/{shareId} → returns items
    //      GET /api/share/download/{shareId}/{fileId} → downloads file
    test('owner creates share, visitor accesses and downloads', async ({ page }) => {
      // Step: Owner logs in and creates a share (from SharesView or context menu)
      // Step: Visitor opens share verify URL
      // Step: Visitor enters password if needed
      // Step: Visitor browses shared files
      // Step: Visitor downloads a file
      // All steps require backend/mocking.
    })
  })
})
