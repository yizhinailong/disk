/**
 * Shared E2E test fixtures, selectors, and helpers.
 *
 * Tests use existing CSS classes and visible text as selectors.
 * No source files are modified to add data-testid attributes.
 *
 * NOTE: These E2E tests assume the backend is running at localhost:8080.
 * For CI or isolated runs, API mocking via Playwright route interception
 * should be added at the describe/block level.
 */
import { type Page, type Locator, expect } from '@playwright/test'

// ==================== Test Credentials ====================
// TODO: Replace with seeded test data or environment variables in CI.
export const TEST_USER = {
  username: 'e2e_tester',
  password: 'TestPass123!',
  email: 'e2e@tester.local',
}

export const TEST_ADMIN = {
  username: 'e2e_admin',
  password: 'AdminPass123!',
}

// ==================== Route Paths ====================
export const Routes = {
  login: '/login',
  register: '/register',
  drive: '/drive',
  shares: '/shares',
  trash: '/trash',
  transfers: '/transfers',
  settings: '/settings',
  shareVerify: (shareId: string) => `/s/${shareId}/verify`,
  shareBrowse: (shareId: string) => `/s/${shareId}/browse`,
  adminUsers: '/admin/users',
  adminShares: '/admin/shares',
  adminLogs: '/admin/logs',
  adminSystem: '/admin/system',
} as const

// ==================== Selectors (CSS class based) ====================

/** Auth page selectors (shared by login and register) */
export const AuthSelectors = {
  page: '.auth-page',
  card: '.auth-card',
  title: '.auth-title',
  submitButton: '.auth-submit',
  footerLink: '.auth-footer a',
}

/** Owner layout selectors */
export const LayoutSelectors = {
  sidebar: '.owner-aside',
  menu: '.aside-menu',
  headerSearch: '.header-search',
  headerUser: '.header-user',
  logoText: '.logo-text',
}

/** Admin layout selectors */
export const AdminSelectors = {
  sidebar: '.admin-aside',
  header: '.admin-header',
  headerTitle: '.header-title',
  adminTag: '.admin-tag',
}

/** Drive view selectors */
export const DriveSelectors = {
  page: '.drive-page',
  breadcrumb: '.drive-page__breadcrumb',
  breadcrumbRoot: '.drive-page__breadcrumb-root',
  table: '.drive-page__table',
  nameCell: '.drive-page__name-cell',
  nameText: '.drive-page__name-text',
  searchHeader: '.drive-page__search-header',
  searchLabel: '.drive-page__search-label',
}

/** Trash view selectors */
export const TrashSelectors = {
  page: '.trash-page',
  title: '.trash-page__title',
  table: '.trash-page__table',
}

/** Shares view selectors */
export const SharesSelectors = {
  page: '.shares-page',
  title: '.shares-page__title',
  filters: '.shares-page__filters',
  table: '.shares-page__table',
}

/** Share verify page selectors */
export const ShareVerifySelectors = {
  page: '.share-verify-page',
  card: '.share-verify-card',
  title: '.share-verify__title',
  submitButton: '.share-verify__submit',
  info: '.share-verify__info',
  error: '.share-verify__error',
}

/** Share browse page selectors */
export const ShareBrowseSelectors = {
  page: '.share-browse-page',
  title: '.share-browse__title',
  table: '.share-browse__table',
  breadcrumb: '.share-browse__breadcrumb',
}

/** Admin users view selectors */
export const AdminUsersSelectors = {
  page: '.admin-users-page',
  search: '.admin-users-page__search',
  table: '.admin-users-page__table',
}

/** Admin logs view selectors */
export const AdminLogsSelectors = {
  page: '.admin-logs-page',
  title: '.admin-logs-page__title',
  filters: '.admin-logs-page__filters',
  table: '.admin-logs-page__table',
}

/** Admin system view selectors */
export const AdminSystemSelectors = {
  page: '.admin-system-page',
  pageTitle: '.page-title',
  statCard: '.stat-card',
  statusCard: '.status-card',
  miniStat: '.mini-stat',
}

// ==================== Helper Functions ====================

/**
 * Log in via UI (fills the login form and submits).
 * Requires the backend to be available, or route interception to be set up.
 */
export async function loginViaUI(
  page: Page,
  username: string = TEST_USER.username,
  password: string = TEST_USER.password,
): Promise<void> {
  await page.goto(Routes.login)
  await page.locator('input[placeholder="请输入用户名"]').fill(username)
  await page.locator('input[placeholder="请输入密码"]').fill(password)
  await page.locator(AuthSelectors.submitButton).click()
  // Wait for navigation to drive (or redirect target)
  await page.waitForURL('**/drive**', { timeout: 15_000 })
}

/**
 * Log out by clicking the user dropdown then the logout option.
 */
export async function logoutViaUI(page: Page): Promise<void> {
  await page.locator(LayoutSelectors.headerUser).click()
  await page.getByRole('menuitem', { name: '退出登录' }).click()
  await page.waitForURL('**/login**', { timeout: 10_000 })
}

/**
 * Inject an auth token directly into localStorage to bypass login API.
 * Useful for tests that focus on UI behavior rather than auth flow.
 *
 * @param page - Playwright page instance
 * @param token - JWT access token string
 */
export async function injectAuthToken(page: Page, token: string): Promise<void> {
  await page.evaluate((t) => {
    localStorage.setItem('access_token', t)
    localStorage.setItem('refresh_token', 'mock-refresh-token')
  }, token)
}

/**
 * Navigate to a route and wait for the page to be visible.
 */
export async function navigateTo(page: Page, path: string): Promise<void> {
  await page.goto(path)
  await page.waitForLoadState('networkidle')
}

/**
 * Get Element Plus el-table row locator by row index.
 */
export function getTableRow(page: Page, tableSelector: string, rowIndex: number): Locator {
  return page.locator(`${tableSelector} .el-table__body-wrapper .el-table__row`).nth(rowIndex)
}

/**
 * Get the count of el-table rows.
 */
export function getTableRowCount(page: Page, tableSelector: string): Promise<number> {
  return page.locator(`${tableSelector} .el-table__body-wrapper .el-table__row`).count()
}

/**
 * Wait for an Element Plus dialog to be visible.
 */
export async function waitForDialog(page: Page, title: string): Promise<void> {
  const dialog = page.locator('.el-dialog').filter({ hasText: title })
  await expect(dialog).toBeVisible()
}

/**
 * Click confirm button in Element Plus MessageBox.
 */
export async function confirmMessageBox(page: Page): Promise<void> {
  await page.locator('.el-message-box__btns .el-button--primary').click()
}

/**
 * Click cancel button in Element Plus MessageBox.
 */
export async function cancelMessageBox(page: Page): Promise<void> {
  await page.locator('.el-message-box__btns .el-button--default').click()
}

// Re-export expect for convenience
export { expect }
