package api

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sync"
	"time"

	"github.com/liufeng/disk/ui/tui/internal/config"
	"github.com/liufeng/disk/ui/tui/internal/models"
	"github.com/liufeng/disk/ui/tui/internal/store"
)

// Client API 客户端
type Client struct {
	httpClient *http.Client
	config     *config.Config
	tokenStore *store.TokenStore

	// Token 管理
	mu           sync.RWMutex
	accessToken  string
	refreshToken string
	tokenExpiry  time.Time

	// 子模块
	Auth   *AuthAPI
	File   *FileAPI
	Folder *FolderAPI
	Trash  *TrashAPI
	Share  *ShareAPI
}

// NewClient 创建 API 客户端
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

// LoadToken 从存储加载 Token
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

// SaveToken 保存 Token 到存储
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

// SetToken 设置 Token
func (c *Client) SetToken(accessToken, refreshToken string, expiresIn int) {
	c.mu.Lock()
	c.accessToken = accessToken
	c.refreshToken = refreshToken
	c.tokenExpiry = time.Now().Add(time.Duration(expiresIn) * time.Second)
	c.mu.Unlock()
}

// ClearToken 清除 Token
func (c *Client) ClearToken() {
	c.mu.Lock()
	c.accessToken = ""
	c.refreshToken = ""
	c.tokenExpiry = time.Time{}
	c.mu.Unlock()
}

// IsLoggedIn 检查是否已登录
func (c *Client) IsLoggedIn() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.accessToken != "" && time.Now().Before(c.tokenExpiry)
}

// NeedRefresh 检查是否需要刷新 Token（5 分钟内过期）
func (c *Client) NeedRefresh() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.refreshToken != "" && time.Now().Add(5*time.Minute).After(c.tokenExpiry)
}

// GetAccessToken 获取当前 Access Token（用于下载等需要直接使用 token 的场景）
func (c *Client) GetAccessToken() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.accessToken
}

// doRequest 执行 HTTP 请求
func (c *Client) doRequest(ctx context.Context, method, path string, body, result any) error {
	// 检查并刷新 Token（仅当需要认证时）
	if c.NeedRefresh() {
		if err := c.Auth.Refresh(ctx); err != nil {
			return fmt.Errorf("刷新令牌失败: %w", err)
		}
	}

	// 构建请求
	var reqBody io.Reader
	if body != nil {
		data, err := json.Marshal(body)
		if err != nil {
			return fmt.Errorf("序列化请求失败: %w", err)
		}
		reqBody = bytes.NewReader(data)
	}

	url := c.config.Server.URL + path
	req, err := http.NewRequestWithContext(ctx, method, url, reqBody)
	if err != nil {
		return fmt.Errorf("创建请求失败: %w", err)
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
		return fmt.Errorf("请求失败: %w", err)
	}
	defer resp.Body.Close()

	// 读取响应
	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("读取响应失败: %w", err)
	}

	// 解析响应
	var apiResp models.ApiResponse[json.RawMessage]
	if err := json.Unmarshal(respBody, &apiResp); err != nil {
		return fmt.Errorf("解析响应失败: %w", err)
	}

	// 检查错误
	if apiResp.Code != 0 {
		return &APIError{Code: apiResp.Code, Message: apiResp.Message}
	}

	// 解析数据
	if result != nil && apiResp.Data != nil {
		if err := json.Unmarshal(apiResp.Data, result); err != nil {
			return fmt.Errorf("解析数据失败: %w", err)
		}
	}

	return nil
}

// APIError API 错误
type APIError struct {
	Code    int
	Message string
}

func (e *APIError) Error() string {
	return fmt.Sprintf("API错误(%d): %s", e.Code, e.Message)
}

// IsAuthError 检查是否为认证错误
func (e *APIError) IsAuthError() bool {
	return e.Code >= 40100 && e.Code < 40200
}

// IsTokenExpired 检查是否为 Token 过期错误
func (e *APIError) IsTokenExpired() bool {
	return e.Code == 40108
}

// IsRefreshTokenUsed 检查是否为 Refresh Token 已使用错误
func (e *APIError) IsRefreshTokenUsed() bool {
	return e.Code == 40110
}
