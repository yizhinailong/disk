// ==================== 通用 API 响应结构 ====================

/** 统一 API 响应格式 */
export interface ApiResponse<T> {
  /** 业务错误码，0 表示成功 */
  readonly code: number;
  /** 提示消息 */
  readonly message: string;
  /** 业务数据 */
  readonly data: T | null;
}

// ==================== 分页 ====================

/** 分页元数据 */
export interface Pagination {
  /** 当前页码（从 1 开始） */
  readonly page: number;
  /** 每页条数 */
  readonly page_size: number;
  /** 总记录数 */
  readonly total: number;
  /** 总页数 */
  readonly total_pages: number;
}

/** 分页数据容器 */
export interface PaginatedData<T> {
  /** 数据列表 */
  readonly items: readonly T[];
  /** 分页信息 */
  readonly pagination: Pagination;
}

/** 分页查询参数 */
export interface PaginationParams {
  /** 页码（默认 1） */
  page?: number;
  /** 每页条数（默认 20，最大 100） */
  page_size?: number;
}

// ==================== 排序 ====================

/** 排序参数 */
export interface SortParams {
  /** 排序字段：name / size / created_at / updated_at */
  sort_by?: string;
  /** 排序方向 */
  sort_order?: 'asc' | 'desc';
}

// ==================== 批量操作 ====================

/** 批量操作单项失败信息 */
export interface BatchFailureItem {
  /** 条目 ID */
  readonly id: string | number;
  /** 失败原因 */
  readonly reason: string;
}

/** 批量操作结果 */
export interface BatchResult<T> {
  /** 成功条目列表 */
  readonly success: readonly T[];
  /** 失败条目列表 */
  readonly failure: readonly BatchFailureItem[];
}

/** 批量操作摘要 */
export interface BatchSummary {
  /** 总数 */
  readonly total: number;
  /** 成功数 */
  readonly success_count: number;
  /** 失败数 */
  readonly failure_count: number;
}

// ==================== 错误码 ====================

/** 通用错误码 */
export type GeneralErrorCode =
  | 0     // Success
  | 10001 // InvalidParameter
  | 10002 // ValidationFailed
  | 10003 // ResourceNotFound
  | 10004 // ResourceConflict
  | 10005 // TooManyRequests
  | 10006; // InternalError

/** 认证错误码 */
export type AuthErrorCode =
  | 40001 // UsernameExists
  | 40002 // EmailExists
  | 40100 // UserNotFound
  | 40101 // InvalidCredentials
  | 40102 // AccountLocked
  | 40103 // AccountDisabled
  | 40104 // InvalidToken
  | 40105 // InvalidRefreshToken
  | 40106 // TokenMissing
  | 40107 // TokenMalformed
  | 40108 // TokenExpired
  | 40109 // TokenWrongType
  | 40110 // RefreshTokenAlreadyUsed
  | 40111; // TokenRevoked

/** 文件错误码 */
export type FileErrorCode =
  | 50001 // InvalidFilename
  | 50004 // StorageQuotaExceeded
  | 50005 // FileNotFound
  | 50006 // FolderNotFound
  | 50007 // FileAlreadyExists
  | 50008 // UploadTaskNotFound
  | 50009 // ChunkVerifyFailed
  | 50010 // FolderAlreadyExists
  | 50011; // FileReadError

/** 分享错误码 */
export type ShareErrorCode =
  | 60001 // ShareNotFound
  | 60002 // ShareExpired
  | 60003 // SharePasswordError
  | 60004; // ShareAccessDenied

/** Redis 错误码 */
export type RedisErrorCode =
  | 70002 // RedisOperationFailed
  | 70003; // RedisKeyNotFound

/** 管理员错误码 */
export type AdminErrorCode =
  | 80001 // AdminRequired
  | 80002 // AdminUserNotFound
  | 80003 // AdminCannotModifySelf
  | 80004 // AdminCannotDemoteLast
  | 80005 // AdminShareNotFound
  | 80006 // AdminInvalidStatus
  | 80007; // AdminInvalidRole

/** 全部业务错误码联合类型 */
export type ErrorCode =
  | GeneralErrorCode
  | AuthErrorCode
  | FileErrorCode
  | ShareErrorCode
  | RedisErrorCode
  | AdminErrorCode;

// ==================== 文件类型 ====================

/** 文件/文件夹类型 */
export type FileItemType = 'file' | 'folder';

/** 用户状态 */
export type UserStatus = 0 | 1 | 2;

/** 用户角色 */
export type UserRole = 0 | 1;
