// Package api API 客户端模块
//
// 提供与后端 API 通信的能力，包括认证、文件管理、分享等功能。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package api

import (
	"context"

	"github.com/yizhinailong/disk/ui/tui/internal/models"
)

// AuthAPI 认证 API
//
// 提供用户认证相关的 API 调用，包括登录、登出、令牌刷新等。
type AuthAPI struct {
	client *Client // API 客户端
}

// Login 用户登录
//
// 业务规则：
//   - 支持用户名或邮箱登录
//   - 密码错误5次锁定账户15分钟
//
// 参数:
//   - ctx: 上下文
//   - account: 账号（用户名或邮箱）
//   - password: 密码
//
// 返回:
//   - *models.LoginResponse: 登录响应（包含令牌）
//   - error: 错误信息
func (a *AuthAPI) Login(ctx context.Context, account, password string) (*models.LoginResponse, error) {
	req := &models.LoginRequest{
		Account:  account,
		Password: password,
	}

	var resp models.LoginResponse
	if err := a.client.doRequest(ctx, "POST", "/api/auth/login", req, &resp); err != nil {
		return nil, err
	}

	a.client.SetToken(resp.AccessToken, resp.RefreshToken, resp.ExpiresIn)

	return &resp, nil
}

// Logout 用户登出
//
// 清除服务端的刷新令牌并清除本地存储的令牌。
//
// 参数:
//   - ctx: 上下文
//
// 返回:
//   - error: 错误信息
func (a *AuthAPI) Logout(ctx context.Context) error {
	err := a.client.doRequest(ctx, "POST", "/api/auth/logout", nil, nil)
	if err == nil {
		a.client.ClearToken()
	}
	return err
}

// Refresh 刷新令牌
//
// 使用 refresh_token 获取新的 access_token 和 refresh_token。
// 旧的 refresh_token 会失效。
//
// 参数:
//   - ctx: 上下文
//
// 返回:
//   - error: 错误信息（无刷新令牌时返回 40105 错误）
func (a *AuthAPI) Refresh(ctx context.Context) error {
	a.client.mu.RLock()
	refreshToken := a.client.refreshToken
	a.client.mu.RUnlock()

	if refreshToken == "" {
		return &APIError{Code: 40105, Message: "无刷新令牌"}
	}

	req := &models.RefreshRequest{
		RefreshToken: refreshToken,
	}

	var resp models.RefreshResponse
	if err := a.client.doRequest(ctx, "POST", "/api/auth/refresh", req, &resp); err != nil {
		a.client.ClearToken()
		return err
	}

	a.client.SetToken(resp.AccessToken, resp.RefreshToken, resp.ExpiresIn)

	return nil
}
