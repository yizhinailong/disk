// ==================== 用户接口类型 ====================

/** 用户资料 */
export interface UserProfile {
  readonly id: number;
  readonly username: string;
  readonly email: string;
  readonly nickname: string;
  readonly avatar: string;
  readonly storage_used: number;
  readonly storage_quota: number;
  readonly file_count: number;
  readonly folder_count: number;
  readonly created_at: string;
  readonly updated_at: string;
}

/** 用户资料响应 */
export interface UserProfileResponse {
  readonly user: UserProfile;
}

/** 更新用户资料请求 */
export interface UpdateProfileRequest {
  /** 昵称：1-64 字符，去除首尾空格 */
  nickname?: string;
  /** 头像 URL：必须是 https，禁止内网地址 */
  avatar?: string;
}

/** 更新用户资料响应 */
export interface UpdateProfileResponse {
  readonly user: UserProfile;
}

/** 修改密码请求 */
export interface ChangePasswordRequest {
  readonly old_password: string;
  /** 8-64 字符，需含大小写字母和数字，不能与旧密码相同 */
  readonly new_password: string;
}

/** 存储分类统计 */
export interface StorageCategory {
  /** 分类：document / image / video / audio / other */
  readonly type: string;
  /** 占用大小（字节） */
  readonly size: number;
  /** 文件数量 */
  readonly count: number;
}

/** 存储空间统计 */
export interface StorageStats {
  /** 实际已使用（含回收站） */
  readonly used: number;
  /** 已预留但未完成的上传空间 */
  readonly reserved: number;
  /** 总配额 */
  readonly quota: number;
  /** 使用百分比，1 位小数 */
  readonly percentage: number;
  /** 分类统计 */
  readonly categories: readonly StorageCategory[];
}

/** 存储统计响应 */
export interface StorageResponse {
  readonly used: number;
  readonly reserved: number;
  readonly quota: number;
  readonly percentage: number;
  readonly categories: readonly StorageCategory[];
}
