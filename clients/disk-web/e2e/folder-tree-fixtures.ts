import { randomUUID } from 'node:crypto'
import { test as base } from '@playwright/test'
import { getTestUserCredentials } from './fixtures'

interface ApiEnvelope<T> {
  readonly code: number
  readonly message: string
  readonly data: T
}

interface AuthenticationData {
  readonly access_token: string
  readonly refresh_token: string
}

export interface FolderRecord {
  readonly id: number
  readonly name: string
  readonly parent_id: number
}

interface FileListItem {
  readonly id: number
  readonly name: string
  readonly type: 'file' | 'folder'
}

interface FileListData {
  readonly items: readonly FileListItem[]
}

interface TrashItem {
  readonly id: number
  readonly original_id: number
  readonly name: string
}

interface TrashListData {
  readonly items: readonly TrashItem[]
  readonly pagination: {
    readonly page: number
    readonly total_pages: number
  }
}

interface FolderTreeData {
  readonly id: number
  readonly name: string
  readonly children: readonly FolderTreeData[]
}

function requiredEnvironment(name: string): string {
  const value = process.env[name]?.trim()
  if (!value) {
    throw new Error(`Missing required Playwright configuration: ${name}`)
  }
  return value
}

export function validateFolderTreeEnvironment(): void {
  getTestUserCredentials()
  if (process.env.DISK_E2E_SEED_USER === '1') {
    requiredEnvironment('DISK_E2E_USER_EMAIL')
  }
}

export class DiskBackendFixture {
  private readonly baseUrl: string
  private accessToken = ''
  private refreshToken = ''
  private readonly ownedFolderIds = new Set<number>()

  constructor() {
    this.baseUrl = (process.env.DISK_E2E_API_URL ?? 'http://127.0.0.1:8080/api').replace(/\/$/, '')
  }

  async authenticate(): Promise<void> {
    const credentials = getTestUserCredentials()
    try {
      await this.login(credentials.username, credentials.password)
    } catch (error) {
      if (process.env.DISK_E2E_SEED_USER !== '1') throw error
      await this.request('/auth/register', {
        method: 'POST',
        body: JSON.stringify({
          username: credentials.username,
          password: credentials.password,
          email: requiredEnvironment('DISK_E2E_USER_EMAIL'),
        }),
      })
      await this.login(credentials.username, credentials.password)
    }
  }

  authenticationState(): { accessToken: string; refreshToken: string } {
    return {
      accessToken: this.accessToken,
      refreshToken: this.refreshToken,
    }
  }

  trackFolder(folderId: number): void {
    this.ownedFolderIds.add(folderId)
  }

  async createFolder(name: string, parentId: number): Promise<FolderRecord> {
    const folder = await this.request<FolderRecord>('/folder/create', {
      method: 'POST',
      body: JSON.stringify({ name, parent_id: parentId }),
    }, true)
    this.trackFolder(folder.id)
    return folder
  }

  async requireFolder(parentId: number, name: string): Promise<FolderRecord> {
    const matches = (await this.listFolder(parentId))
      .filter((item) => item.type === 'folder' && item.name === name)
    if (matches.length !== 1) {
      throw new Error('Owned E2E folder was not present exactly once')
    }
    const folder = { id: matches[0]!.id, name, parent_id: parentId }
    this.trackFolder(folder.id)
    return folder
  }

  async cleanup(root: FolderRecord): Promise<void> {
    try {
      await this.request('/file', {
        method: 'DELETE',
        body: JSON.stringify({ folder_ids: [root.id] }),
      }, true)

      const ownedTrash = (await this.listAllTrash())
        .filter((item) => this.ownedFolderIds.has(item.original_id))
      if (ownedTrash.length > 0) {
        await this.request('/trash', {
          method: 'DELETE',
          body: JSON.stringify({ trash_ids: ownedTrash.map((item) => item.id) }),
        }, true)
      }

      const tree = await this.request<FolderTreeData>('/folder/tree', undefined, true)
      const remainingRoot = tree.children.some((item) => item.id === root.id)
      const remainingTrash = (await this.listAllTrash())
        .some((item) => this.ownedFolderIds.has(item.original_id))
      if (remainingRoot || remainingTrash) {
        throw new Error('Owned E2E resources remain after teardown')
      }
    } catch {
      throw new Error('Folder-tree E2E teardown could not remove its owned resources')
    }
  }

  private async login(username: string, password: string): Promise<void> {
    const result = await this.request<AuthenticationData>('/auth/login', {
      method: 'POST',
      body: JSON.stringify({ account: username, password }),
    })
    this.accessToken = result.access_token
    this.refreshToken = result.refresh_token
  }

  private async listFolder(parentId: number): Promise<readonly FileListItem[]> {
    const parentQuery = parentId === 0 ? '' : `&parent_id=${parentId}`
    const result = await this.request<FileListData>(
      `/file/list?page=1&page_size=100&sort_by=updated_at&sort_order=desc${parentQuery}`,
      undefined,
      true,
    )
    return result.items
  }

  private async listAllTrash(): Promise<readonly TrashItem[]> {
    const items: TrashItem[] = []
    let page = 1
    let totalPages = 1
    do {
      const result = await this.request<TrashListData>(
        `/trash?page=${page}&page_size=100`,
        undefined,
        true,
      )
      items.push(...result.items)
      totalPages = result.pagination.total_pages
      page += 1
    } while (page <= totalPages)
    return items
  }

  private async request<T>(path: string, init?: RequestInit, authenticated = false): Promise<T> {
    const headers = new Headers(init?.headers)
    headers.set('Content-Type', 'application/json')
    if (authenticated) {
      headers.set('Authorization', `Bearer ${this.accessToken}`)
    }

    const response = await fetch(`${this.baseUrl}${path}`, { ...init, headers })
    let envelope: ApiEnvelope<T> | null = null
    try {
      envelope = await response.json() as ApiEnvelope<T>
    } catch {
      // The sanitized error below deliberately excludes response bodies and headers.
    }
    if (!response.ok || envelope?.code !== 0) {
      const code = envelope?.code ?? 'invalid-response'
      throw new Error(`Backend fixture request failed (HTTP ${response.status}, code ${code})`)
    }
    return envelope.data
  }
}

export interface FolderTreeRun {
  readonly namespace: string
  readonly root: FolderRecord
  readonly source: FolderRecord
  readonly target: FolderRecord
  readonly marker: FolderRecord
  readonly createdName: string
  readonly renamedName: string
  readonly api: DiskBackendFixture
}

interface FolderTreeFixtures {
  readonly folderTreeRun: FolderTreeRun
}

export const test = base.extend<FolderTreeFixtures>({
  folderTreeRun: async ({ page }, use, testInfo) => {
    validateFolderTreeEnvironment()
    const api = new DiskBackendFixture()
    await api.authenticate()

    const unique = [
      Date.now().toString(36),
      testInfo.workerIndex,
      testInfo.repeatEachIndex,
      randomUUID().slice(0, 8),
    ].join('-')
    const namespace = `pw-folder-tree-${unique}`
    const root = await api.createFolder(namespace, 0)

    try {
      const source = await api.createFolder(`${namespace}-source`, root.id)
      const target = await api.createFolder(`${namespace}-target`, root.id)
      const marker = await api.createFolder(`${namespace}-marker`, source.id)
      const authentication = api.authenticationState()
      await page.addInitScript(({ accessToken, refreshToken }) => {
        localStorage.setItem('access_token', accessToken)
        localStorage.setItem('refresh_token', refreshToken)
      }, authentication)

      await use({
        namespace,
        root,
        source,
        target,
        marker,
        createdName: `${namespace}-created`,
        renamedName: `${namespace}-renamed`,
        api,
      })
    } finally {
      await api.cleanup(root)
    }
  },
})

export { expect } from '@playwright/test'
