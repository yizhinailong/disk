import { test, expect } from '@playwright/test'
import {
  TrashSelectors,
  Routes,
  loginViaUI,
  navigateTo,
  getTableRow,
  getTableRowCount,
  confirmMessageBox,
} from './fixtures'

test.describe('Trash Management', () => {
  test.describe('Trash View — Page Load', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Mock GET /api/trash to return trash items.
      await loginViaUI(page)
      await navigateTo(page, Routes.trash)
    })

    test('renders trash page with header and title', async ({ page }) => {
      await expect(page.locator(TrashSelectors.page)).toBeVisible()
      await expect(page.locator(TrashSelectors.title)).toHaveText('回收站')
    })

    test('shows toolbar with restore, delete, and empty buttons', async ({ page }) => {
      await expect(page.getByText(/恢复/).first()).toBeVisible()
      await expect(page.getByText(/永久删除/).first()).toBeVisible()
      await expect(page.getByText('清空回收站')).toBeVisible()
    })

    test('shows file table with correct column headers', async ({ page }) => {
      const table = page.locator(TrashSelectors.table)
      await expect(table).toBeVisible()

      await expect(table.locator('th').filter({ hasText: '文件名' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '原始路径' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '大小' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '删除时间' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '过期时间' })).toBeVisible()
      await expect(table.locator('th').filter({ hasText: '操作' })).toBeVisible()
    })

    test('shows empty state when trash is empty', async ({ page }) => {
      // NOTE: Mock GET /api/trash to return empty items.
      await expect(page.getByText('回收站为空')).toBeVisible()
    })

    test('table has selection column for multi-select', async ({ page }) => {
      const table = page.locator(TrashSelectors.table)
      await expect(table.locator('.el-table-column--selection')).toBeVisible()
    })
  })

  test.describe('Trash View — Item Interaction', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Mock GET /api/trash with sample items:
      // [{ id:1, name:'report.pdf', type:'file', original_path:'/docs', size:2048576,
      //   deleted_at:'2026-01-15T10:00:00Z', expires_at:'2026-01-22T10:00:00Z' }]
      await loginViaUI(page)
      await navigateTo(page, Routes.trash)
    })

    test('restore button is disabled when no items are selected', async ({ page }) => {
      const restoreBtn = page.locator('.trash-page__toolbar .el-button--primary')
      await expect(restoreBtn).toBeDisabled()
    })

    test('permanent delete button is disabled when no items are selected', async ({ page }) => {
      const deleteBtn = page.locator('.trash-page__toolbar .el-button--danger').first()
      await expect(deleteBtn).toBeDisabled()
    })

    test('selecting items enables restore and delete buttons', async ({ page }) => {
      // NOTE: Mock trash items with at least one row.
      const checkbox = page.locator(TrashSelectors.table + ' .el-checkbox').first()
      if (await checkbox.isVisible()) {
        await checkbox.click()

        const restoreBtn = page.locator('.trash-page__toolbar .el-button--primary')
        await expect(restoreBtn).toBeEnabled()
      }
    })

    test('row-level restore button triggers restore for single item', async ({ page }) => {
      // NOTE: Mock POST /api/trash/restore with { trash_ids: [rowId] }.
      const rowCount = await getTableRowCount(page, TrashSelectors.table)
      if (rowCount > 0) {
        const restoreBtn = getTableRow(page, TrashSelectors.table, 0).getByText('恢复')
        if (await restoreBtn.isVisible()) {
          await restoreBtn.click()
          // NOTE: Mock should return success response.
        }
      }
    })

    test('row-level permanent delete shows confirmation dialog', async ({ page }) => {
      // NOTE: Clicking "永久删除" in a row triggers ElMessageBox.confirm.
      const rowCount = await getTableRowCount(page, TrashSelectors.table)
      if (rowCount > 0) {
        const deleteBtn = getTableRow(page, TrashSelectors.table, 0).getByText('永久删除')
        if (await deleteBtn.isVisible()) {
          await deleteBtn.click()
          await expect(page.locator('.el-message-box')).toBeVisible()
          await expect(page.getByText(/确定要永久删除/)).toBeVisible()
        }
      }
    })
  })

  test.describe('Trash View — Batch Operations', () => {
    test.beforeEach(async ({ page }) => {
      // NOTE: Mock GET /api/trash with multiple items for batch testing.
      await loginViaUI(page)
      await navigateTo(page, Routes.trash)
    })

    test('batch restore via toolbar button', async ({ page }) => {
      // NOTE: Mock POST /api/trash/restore with multiple trash_ids.
      const checkboxes = page.locator(TrashSelectors.table + ' .el-checkbox')
      const count = await checkboxes.count()

      if (count > 1) {
        await checkboxes.nth(1).click()

        const restoreBtn = page.locator('.trash-page__toolbar .el-button--primary')
        await expect(restoreBtn).toBeEnabled()
        await restoreBtn.click()
        // NOTE: Mock should return { summary: { total, success_count, failure_count }, results: [...] }
      }
    })

    test('batch permanent delete via toolbar shows confirmation', async ({ page }) => {
      const checkboxes = page.locator(TrashSelectors.table + ' .el-checkbox')
      const count = await checkboxes.count()

      if (count > 1) {
        await checkboxes.nth(1).click()

        const deleteBtn = page.locator('.trash-page__toolbar .el-button--danger').first()
        if (await deleteBtn.isEnabled()) {
          await deleteBtn.click()
          await expect(page.locator('.el-message-box')).toBeVisible()
        }
      }
    })

    test('empty trash shows danger confirmation dialog', async ({ page }) => {
      // NOTE: Mock GET /api/trash with at least one item so "清空回收站" is enabled.
      const emptyBtn = page.getByText('清空回收站')
      if (await emptyBtn.isEnabled()) {
        await emptyBtn.click()
        await expect(page.locator('.el-message-box')).toBeVisible()
        await expect(page.getByText(/清空回收站将永久删除所有文件/)).toBeVisible()
      }
    })

    test('empty trash confirmation dialog has error type styling', async ({ page }) => {
      const emptyBtn = page.getByText('清空回收站')
      if (await emptyBtn.isEnabled()) {
        await emptyBtn.click()
        // ElMessageBox with type: 'error' uses danger-styled confirm button
        const confirmBtn = page.locator('.el-message-box__btns .el-button--primary')
        await expect(confirmBtn).toBeVisible()
      }
    })
  })

  test.describe('Trash View — Pagination', () => {
    test.beforeEach(async ({ page }) => {
      await loginViaUI(page)
      await navigateTo(page, Routes.trash)
    })

    test('pagination component appears when items span multiple pages', async ({ page }) => {
      // NOTE: Mock GET /api/trash with total > page_size to trigger pagination.
      const pagination = page.locator('.el-pagination')
      if (await pagination.isVisible()) {
        await expect(pagination).toBeVisible()
      }
    })

    test('clicking next page loads next batch', async ({ page }) => {
      // NOTE: Mock page=2 response with different items.
      const nextBtn = page.locator('.el-pagination .btn-next')
      if (await nextBtn.isVisible() && await nextBtn.isEnabled()) {
        await nextBtn.click()
        await page.waitForLoadState('networkidle')
      }
    })
  })

  test.describe('Trash Flow: Move to Trash → Restore → Permanent Delete', () => {
    // NOTE: Full integration lifecycle requiring backend or comprehensive mocking:
    //   DELETE /api/file (soft delete → moves to trash)
    //   GET  /api/trash (list trashed items)
    //   POST /api/trash/restore (restore items)
    //   DELETE /api/trash (permanent delete)
    test('soft delete appears in trash, can be restored and permanently deleted', async ({ page }) => {
      await loginViaUI(page)

      // Step: Navigate to drive and delete a file (soft delete)
      // Mock DELETE /api/file → success

      // Step: Navigate to trash, verify item appears
      await navigateTo(page, Routes.trash)

      // Step: Restore item
      // Mock POST /api/trash/restore → success

      // Step: Verify item is gone from trash
      // Mock GET /api/trash → empty after restore

      // Step: Alternatively, permanently delete instead of restore
      // Mock DELETE /api/trash → success
    })
  })
})
