// ==================== 认证接口类型 ====================

/** 注册请求 */
export interface RegisterRequest {
  /** 用户名：4-32 字符，字母数字下划线 */
  readonly username: string;
  /** 有效邮箱格式 */
  readonly email: string;
  /** 密码：8-64 字符，需含大小写字母和数字 */
  readonly password: string;
}

/** 注册响应 */
export interface RegisterResponse {
  readonly id: number;
  readonly username: string;
  readonly email: string;
  readonly nickname: string;
  readonly storage_quota: number;
  readonly storage_used: number;
  readonly created_at: string;
}

/** 登录请求 */
export interface LoginRequest {
  /** 用户名或邮箱 */
  readonly account: string;
  /** 密码 */
  readonly password: string;
}

/** 登录响应中的用户信息 */
export interface LoginUser {
  readonly id: number;
  readonly username: string;
  readonly email: string;
  readonly nickname: string;
  readonly avatar: string | null;
  readonly storage_used: number;
  readonly storage_quota: number;
}

/** 登录响应 */
export interface LoginResponse {
  readonly access_token: string;
  readonly refresh_token: string;
  /** 固定 "Bearer" */
  readonly token_type: string;
  /** 有效期秒数，默认 7200 */
  readonly expires_in: number;
  readonly user: LoginUser;
}

/** 刷新令牌请求 */
export interface RefreshTokenRequest {
  readonly refresh_token: string;
}

/** 刷新令牌响应 */
export interface RefreshTokenResponse {
  readonly access_token: string;
  readonly refresh_token: string;
  readonly expires_in: number;
}

/** JWT Token 解码后的载荷 */
export interface TokenPayload {
  readonly user_id: number;
  readonly username: string;
  readonly role: number;
  readonly exp: number;
  readonly iat: number;
}
