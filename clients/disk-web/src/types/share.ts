import type { PaginationParams, PaginatedData, FileItemType } from './api';

// ==================== 分享状态 ====================

/** 分享状态 */
export type ShareStatus = 'active' | 'expired' | 'cancelled';

/** 分享权限 */
export type SharePermission = 'view' | 'download';

// ==================== 创建分享 ====================

/** 创建分享请求 */
export interface CreateShareRequest {
  /** 文件 ID 列表（至少提供 file_ids 或 folder_ids 之一） */
  file_ids?: readonly number[];
  /** 文件夹 ID 列表 */
  folder_ids?: readonly number[];
  /** 有效期天数，默认 7，0 表示永久 */
  expire_days?: number;
  /** 访问密码：4-8 字符，可选 */
  password?: string;
  /** 权限，默认 download */
  permission?: SharePermission;
}

/** 创建分享响应 */
export interface CreateShareResponse {
  readonly share_id: string;
  readonly share_link: string;
  readonly password?: string;
  readonly permission: string;
  readonly expires_at: string;
  readonly created_at: string;
}

// ==================== 分享列表 ====================

/** 分享列表项 */
export interface ShareItem {
  readonly share_id: string;
  readonly file_name: string;
  readonly file_count: number;
  readonly share_link: string;
  readonly has_password: boolean;
  readonly permission: string;
  readonly view_count: number;
  readonly download_count: number;
  readonly created_at: string;
  readonly expires_at: string;
  readonly status: ShareStatus;
}

/** 分享列表查询参数 */
export interface ShareListQuery extends PaginationParams {
  /** 状态过滤，默认 all */
  status?: 'all' | ShareStatus;
}

/** 分享列表响应 */
export interface ShareListResponse extends PaginatedData<ShareItem> {}

// ==================== 分享详情 ====================

/** 分享中的文件/文件夹项 */
export interface ShareFile {
  readonly id: number;
  readonly name: string;
  readonly type: FileItemType;
  readonly size: number;
  /** 文件时有值，用于下载完整性校验 */
  readonly file_hash?: string;
  /** 文件是否支持 HTTP Range 下载 */
  readonly supports_range?: boolean;
  /** 文件夹时有值 */
  readonly item_count?: number;
}

/** 分享详情响应 */
export interface ShareDetailResponse {
  readonly share_id: string;
  readonly files: readonly ShareFile[];
  readonly share_link: string;
  readonly has_password: boolean;
  readonly permission: string;
  readonly view_count: number;
  readonly download_count: number;
  readonly created_at: string;
  readonly expires_at: string;
  readonly status: string;
}

// ==================== 更新分享 ====================

/** 更新分享请求 */
export interface UpdateShareRequest {
  /** 从当前时间计算的新有效期天数 */
  expire_days?: number;
  /** 新密码，空字符串表示移除密码 */
  password?: string;
  /** 权限 */
  permission?: SharePermission;
}

/** 更新分享响应 */
export interface UpdateShareResponse {
  readonly share_id: string;
  readonly expires_at: string;
  readonly has_password: boolean;
  readonly permission: string;
  readonly updated_at: string;
}

// ==================== 取消分享 ====================

/** 批量取消分享请求 */
export interface CancelShareRequest {
  readonly share_ids: readonly string[];
}

/** 取消分享单项结果 */
export interface CancelShareResult {
  readonly share_id: string;
  readonly status: 'success' | 'failed';
  readonly error?: {
    readonly code: number;
    readonly message: string;
    readonly reason: string;
  };
}

/** 批量取消分享响应 */
export interface CancelShareResponse {
  readonly summary: {
    readonly total: number;
    readonly succeeded: number;
    readonly failed: number;
  };
  readonly results: readonly CancelShareResult[];
}

// ==================== 分享访问 ====================

/** 分享密码验证请求 */
export interface AccessShareRequest {
  /** 分享设置了密码时必填 */
  password?: string;
}

/** 分享密码验证响应 */
export interface AccessShareResponse {
  readonly share_token: string;
  /** 有效期秒数，默认 3600 */
  readonly expires_in: number;
  readonly permission: string;
  readonly files: readonly ShareFile[];
}

// ==================== 分享浏览 ====================

/** 分享浏览条目 */
export interface BrowseItem {
  readonly id: number;
  readonly name: string;
  readonly type: FileItemType;
  readonly size: number;
  /** 文件时有值，用于下载完整性校验 */
  readonly file_hash?: string;
  /** 文件是否支持 HTTP Range 下载 */
  readonly supports_range?: boolean;
  /** 文件夹时有值 */
  readonly item_count?: number;
}

/** 分享浏览面包屑 */
export interface BrowseBreadcrumb {
  readonly id: number;
  readonly name: string;
}

/** 分享浏览查询参数 */
export interface BrowseShareQuery {
  /** 分享内的相对文件夹 ID */
  folder_id?: number;
}

/** 分享浏览响应 */
export interface BrowseShareResponse {
  readonly items: readonly BrowseItem[];
  readonly breadcrumb: readonly BrowseBreadcrumb[];
}

// ==================== 保存分享到我的网盘 ====================

/** 保存分享请求 */
export interface SaveShareRequest {
  /** 要保存的文件 ID 列表 */
  file_ids?: readonly number[];
  /** 要保存的文件夹 ID 列表 */
  folder_ids?: readonly number[];
  /** 目标文件夹 ID（0 或不传表示保存到根目录） */
  target_folder_id?: number;
}

/** 保存分享响应 */
export interface SaveShareResponse {
  readonly summary: {
    readonly total: number;
    readonly succeeded: number;
    readonly failed: number;
  };
}
