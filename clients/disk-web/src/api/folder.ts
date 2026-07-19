import { apiClient } from './client'
import type {
  CreateFolderRequest,
  CreateFolderResponse,
  FolderTreeQuery,
  FolderTreeResponse,
  BreadcrumbResponse,
  RenameRequest,
  RenameFolderResponse,
} from '@/types'

export function createFolder(data: CreateFolderRequest): Promise<CreateFolderResponse> {
  return apiClient.post('/folder/create', data) as Promise<CreateFolderResponse>
}

export function getFolderTree(params?: FolderTreeQuery): Promise<FolderTreeResponse> {
  return apiClient.get('/folder/tree', { params }) as Promise<FolderTreeResponse>
}

export function getBreadcrumb(folderId: number): Promise<BreadcrumbResponse> {
  return apiClient.get(`/folder/${folderId}/breadcrumb`) as Promise<BreadcrumbResponse>
}

export function renameFolder(folderId: number, data: RenameRequest): Promise<RenameFolderResponse> {
  return apiClient.put(`/folder/${folderId}/rename`, data) as Promise<RenameFolderResponse>
}
