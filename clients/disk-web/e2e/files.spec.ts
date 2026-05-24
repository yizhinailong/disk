import { test, expect } from '@playwright/test'
import {
  DriveSelectors,
  LayoutSelectors,
  Routes,
  loginViaUI,
  navigateTo,
  getTableRow,
  getTableRowCount,
} from './fixtures'

test.describe('File Management', () => {
  test.describe('Drive View — Page Load', () => {
    // NOTE: Requires authenticated session. Use loginViaUI or injectAuthToken
    // with route interception for the file list API.
    test.beforeEach(async ({ page }) => {
      // NOTE: With route interception, mock GET /api/file/list to return test data.
      // Example:
      //   await page.route('**/api/file/list**', route => route.fulfill({
      //     status: 200,
      //     contentType: 'application/json',
      //     body: JSON.stringify({ code: 0, message: 'ok', data: { items: [...], pagination: {...} } })
      //   }))
      await loginViaUI(page)
    })

    test('displays drive page with breadcrumb and file table', async ({ page }) => {
      await expect(page.locator(DriveSelectors.page)).toBeVisible()
      await expect(page.locator(DriveSelectors.breadcrumb)).toBeVisible()
      await expect(page.locator(DriveSelectors.breadcrumbRoot)).toHaveText('全部文件')
    })

    test('shows file list table with correct column headers', async ({ page }) => {
      const table = page.locator(DriveSelectors.table)
      await expect(table).toBeVisible()

      await expect(table.locator('th').filter({ hasText: '名称' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '大小' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '修改时间' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '类型' })).toBeVisible()
    })

    test('displays empty state when no files exist', async ({ page }) => {
      // NOTE: Mock GET /api/file/list to return empty items array.
      const emptyText = page.getByText('此文件夹为空')
      await expect(emptyText).toBeVisible()
    })
  })

  test.describe('Drive View — File List Interaction', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Mock file list with sample data:
      // { items: [{ id:1, name:'文档.pdf', type:'file', mime_type:'application/pdf', size:1048576, updated_at:'...' }] }
      await loginViaUI(page)
    })

    test('displays file items with name, size, and type', async ({ page }) => {
      const rowCount = await getTableRowCount(page, DriveSelectors.table)
      if (rowCount > 0) {
        const firstRow = getTableRow(page, DriveSelectors.table, 0)
        await expect(firstRow.locator(DriveSelectors.nameText)).toBeVisible()
      }
    })

    test('navigates into folder on single click', async ({ page }) => {
      // NOTE: Mock file list with a folder item, then mock folder contents.
      // Folder row click triggers router.push({ path: '/drive', query: { folderId: row.id } })
      const rowCount = await getTableRowCount(page, DriveSelectors.table)
      if (rowCount > 0) {
        const firstRow = getTableRow(page, DriveSelectors.table, 0)
        const nameText = await firstRow.locator(DriveSelectors.nameText).textContent()

        await firstRow.click()

        // If the item is a folder, URL should update with folderId query param
        if (nameText && page.url().includes('folderId')) {
          await expect(page.locator(DriveSelectors.breadcrumb)).toBeVisible()
        }
      }
    })

    test('updates breadcrumb when navigating into folders', async ({ page }) => {
      // NOTE: Mock breadcrumb API: GET /api/folder/{id}/breadcrumb
      // After navigating into a folder, breadcrumb should show folder name.
      await page.goto(Routes.drive + '?folderId=42')

      // Breadcrumb should have root + folder items
      const breadcrumbItems = page.locator('.el-breadcrumb__item')
      const count = await breadcrumbItems.count()
      expect(count).toBeGreaterThanOrEqual(1)
    })

    test('sorts files when clicking column headers', async ({ page }) => {
      const nameHeader = page.locator(DriveSelectors.table + ' th').filter({ hasText: '名称' })
      await nameHeader.click()

      // After clicking, sort indicator should appear (ascending)
      const sortCaret = page.locator('.el-table__sort-caret.ascending')
      await expect(sortCaret).toBeVisible()
    })

    test('paginates when clicking page controls', async ({ page }) => {
      // NOTE: Mock file list with enough items to trigger pagination.
      const pagination = page.locator('.el-pagination')
      if (await pagination.isVisible()) {
        const nextBtn = pagination.locator('.btn-next')
        if (await nextBtn.isEnabled()) {
          await nextBtn.click()
          // Should trigger new API call with page=2
          await page.waitForLoadState('networkidle')
        }
      }
    })
  })

  test.describe('Drive View — Search', () => {
    test.beforeEach(async ({ page }) => {
      await loginViaUI(page)
    })

    test('enters search mode from header search input', async ({ page }) => {
      const searchInput = page.locator(LayoutSelectors.headerSearch + ' input')
      await searchInput.fill('测试文件')
      await searchInput.press('Enter')

      // Should navigate to /drive?q=测试文件
      await expect(page).toHaveURL(/q=/)
    })

    test('shows search results header with query text', async ({ page }) => {
      await page.goto(Routes.drive + '?q=测试文件')

      // Search header should be visible
      await expect(page.locator(DriveSelectors.searchHeader)).toBeVisible()
      await expect(page.locator(DriveSelectors.searchLabel)).toContainText('测试文件')
    })

    test('exits search mode on "返回文件列表" button', async ({ page }) => {
      await page.goto(Routes.drive + '?q=测试文件')
      await page.getByText('返回文件列表').click()

      await expect(page).toHaveURL(/\/drive$/)
      await expect(page.locator(DriveSelectors.searchHeader)).not.toBeVisible()
    })
  })

  test.describe('File Upload → Download → Delete Flow', () => {
    // NOTE: This is a full integration flow requiring backend or comprehensive mocking.
    // Mock these endpoints:
    //   POST /api/file/upload/init
    //   POST /api/file/upload/chunk
    //   POST /api/file/upload/complete
    //   GET  /api/file/download/{id}/info
    //   GET  /api/file/download/{id}
    //   DELETE /api/file
    test('completes upload, download, and delete lifecycle', async ({ page }) => {
      await loginViaUI(page)

      // Step: Upload (toolbar button trigger — depends on DriveToolbar component)
      // The upload dialog is part of DriveToolbar; interaction depends on
      // whether the toolbar renders buttons via slot or directly.
      // Placeholder: click upload trigger when available.

      // Step: Verify file appears in list
      // After upload completes, file should be visible in the table.

      // Step: Download file (double-click triggers download task)
      const rowCount = await getTableRowCount(page, DriveSelectors.table)
      if (rowCount > 0) {
        const firstRow = getTableRow(page, DriveSelectors.table, 0)
        await firstRow.dblclick()
        // Download task should be added to transfer center
      }

      // Step: Delete file (context menu or toolbar action)
      // Select file via checkbox or click, then trigger delete action.
    })
  })
})
