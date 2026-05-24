import type { PaginationParams, PaginatedData, SortParams, FileItemType } from './api';

// ==================== 文件列表 ====================

/** 文件/文件夹列表项 */
export interface FileItem {
  readonly id: number;
  readonly name: string;
  readonly type: FileItemType;
  /** 文件时有值 */
  readonly size?: number;
  /** 文件时有值 */
  readonly mime_type?: string;
  /** 文件时有值 */
  readonly hash?: string;
  /** 文件夹时有值 */
  readonly item_count?: number;
  readonly created_at: string;
  readonly updated_at: string;
}

/** 文件列表查询参数 */
export interface FileListQuery extends PaginationParams, SortParams {
  /** 父目录 ID，默认 0（根目录） */
  parent_id?: number;
  /** 类型过滤：all / file / folder，默认 all */
  type?: string;
}

/** 文件列表响应 */
export interface FileListResponse extends PaginatedData<FileItem> {}

// ==================== 文件详情 ====================

/** 文件详情响应 */
export interface FileDetailResponse {
  readonly id: number;
  readonly name: string;
  readonly type: FileItemType;
  readonly size: number;
  readonly hash: string;
  readonly mime_type: string;
  readonly parent_id: number;
  /** 面包屑路径 */
  readonly path: string;
  readonly created_at: string;
  readonly updated_at: string;
}

// ==================== 文件搜索 ====================

/** 搜索结果项 */
export interface SearchResultItem extends FileItem {
  /** 面包屑路径 */
  readonly path: string;
}

/** 搜索查询参数 */
export interface SearchQuery extends PaginationParams {
  /** 搜索关键词：1-100 字符，必填 */
  readonly keyword: string;
  /** 类型过滤：all / file / folder，默认 all */
  type?: string;
  /** 限定搜索范围的目录 ID */
  folder_id?: number;
}

/** 搜索响应 */
export interface SearchResponse extends PaginatedData<SearchResultItem> {}

// ==================== 文件操作 ====================

/** 重命名请求 */
export interface RenameRequest {
  /** 新名称：1-255 字符，同文件名约束 */
  readonly new_name: string;
}

/** 重命名响应 */
export interface RenameResponse {
  readonly id: number;
  readonly name: string;
  readonly updated_at: string;
}

/** 移动请求 */
export interface MoveRequest {
  /** 文件 ID 列表 */
  file_ids?: readonly number[];
  /** 文件夹 ID 列表 */
  folder_ids?: readonly number[];
  /** 目标文件夹 ID */
  readonly target_folder_id: number;
}

/** 移动响应 */
export interface MoveResponse {
  readonly moved_count: number;
  readonly moved_file_count: number;
  readonly moved_folder_count: number;
}

/** 复制请求 */
export interface CopyRequest {
  /** 文件 ID 列表 */
  file_ids?: readonly number[];
  /** 文件夹 ID 列表 */
  folder_ids?: readonly number[];
  /** 目标文件夹 ID */
  readonly target_folder_id: number;
}

/** 复制后的文件映射 */
export interface CopiedFileMapping {
  readonly old_id: number;
  readonly new_id: number;
}

/** 复制响应 */
export interface CopyResponse {
  readonly copied_count: number;
  readonly new_files: readonly CopiedFileMapping[];
}

/** 批量删除请求 */
export interface DeleteRequest {
  /** 文件 ID 列表 */
  file_ids?: readonly number[];
  /** 文件夹 ID 列表 */
  folder_ids?: readonly number[];
}

/** 批量删除响应 */
export interface DeleteResponse {
  readonly deleted_count: number;
}

// ==================== 目录操作 ====================

/** 创建目录请求 */
export interface CreateFolderRequest {
  /** 目录名称：1-255 字符，同文件名约束 */
  readonly name: string;
  /** 父目录 ID，默认 0（根目录） */
  parent_id?: number;
}

/** 创建目录响应 */
export interface CreateFolderResponse {
  readonly id: number;
  readonly name: string;
  readonly parent_id: number;
  readonly path: string;
  readonly created_at: string;
}

/** 目录树节点 */
export interface FolderTreeNode {
  readonly id: number;
  readonly name: string;
  readonly children: readonly FolderTreeNode[];
}

/** 目录树查询参数 */
export interface FolderTreeQuery {
  /** 父目录 ID，默认 0 */
  parent_id?: number;
  /** 深度，默认 -1（全部） */
  depth?: number;
}

/** 目录树响应 */
export interface FolderTreeResponse {
  readonly id: number;
  readonly name: string;
  readonly children: readonly FolderTreeNode[];
}

/** 面包屑条目 */
export interface BreadcrumbItem {
  readonly id: number;
  readonly name: string;
}

/** 面包屑响应 */
export interface BreadcrumbResponse {
  readonly path: readonly BreadcrumbItem[];
}
