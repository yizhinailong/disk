import { apiClient } from './client'
import type {
  CreateFolderRequest,
  CreateFolderResponse,
  FolderTreeQuery,
  FolderTreeResponse,
  BreadcrumbResponse,
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
