import type { Locator, Page } from '@playwright/test'
import { test, expect, validateFolderTreeEnvironment } from './folder-tree-fixtures'

validateFolderTreeEnvironment()

test.describe.configure({ mode: 'serial' })
test.use({ trace: 'off', screenshot: 'off', video: 'off' })
test.setTimeout(90_000)

function fileRow(page: Page, name: string): Locator {
  return page.getByRole('region', { name: /^当前文件夹 / }).getByRole('row').filter({
    has: page.getByText(name, { exact: true }),
  })
}

function folderTree(page: Page): Locator {
  return page.getByRole('complementary', { name: '文件夹树' })
}

function treeNode(page: Page, name: string): Locator {
  return folderTree(page).getByTitle(name, { exact: true })
}

async function expectFolderState(page: Page, name: string, expectedRows: readonly string[]): Promise<void> {
  await expect(page.getByRole('region', { name: `当前文件夹 ${name}` })).toBeVisible()
  await expect(page.getByRole('navigation', { name: '文件夹路径' }).getByText(name, { exact: true })).toBeVisible()
  await expect(treeNode(page, name)).toHaveAttribute('aria-current', 'page')
  for (const rowName of expectedRows) {
    await expect(fileRow(page, rowName)).toHaveCount(1)
  }
}

async function openFolderFromList(page: Page, name: string): Promise<void> {
  const row = fileRow(page, name)
  await expect(row).toHaveCount(1)
  await row.getByText(name, { exact: true }).click()
}

async function selectFolderRow(page: Page, name: string): Promise<void> {
  const row = fileRow(page, name)
  await expect(row).toHaveCount(1)
  const checkbox = row.getByRole('checkbox')
  // Element Plus positions the native input off-screen; preserve an existing selection.
  if (!await checkbox.isChecked()) {
    await checkbox.evaluate((element: HTMLInputElement) => element.click())
  }
  await expect(checkbox).toBeChecked()
}

test('keeps folder navigation and hierarchy mutations synchronized', async ({ page, folderTreeRun }) => {
  const run = folderTreeRun
  await page.goto(`/drive?folderId=${run.root.id}`)

  await expect(page).toHaveURL(new RegExp(`folderId=${run.root.id}`))
  await expectFolderState(page, run.root.name, [run.source.name, run.target.name])

  await openFolderFromList(page, run.source.name)
  await expectFolderState(page, run.source.name, [run.marker.name])

  await treeNode(page, run.root.name).click()
  await expectFolderState(page, run.root.name, [run.source.name, run.target.name])

  await treeNode(page, run.source.name).click()
  await expect(page).toHaveURL(new RegExp(`folderId=${run.source.id}`))
  await expectFolderState(page, run.source.name, [run.marker.name])

  await page.getByRole('navigation', { name: '文件夹路径' }).getByText(run.root.name, { exact: true }).click()
  await expectFolderState(page, run.root.name, [run.source.name, run.target.name])

  await openFolderFromList(page, run.source.name)
  await expectFolderState(page, run.source.name, [run.marker.name])
  await page.getByRole('button', { name: '返回上一级' }).click()
  await expectFolderState(page, run.root.name, [run.source.name, run.target.name])

  await treeNode(page, run.source.name).click()
  await expectFolderState(page, run.source.name, [run.marker.name])

  await page.route('**/api/folder/tree**', async (route) => {
    await route.fulfill({
      status: 503,
      contentType: 'application/json',
      body: JSON.stringify({ code: 59001, message: 'folder tree refresh unavailable', data: null }),
    })
  })

  await page.getByRole('button', { name: '新建文件夹' }).click()
  const createDialog = page.getByRole('dialog', { name: '新建文件夹' })
  await createDialog.getByPlaceholder('请输入文件夹名称').fill(run.createdName)
  const treeRefreshFailure = page.waitForResponse((response) => {
    return new URL(response.url()).pathname === '/api/folder/tree' && response.status() === 503
  })
  await createDialog.getByRole('button', { name: '创建' }).click()
  await expect(page.getByText(`文件夹「${run.createdName}」创建成功`, { exact: true })).toBeVisible()
  await treeRefreshFailure
  const created = await run.api.requireFolder(run.source.id, run.createdName)
  await page.unroute('**/api/folder/tree**')

  await expectFolderState(page, run.source.name, [run.marker.name, run.createdName])
  await expect(treeNode(page, run.marker.name)).toBeVisible()
  await expect(treeNode(page, run.createdName)).toHaveCount(0)
  const refreshError = folderTree(page).getByRole('alert')
  await expect(refreshError).toContainText('folder tree refresh unavailable')
  await refreshError.getByRole('button', { name: '重试' }).click()
  await expect(refreshError).toHaveCount(0)
  await expect(treeNode(page, run.createdName)).toBeVisible()

  await selectFolderRow(page, run.createdName)
  await page.getByRole('button', { name: '重命名' }).click()
  const renameDialog = page.getByRole('dialog', { name: '重命名' })
  await renameDialog.getByPlaceholder('请输入新名称').fill(run.renamedName)
  const renameTreeRefresh = page.waitForResponse((response) => {
    return new URL(response.url()).pathname === '/api/folder/tree' && response.ok()
  })
  await renameDialog.getByRole('button', { name: '确认' }).click()
  await expect(page.getByText('重命名成功', { exact: true })).toBeVisible()
  await renameTreeRefresh
  await expect(fileRow(page, run.createdName)).toHaveCount(0)
  await expect(fileRow(page, run.renamedName)).toHaveCount(1)
  await expect(treeNode(page, run.createdName)).toHaveCount(0)
  await expect(treeNode(page, run.renamedName)).toBeVisible()

  await selectFolderRow(page, run.renamedName)
  await page.getByRole('button', { name: '移动' }).click()
  const moveDialog = page.getByRole('dialog', { name: '移动到' })
  await moveDialog.getByText(run.target.name, { exact: true }).click()
  const moveTreeRefresh = page.waitForResponse((response) => {
    return new URL(response.url()).pathname === '/api/folder/tree' && response.ok()
  })
  await moveDialog.getByRole('button', { name: '确认' }).click()
  await expect(page.getByText('已移动 1 项', { exact: true })).toBeVisible()
  await moveTreeRefresh
  await expect(fileRow(page, run.renamedName)).toHaveCount(0)

  await treeNode(page, run.target.name).click()
  await expectFolderState(page, run.target.name, [run.renamedName])
  await expect(treeNode(page, run.renamedName)).toBeVisible()

  await selectFolderRow(page, run.renamedName)
  await page.getByRole('button', { name: '删除' }).click()
  const deletePrompt = page.getByRole('dialog', { name: '确认删除' })
  await expect(deletePrompt).toContainText(run.renamedName)
  const deleteTreeRefresh = page.waitForResponse((response) => {
    return new URL(response.url()).pathname === '/api/folder/tree' && response.ok()
  })
  await deletePrompt.getByRole('button', { name: '删除' }).click()
  await expect(page.getByText('已删除 1 项', { exact: true })).toBeVisible()
  await deleteTreeRefresh
  await expect(fileRow(page, run.renamedName)).toHaveCount(0)
  await expect(treeNode(page, run.renamedName)).toHaveCount(0)

  run.api.trackFolder(created.id)
})
