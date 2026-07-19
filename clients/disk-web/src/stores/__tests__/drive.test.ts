import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useDriveStore } from '../drive'
import * as fileApi from '@/api/file'
import * as folderApi from '@/api/folder'
import type { FileItem, BreadcrumbItem, SearchResultItem } from '@/types'

vi.mock('@/api/file')
vi.mock('@/api/folder')

const sampleFiles: FileItem[] = [
  { id: 1, name: 'file-a.txt', type: 'file', size: 100, mime_type: 'text/plain', created_at: '2024-01-01', updated_at: '2024-01-03' },
  { id: 2, name: 'file-b.txt', type: 'file', size: 200, mime_type: 'text/plain', created_at: '2024-01-02', updated_at: '2024-01-02' },
  { id: 3, name: 'folder-c', type: 'folder', item_count: 5, created_at: '2024-01-03', updated_at: '2024-01-01' },
]

describe('useDriveStore', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  describe('initial state', () => {
    it('has empty files and root folder', () => {
      const store = useDriveStore()
      expect(store.files).toEqual([])
      expect(store.currentFolderId).toBe(0)
      expect(store.isRoot).toBe(true)
      expect(store.hasSelection).toBe(false)
      expect(store.selectedCount).toBe(0)
      expect(store.isSearching).toBe(false)
    })
  })

  describe('fetchFiles', () => {
    it('fetches files and clears selection', async () => {
      vi.mocked(fileApi.listFiles).mockResolvedValue({
        items: sampleFiles,
        pagination: { page: 1, page_size: 20, total: 3, total_pages: 1 },
      })

      const store = useDriveStore()
      store.selectedIds = new Set([1, 2])
      await store.fetchFiles()

      expect(fileApi.listFiles).toHaveBeenCalled()
      expect(store.files).toHaveLength(3)
      expect(store.loading).toBe(false)
      expect(store.selectedIds.size).toBe(0)
    })

    it('sets loading to false even on error', async () => {
      vi.mocked(fileApi.listFiles).mockRejectedValue(new Error('fail'))

      const store = useDriveStore()
      await expect(store.fetchFiles()).rejects.toThrow('fail')
      expect(store.loading).toBe(false)
    })
  })

  describe('navigateToFolder', () => {
    it('sets folder id and fetches files + breadcrumb', async () => {
      vi.mocked(fileApi.listFiles).mockResolvedValue({
        items: sampleFiles,
        pagination: { page: 1, page_size: 20, total: 3, total_pages: 1 },
      })
      const breadcrumb: BreadcrumbItem[] = [{ id: 5, name: 'SubFolder' }]
      vi.mocked(folderApi.getBreadcrumb).mockResolvedValue({ path: breadcrumb })

      const store = useDriveStore()
      await store.navigateToFolder(5)

      expect(store.currentFolderId).toBe(5)
      expect(store.isRoot).toBe(false)
      expect(store.breadcrumbs).toEqual(breadcrumb)
      expect(fileApi.listFiles).toHaveBeenCalledWith(expect.objectContaining({ parent_id: 5 }))
    })

    it('keeps list and breadcrumb consistent for tree selection, breadcrumb navigation, and drill-down', async () => {
      vi.mocked(fileApi.listFiles).mockResolvedValueOnce({
        items: [{ id: 11, name: 'design.md', type: 'file', size: 10, mime_type: 'text/markdown', created_at: '2024-01-01', updated_at: '2024-01-01' }],
        pagination: { page: 1, page_size: 20, total: 1, total_pages: 1 },
      }).mockResolvedValueOnce({
        items: [{ id: 21, name: 'nested', type: 'folder', item_count: 0, created_at: '2024-01-01', updated_at: '2024-01-02' }],
        pagination: { page: 1, page_size: 20, total: 1, total_pages: 1 },
      }).mockResolvedValueOnce({
        items: [{ id: 31, name: 'leaf.txt', type: 'file', size: 1, mime_type: 'text/plain', created_at: '2024-01-01', updated_at: '2024-01-03' }],
        pagination: { page: 1, page_size: 20, total: 1, total_pages: 1 },
      })
      vi.mocked(folderApi.getBreadcrumb).mockResolvedValueOnce({
        path: [{ id: 5, name: 'Docs' }],
      }).mockResolvedValueOnce({
        path: [{ id: 5, name: 'Docs' }, { id: 9, name: 'Specs' }],
      }).mockResolvedValueOnce({
        path: [{ id: 5, name: 'Docs' }, { id: 9, name: 'Specs' }, { id: 21, name: 'Nested' }],
      })

      const store = useDriveStore()

      await store.navigateToFolder(5)
      expect(store.currentFolderId).toBe(5)
      expect(store.files[0]?.name).toBe('design.md')
      expect(store.breadcrumbs.map((item) => item.id)).toEqual([5])

      await store.navigateToFolder(9)
      expect(store.currentFolderId).toBe(9)
      expect(store.files[0]?.id).toBe(21)
      expect(store.breadcrumbs.map((item) => item.id)).toEqual([5, 9])

      await store.navigateToFolder(21)
      expect(store.currentFolderId).toBe(21)
      expect(store.files[0]?.name).toBe('leaf.txt')
      expect(store.breadcrumbs.map((item) => item.id)).toEqual([5, 9, 21])
    })
  })

  describe('searchFiles', () => {
    it('searches and stores results', async () => {
      const results: SearchResultItem[] = [
        { id: 10, name: 'match.txt', type: 'file', size: 50, mime_type: 'text/plain', created_at: '2024-01-01', updated_at: '2024-01-01', path: '/match.txt' },
      ]
      vi.mocked(fileApi.searchFiles).mockResolvedValue({
        items: results,
        pagination: { page: 1, page_size: 20, total: 1, total_pages: 1 },
      })

      const store = useDriveStore()
      await store.searchFiles('match')

      expect(store.isSearching).toBe(true)
      expect(store.searchResults).toHaveLength(1)
      expect(store.loading).toBe(false)
    })

    it('clears search when keyword is empty', async () => {
      const store = useDriveStore()
      store.searchQuery = 'old'
      store.searchResults = [{ id: 1, name: 'a', type: 'file', size: 0, mime_type: '', created_at: '', updated_at: '', path: '/' }] as SearchResultItem[]

      await store.searchFiles('  ')

      expect(store.isSearching).toBe(false)
      expect(store.searchResults).toEqual([])
    })
  })

  describe('clearSearch', () => {
    it('clears search state', () => {
      const store = useDriveStore()
      store.searchQuery = 'test'
      store.searchResults = [] as SearchResultItem[]
      store.clearSearch()
      expect(store.searchQuery).toBe('')
      expect(store.searchResults).toEqual([])
    })
  })

  describe('sortedFiles', () => {
    it('sorts files by updated_at desc by default', () => {
      const store = useDriveStore()
      store.files = [...sampleFiles]

      const sorted = store.sortedFiles
      expect(sorted[0]!.name).toBe('file-a.txt')
      expect(sorted[1]!.name).toBe('file-b.txt')
    })

    it('sorts files by name asc', () => {
      const store = useDriveStore()
      store.files = [...sampleFiles]
      store.setSortBy('name')
      store.setSortOrder('asc')

      const sorted = store.sortedFiles
      expect(sorted[0]!.name).toBe('file-a.txt')
      expect(sorted[1]!.name).toBe('file-b.txt')
      expect(sorted[2]!.name).toBe('folder-c')
    })
  })

  describe('selection', () => {
    it('toggleSelect adds and removes ids', () => {
      const store = useDriveStore()
      store.toggleSelect(1)
      expect(store.hasSelection).toBe(true)
      expect(store.selectedCount).toBe(1)

      store.toggleSelect(1)
      expect(store.hasSelection).toBe(false)
    })

    it('selectAll selects all files', () => {
      const store = useDriveStore()
      store.files = sampleFiles
      store.selectAll()
      expect(store.selectedCount).toBe(3)
    })

    it('clearSelection removes all selections', () => {
      const store = useDriveStore()
      store.files = sampleFiles
      store.selectAll()
      store.clearSelection()
      expect(store.selectedCount).toBe(0)
    })

    it('setSelection replaces the current table selection', () => {
      const store = useDriveStore()
      store.selectedIds = new Set([1])
      store.setSelection([2, 3])
      expect([...store.selectedIds]).toEqual([2, 3])
    })
  })

  describe('mutation reconciliation', () => {
    it('adds a created folder and updates pagination without duplicating it', () => {
      const store = useDriveStore()
      store.files = [...sampleFiles]
      store.pagination = { page: 1, page_size: 2, total: 3, total_pages: 2 }
      const created = {
        id: 8,
        name: 'created',
        parent_id: 0,
        path: '/created/',
        created_at: '2024-01-04',
      }

      store.applyCreatedFolder(created)
      store.applyCreatedFolder(created)

      expect(store.files.filter((item) => item.id === 8)).toHaveLength(1)
      expect(store.files.find((item) => item.id === 8)).toMatchObject({
        name: 'created',
        type: 'folder',
        updated_at: '2024-01-04',
      })
      expect(store.pagination).toMatchObject({ total: 4, total_pages: 2 })
    })

    it('applies successful rename and removal results to visible rows', () => {
      const store = useDriveStore()
      store.files = [...sampleFiles]
      store.pagination = { page: 1, page_size: 20, total: 3, total_pages: 1 }
      store.selectedIds = new Set([2, 3])

      store.applyItemRename({ id: 3, name: 'renamed', updated_at: '2024-01-05' })
      store.applyItemsRemoved([2])

      expect(store.files.find((item) => item.id === 3)).toMatchObject({
        name: 'renamed',
        updated_at: '2024-01-05',
      })
      expect(store.files.some((item) => item.id === 2)).toBe(false)
      expect([...store.selectedIds]).toEqual([3])
      expect(store.pagination).toMatchObject({ total: 2, total_pages: 1 })
    })
  })

  describe('setSortBy / setSortOrder / setFilterType', () => {
    it('updates sort field', () => {
      const store = useDriveStore()
      store.setSortBy('name')
      expect(store.sortBy).toBe('name')
    })

    it('updates sort order', () => {
      const store = useDriveStore()
      store.setSortOrder('asc')
      expect(store.sortOrder).toBe('asc')
    })

    it('updates filter type', () => {
      const store = useDriveStore()
      store.setFilterType('file')
      expect(store.filterType).toBe('file')
    })
  })

  describe('fetchFolderTree', () => {
    it('stores folder tree and toggles tree loading state', async () => {
      vi.mocked(folderApi.getFolderTree).mockResolvedValue({
        id: 0,
        name: 'Root',
        children: [{ id: 1, name: 'Docs', children: [] }],
      })

      const store = useDriveStore()
      await store.fetchFolderTree()
      expect(store.folderTree).toEqual({ id: 0, name: 'Root', children: [{ id: 1, name: 'Docs', children: [] }] })
      expect(store.folderTreeLoading).toBe(false)
    })

    it('refreshes hierarchy view with files, breadcrumb, and folder tree', async () => {
      vi.mocked(fileApi.listFiles).mockResolvedValue({
        items: sampleFiles,
        pagination: { page: 1, page_size: 20, total: 3, total_pages: 1 },
      })
      vi.mocked(folderApi.getBreadcrumb).mockResolvedValue({ path: [{ id: 5, name: 'SubFolder' }] })
      vi.mocked(folderApi.getFolderTree).mockResolvedValue({
        id: 0,
        name: 'Root',
        children: [{ id: 5, name: 'SubFolder', children: [] }],
      })

      const store = useDriveStore()
      store.currentFolderId = 5
      await store.refreshHierarchyView()

      expect(fileApi.listFiles).toHaveBeenCalled()
      expect(folderApi.getBreadcrumb).toHaveBeenCalledWith(5)
      expect(folderApi.getFolderTree).toHaveBeenCalled()
      expect(store.breadcrumbs).toEqual([{ id: 5, name: 'SubFolder' }])
      expect(store.folderTree?.children[0]?.id).toBe(5)
    })

    it('keeps folder tree, list, and breadcrumb consistent after hierarchy refresh', async () => {
      vi.mocked(fileApi.listFiles).mockResolvedValue({
        items: [{ id: 9, name: 'nested.txt', type: 'file', size: 12, mime_type: 'text/plain', created_at: '2024-01-01', updated_at: '2024-01-01' }],
        pagination: { page: 1, page_size: 20, total: 1, total_pages: 1 },
      })
      vi.mocked(folderApi.getBreadcrumb).mockResolvedValue({
        path: [{ id: 5, name: 'Docs' }, { id: 9, name: 'Nested' }],
      })
      vi.mocked(folderApi.getFolderTree).mockResolvedValue({
        id: 0,
        name: 'Root',
        children: [{ id: 5, name: 'Docs', children: [{ id: 9, name: 'Nested', children: [] }] }],
      })

      const store = useDriveStore()
      store.currentFolderId = 9
      store.selectedIds = new Set([1])

      await store.refreshCurrentView()

      expect(fileApi.listFiles).toHaveBeenCalledWith(expect.objectContaining({ parent_id: 9 }))
      expect(folderApi.getBreadcrumb).toHaveBeenCalledWith(9)
      expect(folderApi.getFolderTree).toHaveBeenCalled()
      expect(store.files).toHaveLength(1)
      expect(store.selectedIds.size).toBe(0)
      expect(store.breadcrumbs.map((item) => item.id)).toEqual([5, 9])
      expect(store.folderTree?.children[0]?.children?.[0]?.id).toBe(9)
    })

    it('preserves the last safe tree and navigation state when a tree refresh fails', async () => {
      vi.mocked(fileApi.listFiles).mockResolvedValue({
        items: [{ id: 12, name: 'fresh.txt', type: 'file', size: 5, mime_type: 'text/plain', created_at: '2024-01-01', updated_at: '2024-01-01' }],
        pagination: { page: 1, page_size: 20, total: 1, total_pages: 1 },
      })
      vi.mocked(folderApi.getBreadcrumb).mockResolvedValue({
        path: [{ id: 5, name: 'Docs' }],
      })
      vi.mocked(folderApi.getFolderTree).mockRejectedValueOnce(new Error('tree unavailable'))

      const store = useDriveStore()
      const safeTree = {
        id: 0,
        name: 'Root',
        children: [{ id: 5, name: 'Docs', children: [{ id: 8, name: 'Safe', children: [] }] }],
      }
      store.currentFolderId = 5
      store.folderTree = safeTree

      await store.refreshCurrentView()

      expect(store.currentFolderId).toBe(5)
      expect(store.folderTree).toEqual(safeTree)
      expect(store.folderTreeError).toBe('tree unavailable')
      expect(store.files[0]?.name).toBe('fresh.txt')
      expect(store.breadcrumbs).toEqual([{ id: 5, name: 'Docs' }])

      vi.mocked(folderApi.getFolderTree).mockResolvedValueOnce({
        id: 0,
        name: 'Root',
        children: [{ id: 5, name: 'Docs', children: [{ id: 9, name: 'Recovered', children: [] }] }],
      })
      await store.refreshFolderTree()

      expect(store.folderTreeError).toBeNull()
      expect(store.folderTree?.children[0]?.children[0]?.name).toBe('Recovered')
    })

    it('refreshes navigation metadata without discarding a safe tree on failure', async () => {
      vi.mocked(folderApi.getBreadcrumb).mockResolvedValue({
        path: [{ id: 5, name: 'Docs' }],
      })
      vi.mocked(folderApi.getFolderTree).mockRejectedValue(new Error('retry tree'))

      const store = useDriveStore()
      const safeTree = {
        id: 0,
        name: 'Root',
        children: [{ id: 5, name: 'Docs', children: [] }],
      }
      store.currentFolderId = 5
      store.folderTree = safeTree

      await store.refreshNavigationMetadata()

      expect(fileApi.listFiles).not.toHaveBeenCalled()
      expect(store.breadcrumbs).toEqual([{ id: 5, name: 'Docs' }])
      expect(store.folderTree).toEqual(safeTree)
      expect(store.folderTreeError).toBe('retry tree')
    })

    it('reapplies a confirmed folder move after a stale tree refresh', async () => {
      vi.mocked(folderApi.getBreadcrumb).mockResolvedValue({
        path: [{ id: 5, name: 'Source' }],
      })
      vi.mocked(folderApi.getFolderTree).mockResolvedValue({
        id: 0,
        name: 'Root',
        children: [
          { id: 5, name: 'Source', children: [] },
          { id: 6, name: 'Target', children: [] },
        ],
      })

      const store = useDriveStore()
      store.currentFolderId = 5
      store.folderTree = {
        id: 0,
        name: 'Root',
        children: [
          { id: 5, name: 'Source', children: [{ id: 9, name: 'Moved', children: [] }] },
          { id: 6, name: 'Target', children: [] },
        ],
      }

      await store.refreshAfterFolderMove([9], 6)

      expect(store.folderTree?.children[0]?.children).toEqual([])
      expect(store.folderTree?.children[1]?.children).toEqual([
        { id: 9, name: 'Moved', children: [] },
      ])
    })
  })

  describe('fetchBreadcrumb', () => {
    it('clears breadcrumbs for root', async () => {
      const store = useDriveStore()
      store.breadcrumbs = [{ id: 1, name: 'old' }]
      await store.fetchBreadcrumb()
      expect(store.breadcrumbs).toEqual([])
    })

    it('fetches breadcrumb for non-root folder', async () => {
      const path: BreadcrumbItem[] = [{ id: 1, name: 'Docs' }]
      vi.mocked(folderApi.getBreadcrumb).mockResolvedValue({ path })

      const store = useDriveStore()
      store.currentFolderId = 5
      await store.fetchBreadcrumb()
      expect(store.breadcrumbs).toEqual(path)
    })
  })
})
