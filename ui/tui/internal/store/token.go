// Package store Token 存储模块
//
// 提供 JWT Token 的本地文件存储功能。
// 支持持久化到本地文件系统。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package store

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

// TokenData 令牌数据结构
//
// 存储访问令牌、刷新令牌和过期时间。
type TokenData struct {
	AccessToken  string `json:"access_token"`  // 访问令牌
	RefreshToken string `json:"refresh_token"` // 刷新令牌
	ExpiresAt    int64  `json:"expires_at"`    // 过期时间（Unix 时间戳）
}

// TokenStore 令牌存储
//
// 提供令牌持久化存储。
type TokenStore struct {
	filePath string // 令牌文件路径
}

// NewTokenStore 创建令牌存储
//
// 参数:
//   - filePath: 令牌文件存储路径
//
// 返回:
//   - *TokenStore: 令牌存储实例
func NewTokenStore(filePath string) *TokenStore {
	return &TokenStore{
		filePath: filePath,
	}
}

// Save 保存令牌
//
// 将令牌数据序列化为 JSON 并存储到文件。
// 文件权限设置为 0600，仅所有者可读写。
//
// 参数:
//   - data: 令牌数据
//
// 返回:
//   - error: 错误信息
func (s *TokenStore) Save(data *TokenData) error {
	// 序列化
	plaintext, err := json.Marshal(data)
	if err != nil {
		return fmt.Errorf("failed to serialize token: %w", err)
	}

	// 确保目录存在
	dir := filepath.Dir(s.filePath)
	if err := os.MkdirAll(dir, 0700); err != nil {
		return fmt.Errorf("failed to create directory: %w", err)
	}

	// 写入文件（权限 600）
	if err := os.WriteFile(s.filePath, plaintext, 0600); err != nil {
		return fmt.Errorf("failed to write file: %w", err)
	}

	return nil
}

// Load 加载令牌
//
// 从文件读取令牌数据。如果文件不存在，返回 nil（表示未登录）。
//
// 返回:
//   - *TokenData: 令牌数据（文件不存在时为 nil）
//   - error: 错误信息
func (s *TokenStore) Load() (*TokenData, error) {
	data, err := os.ReadFile(s.filePath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil // 文件不存在，返回 nil（未登录）
		}
		return nil, fmt.Errorf("failed to read file: %w", err)
	}

	// 反序列化
	var tokenData TokenData
	if err := json.Unmarshal(data, &tokenData); err != nil {
		return nil, fmt.Errorf("failed to deserialize: %w", err)
	}

	return &tokenData, nil
}

// Delete 删除令牌文件
//
// 删除存储的令牌文件。如果文件不存在，不返回错误。
//
// 返回:
//   - error: 删除错误（文件不存在时为 nil）
func (s *TokenStore) Delete() error {
	if err := os.Remove(s.filePath); err != nil && !os.IsNotExist(err) {
		return err
	}
	return nil
}

// Exists 检查令牌文件是否存在
//
// 返回:
//   - bool: true 表示文件存在，false 表示不存在
func (s *TokenStore) Exists() bool {
	_, err := os.Stat(s.filePath)
	return err == nil
}
