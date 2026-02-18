// Package models 数据模型定义
//
// 定义 TUI 客户端使用的所有数据结构，包括用户、文件、文件夹、分享和回收站等。
// 这些模型与后端 API 的 JSON 响应结构保持一致。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package models

// ApiResponse 通用 API 响应
//
// 泛型响应结构，所有 API 返回数据都包装在此结构中。
type ApiResponse[T any] struct {
	Code    int    `json:"code"`    // 状态码（0 表示成功）
	Message string `json:"message"` // 响应消息
	Data    T      `json:"data"`    // 响应数据
}

// Error 错误信息
type Error struct {
	Code    int    `json:"code"`    // 错误码
	Message string `json:"message"` // 错误消息
}

// LoginRequest 登录请求
type LoginRequest struct {
	Account  string `json:"account"`  // 账号（用户名或邮箱）
	Password string `json:"password"` // 密码
}

// LoginResponse 登录响应
type LoginResponse struct {
	AccessToken  string `json:"access_token"`  // 访问令牌（2 小时有效）
	RefreshToken string `json:"refresh_token"` // 刷新令牌（7 天有效，单次使用）
	TokenType    string `json:"token_type"`    // 令牌类型（Bearer）
	ExpiresIn    int    `json:"expires_in"`    // 访问令牌有效期（秒）
	User         *User  `json:"user"`          // 用户信息
}

// RefreshRequest 刷新令牌请求
type RefreshRequest struct {
	RefreshToken string `json:"refresh_token"` // 刷新令牌
}

// RefreshResponse 刷新令牌响应
type RefreshResponse struct {
	AccessToken  string `json:"access_token"`  // 新的访问令牌
	RefreshToken string `json:"refresh_token"` // 新的刷新令牌（旧的立即失效）
	ExpiresIn    int    `json:"expires_in"`    // 访问令牌有效期（秒）
}
