import type { PaginationParams, PaginatedData, BatchSummary, FileItemType } from './api';

// ==================== 回收站列表 ====================

/** 回收站条目 */
export interface TrashItem {
  readonly id: number;
  /** 原始文件/文件夹 ID */
  readonly original_id: number;
  readonly name: string;
  readonly type: FileItemType;
  readonly size: number;
  /** 原始路径 */
  readonly original_path: string;
  readonly deleted_at: string;
  /** 过期时间，到期后自动清理 */
  readonly expires_at: string;
}

/** 回收站列表查询参数 */
export interface TrashListQuery extends PaginationParams {}

/** 回收站列表响应 */
export interface TrashListResponse extends PaginatedData<TrashItem> {}

// ==================== 恢复 ====================

/** 批量恢复请求 */
export interface TrashRestoreRequest {
  /** 回收站条目 ID 列表，1-100 项 */
  readonly trash_ids: readonly number[];
}

/** 恢复单项结果错误信息 */
export interface RestoreError {
  readonly code: number;
  readonly message: string;
  readonly field?: string;
  readonly value?: string;
}

/** 恢复单项结果 */
export interface RestoreResultItem {
  readonly trash_id: number;
  readonly status: 'success' | 'failed';
  readonly file_id?: number;
  readonly folder_id?: number;
  readonly path?: string;
  readonly error?: RestoreError;
}

/** 批量恢复响应 */
export interface TrashRestoreResponse {
  readonly summary: BatchSummary;
  readonly results: readonly RestoreResultItem[];
}

// ==================== 彻底删除 ====================

/** 批量彻底删除请求 */
export interface TrashDeleteRequest {
  readonly trash_ids: readonly number[];
}

/** 彻底删除单项错误信息 */
export interface DeleteError {
  readonly code: number;
  readonly message: string;
}

/** 彻底删除单项结果 */
export interface DeleteResultItem {
  readonly trash_id: number;
  readonly status: 'success' | 'failed';
  readonly freed_space?: number;
  readonly error?: DeleteError;
}

/** 批量彻底删除响应 */
export interface TrashDeleteResponse {
  readonly summary: BatchSummary;
  readonly results: readonly DeleteResultItem[];
}

// ==================== 清空回收站 ====================

/** 清空回收站响应 */
export interface TrashDeleteAllResponse {
  /** 删除条目数 */
  readonly deleted_count: number;
  /** 释放空间（字节） */
  readonly freed_space: number;
}
