// Package api API 客户端模块
//
// 提供与后端 API 通信的能力，包括认证、文件管理、分享等功能。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package api

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"sync"
	"time"

	"github.com/yizhinailong/disk/ui/tui/internal/config"
	"github.com/yizhinailong/disk/ui/tui/internal/models"
	"github.com/yizhinailong/disk/ui/tui/internal/store"
)

// Client API 客户端
//
// 提供与后端 API 通信的能力，包括认证、文件管理、分享等功能。
type Client struct {
	httpClient *http.Client      // HTTP 客户端
	config     *config.Config    // 配置
	tokenStore *store.TokenStore // 令牌存储

	// Token 管理
	mu           sync.RWMutex // 读写锁
	accessToken  string       // 访问令牌
	refreshToken string       // 刷新令牌
	tokenExpiry  time.Time    // 令牌过期时间

	// 子模块
	Auth   *AuthAPI   // 认证 API
	File   *FileAPI   // 文件 API
	Folder *FolderAPI // 文件夹 API
	Trash  *TrashAPI  // 回收站 API
	Share  *ShareAPI  // 分享 API
}

// NewClient 创建 API 客户端
//
// 参数:
//   - cfg: 配置对象
//   - tokenStore: 令牌存储
//
// 返回:
//   - *Client: API 客户端实例
func NewClient(cfg *config.Config, tokenStore *store.TokenStore) *Client {
	c := &Client{
		httpClient: &http.Client{
			Timeout: time.Duration(cfg.Server.Timeout) * time.Second,
		},
		config:     cfg,
		tokenStore: tokenStore,
	}

	// 初始化子模块
	c.Auth = &AuthAPI{client: c}
	c.File = &FileAPI{client: c}
	c.Folder = &FolderAPI{client: c}
	c.Trash = &TrashAPI{client: c}
	c.Share = &ShareAPI{client: c}

	return c
}

// LoadToken 从存储加载令牌
func (c *Client) LoadToken() error {
	data, err := c.tokenStore.Load()
	if err != nil {
		return err
	}
	if data != nil {
		c.mu.Lock()
		c.accessToken = data.AccessToken
		c.refreshToken = data.RefreshToken
		c.tokenExpiry = time.Unix(data.ExpiresAt, 0)
		c.mu.Unlock()
	}
	return nil
}

// SaveToken 保存令牌到存储
func (c *Client) SaveToken() error {
	c.mu.RLock()
	data := &store.TokenData{
		AccessToken:  c.accessToken,
		RefreshToken: c.refreshToken,
		ExpiresAt:    c.tokenExpiry.Unix(),
	}
	c.mu.RUnlock()
	return c.tokenStore.Save(data)
}

// SetToken 设置令牌
//
// 参数:
//   - accessToken: 访问令牌
//   - refreshToken: 刷新令牌
//   - expiresIn: 过期时间（秒）
func (c *Client) SetToken(accessToken, refreshToken string, expiresIn int) {
	c.mu.Lock()
	c.accessToken = accessToken
	c.refreshToken = refreshToken
	c.tokenExpiry = time.Now().Add(time.Duration(expiresIn) * time.Second)
	c.mu.Unlock()
}

// ClearToken 清除令牌
func (c *Client) ClearToken() {
	c.mu.Lock()
	c.accessToken = ""
	c.refreshToken = ""
	c.tokenExpiry = time.Time{}
	c.mu.Unlock()
}

// IsLoggedIn 检查是否已登录
//
// 验证 access_token 是否存在且未过期。
func (c *Client) IsLoggedIn() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.accessToken != "" && time.Now().Before(c.tokenExpiry)
}

// NeedRefresh 检查是否需要刷新令牌
//
// 令牌在 5 分钟内过期时返回 true。
func (c *Client) NeedRefresh() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.refreshToken != "" && time.Now().Add(5*time.Minute).After(c.tokenExpiry)
}

// GetAccessToken 获取当前访问令牌
//
// 用于下载等需要直接使用令牌的场景。
func (c *Client) GetAccessToken() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.accessToken
}

// doRequest 执行 HTTP 请求
//
// 内部方法，处理请求构建、认证、响应解析和错误处理。
// 自动刷新即将过期的令牌。
//
// 参数:
//   - ctx: 上下文
//   - method: HTTP 方法
//   - path: API 路径
//   - body: 请求体（可为 nil）
//   - result: 响应结果指针（可为 nil）
func (c *Client) doRequest(ctx context.Context, method, path string, body, result any) error {
	// 检查并刷新 Token（仅当需要认证时）
	if c.NeedRefresh() {
		if err := c.Auth.Refresh(ctx); err != nil {
			return fmt.Errorf("failed to refresh token: %w", err)
		}
	}

	// 构建请求
	var reqBody io.Reader
	if body != nil {
		data, err := json.Marshal(body)
		if err != nil {
			return fmt.Errorf("failed to serialize request: %w", err)
		}
		reqBody = bytes.NewReader(data)
	}

	url := c.config.Server.URL + path
	req, err := http.NewRequestWithContext(ctx, method, url, reqBody)
	if err != nil {
		return fmt.Errorf("failed to create request: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")

	// 添加认证头
	c.mu.RLock()
	if c.accessToken != "" {
		req.Header.Set("Authorization", "Bearer "+c.accessToken)
	}
	c.mu.RUnlock()

	// 发送请求
	resp, err := c.httpClient.Do(req)
	if err != nil {
		return fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	// 读取响应
	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("failed to read response: %w", err)
	}

	// 解析响应
	var apiResp models.ApiResponse[json.RawMessage]
	if err := json.Unmarshal(respBody, &apiResp); err != nil {
		return fmt.Errorf("failed to parse response: %w", err)
	}

	// 检查错误
	if apiResp.Code != 0 {
		return &APIError{Code: apiResp.Code, Message: apiResp.Message}
	}

	// 解析数据
	if result != nil && apiResp.Data != nil {
		if err := json.Unmarshal(apiResp.Data, result); err != nil {
			return fmt.Errorf("failed to parse data: %w", err)
		}
	}

	return nil
}

// APIError API 错误
//
// 封装后端 API 返回的错误信息。
type APIError struct {
	Code    int    // 错误码
	Message string // 错误消息
}

// Error 实现 error 接口
func (e *APIError) Error() string {
	return fmt.Sprintf("API Error(%d): %s", e.Code, e.Message)
}

// IsAuthError 检查是否为认证错误
//
// 错误码在 40100-40199 范围内为认证错误。
func (e *APIError) IsAuthError() bool {
	return e.Code >= 40100 && e.Code < 40200
}

// IsTokenExpired 检查是否为令牌过期错误（错误码 40108）
func (e *APIError) IsTokenExpired() bool {
	return e.Code == 40108
}

// IsRefreshTokenUsed 检查是否为刷新令牌已使用错误（错误码 40110）
func (e *APIError) IsRefreshTokenUsed() bool {
	return e.Code == 40110
}

// doMultipartRequest 执行 multipart/form-data HTTP 请求
//
// 用于文件上传等需要 multipart 表单的场景。
//
// 参数:
//   - ctx: 上下文
//   - method: HTTP 方法
//   - path: API 路径
//   - fields: 表单字段（键值对）
//   - files: 文件字段（字段名 -> (文件名, 数据)）
//   - result: 响应结果指针（可为 nil）
func (c *Client) doMultipartRequest(ctx context.Context, method, path string, fields map[string]string, files map[string]struct {
	Filename string
	Data     []byte
}, result any) error {
	// 检查并刷新 Token
	if c.NeedRefresh() {
		if err := c.Auth.Refresh(ctx); err != nil {
			return fmt.Errorf("failed to refresh token: %w", err)
		}
	}

	// 构建 multipart body
	var body bytes.Buffer
	writer := multipart.NewWriter(&body)

	// 添加普通字段
	for key, value := range fields {
		if err := writer.WriteField(key, value); err != nil {
			return fmt.Errorf("failed to write field %s: %w", key, err)
		}
	}

	// 添加文件字段
	for fieldName, file := range files {
		part, err := writer.CreateFormFile(fieldName, file.Filename)
		if err != nil {
			return fmt.Errorf("failed to create file field %s: %w", fieldName, err)
		}
		if _, err := part.Write(file.Data); err != nil {
			return fmt.Errorf("failed to write file data: %w", err)
		}
	}

	if err := writer.Close(); err != nil {
		return fmt.Errorf("failed to close multipart writer: %w", err)
	}

	// 创建请求
	url := c.config.Server.URL + path
	req, err := http.NewRequestWithContext(ctx, method, url, &body)
	if err != nil {
		return fmt.Errorf("failed to create request: %w", err)
	}

	req.Header.Set("Content-Type", writer.FormDataContentType())

	// 添加认证头
	c.mu.RLock()
	if c.accessToken != "" {
		req.Header.Set("Authorization", "Bearer "+c.accessToken)
	}
	c.mu.RUnlock()

	// 发送请求
	resp, err := c.httpClient.Do(req)
	if err != nil {
		return fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	// 读取响应
	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("failed to read response: %w", err)
	}

	// 解析响应
	var apiResp models.ApiResponse[json.RawMessage]
	if err := json.Unmarshal(respBody, &apiResp); err != nil {
		return fmt.Errorf("failed to parse response: %w", err)
	}

	// 检查错误
	if apiResp.Code != 0 {
		return &APIError{Code: apiResp.Code, Message: apiResp.Message}
	}

	// 解析数据
	if result != nil && apiResp.Data != nil {
		if err := json.Unmarshal(apiResp.Data, result); err != nil {
			return fmt.Errorf("failed to parse data: %w", err)
		}
	}

	return nil
}
