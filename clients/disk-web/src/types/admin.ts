import type { PaginationParams, PaginatedData } from './api';
import type { ShareFile } from './share';

// ==================== 管理员 - 用户管理 ====================

/** 管理员用户列表项 */
export interface AdminUserItem {
  readonly id: number;
  readonly username: string;
  readonly email: string;
  readonly nickname: string;
  /** 0=禁用 1=正常 2=锁定 */
  readonly status: number;
  /** 0=普通用户 1=管理员 */
  readonly role: number;
  readonly storage_used: number;
  readonly storage_quota: number;
  readonly file_count: number;
  readonly folder_count: number;
  readonly created_at: string;
  readonly updated_at: string;
}

/** 管理员用户列表查询参数 */
export interface AdminListUsersQuery extends PaginationParams {
  /** 用户名模糊匹配 */
  username?: string;
  /** 邮箱模糊匹配 */
  email?: string;
  /** 状态过滤：0/1/2 */
  status?: number;
  /** 角色过滤：0/1 */
  role?: number;
}

/** 管理员用户列表响应 */
export interface AdminUserListResponse extends PaginatedData<AdminUserItem> {}

/** 管理员用户详情响应 */
export interface AdminUserDetailResponse {
  readonly id: number;
  readonly username: string;
  readonly email: string;
  readonly nickname: string;
  readonly avatar: string;
  readonly status: number;
  readonly role: number;
  readonly storage_used: number;
  readonly storage_quota: number;
  readonly file_count: number;
  readonly folder_count: number;
  readonly created_at: string;
  readonly updated_at: string;
}

/** 修改用户状态请求 */
export interface AdminChangeStatusRequest {
  /** 0=禁用 1=正常 2=锁定 */
  readonly status: number;
}

/** 修改用户状态响应 */
export interface AdminChangeStatusResponse {
  readonly id: number;
  readonly status: number;
  readonly updated_at: string;
}

/** 修改用户角色请求 */
export interface AdminChangeRoleRequest {
  /** 0=普通用户 1=管理员 */
  readonly role: number;
}

/** 修改用户角色响应 */
export interface AdminChangeRoleResponse {
  readonly id: number;
  readonly role: number;
  readonly updated_at: string;
}

/** 删除用户响应 */
export interface AdminDeleteUserResponse {
  readonly id: number;
  readonly status: number;
  readonly updated_at: string;
}

// ==================== 管理员 - 存储统计 ====================

/** 管理员全局存储统计响应 */
export interface AdminStorageStatsResponse {
  readonly total_users: number;
  readonly total_files: number;
  readonly total_folders: number;
  readonly total_size: number;
  readonly user_count: number;
  readonly active_user_count: number;
}

// ==================== 管理员 - 分享管理 ====================

/** 管理员分享列表项 */
export interface AdminShareItem {
  readonly share_id: string;
  readonly user_id: number;
  readonly username: string;
  readonly file_name: string;
  readonly file_count: number;
  readonly share_link: string;
  readonly has_password: boolean;
  readonly permission: string;
  readonly view_count: number;
  readonly download_count: number;
  readonly created_at: string;
  readonly expires_at: string;
  /** 0/1/2 */
  readonly status: number;
}

/** 管理员分享列表查询参数 */
export interface AdminListSharesQuery extends PaginationParams {
  /** 状态过滤：0/1/2 */
  status?: number;
  /** 用户 ID */
  user_id?: number;
  /** 用户名模糊匹配 */
  username?: string;
}

/** 管理员分享列表响应 */
export interface AdminShareListResponse extends PaginatedData<AdminShareItem> {}

/** 管理员分享详情响应 */
export interface AdminShareDetailResponse {
  readonly share_id: string;
  readonly user_id: number;
  readonly username: string;
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

/** 管理员删除分享响应 */
export interface AdminDeleteShareResponse {
  readonly share_id: string;
  readonly status: string;
  readonly updated_at: string;
}

// ==================== 管理员 - 概览统计 ====================

/** 管理员概览统计响应 */
export interface AdminStatsOverviewResponse {
  readonly user_count: number;
  readonly file_count: number;
  readonly storage_size: number;
  readonly share_count: number;
  readonly active_share_count: number;
  readonly storage_quota: number;
}

/** MySQL 连接状态 */
export interface MysqlStatus {
  readonly connected: boolean;
  readonly connection_count: number;
  readonly latency_ms: number;
}

/** Redis 连接状态 */
export interface RedisStatus {
  readonly connected: boolean;
  readonly latency_ms: number;
}

/** 磁盘使用状态 */
export interface DiskUsage {
  readonly total: number;
  readonly used: number;
  readonly free: number;
  readonly percentage: number;
}

/** 管理员系统状态响应 */
export interface AdminStatsSystemResponse {
  readonly uptime: number;
  readonly version: string;
  readonly mysql: MysqlStatus;
  readonly redis: RedisStatus;
  readonly disk: DiskUsage;
}

// ==================== 系统与日志 ====================

/** 健康检查组件状态 */
export interface HealthComponent {
  readonly status: 'healthy' | 'unhealthy';
  readonly latency_ms?: number;
  readonly message?: string;
}

/** 健康检查响应 */
export interface HealthResponse {
  readonly overall_status: 'healthy' | 'degraded' | 'unhealthy';
  readonly version: string;
  /** 运行时间（秒） */
  readonly uptime: number;
  readonly timestamp: string;
  readonly components: {
    readonly database: HealthComponent;
    readonly redis: HealthComponent;
  };
}

/** 系统信息连接统计 */
export interface ConnectionStats {
  readonly current: number;
  readonly peak: number;
}

/** 系统信息存储统计 */
export interface SystemStorageStats {
  readonly total_users: number;
  readonly total_files: number;
  readonly total_folders: number;
  readonly total_size: number;
}

/** 系统信息响应 */
export interface SystemInfoResponse {
  readonly version: string;
  readonly drogon_version: string;
  readonly build_time: string;
  readonly uptime: number;
  readonly connections: ConnectionStats;
  readonly storage: SystemStorageStats;
}

/** 操作日志条目 */
export interface LogItem {
  readonly id: number;
  /** 操作类型 */
  readonly action: string;
  /** 目标类型 */
  readonly target_type: string;
  readonly target_id?: number;
  readonly target_name?: string;
  /** JSON 字符串 */
  readonly details?: string;
  readonly ip_address: string;
  readonly created_at: string;
}

/** 操作日志查询参数 */
export interface LogsQuery extends PaginationParams {}

/** 操作日志响应 */
export interface LogsResponse {
  readonly items: readonly LogItem[];
  readonly total: number;
  readonly page: number;
  readonly page_size: number;
}
