// Package models 数据模型定义
//
// 定义 TUI 客户端使用的所有数据结构，包括用户、文件、文件夹、分享和回收站等。
// 这些模型与后端 API 的 JSON 响应结构保持一致。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package models

import "time"

// Share 分享信息
//
// 包含分享链接的完整信息，包括访问统计和分享的文件列表。
type Share struct {
	ID            uint64     `json:"id"`                   // 分享 ID
	ShareCode     string     `json:"share_code"`           // 分享码（用于构建分享链接）
	HasPassword   bool       `json:"has_password"`         // 是否有访问密码
	Permission    string     `json:"permission"`           // 权限类型（view/download）
	ViewCount     int        `json:"view_count"`           // 浏览次数
	DownloadCount int        `json:"download_count"`       // 下载次数
	CreatedAt     time.Time  `json:"created_at"`           // 创建时间
	ExpiresAt     *time.Time `json:"expires_at,omitempty"` // 过期时间（nil 表示永久有效）
	Status        string     `json:"status"`               // 状态（active/expired/cancelled）
	Files         []File     `json:"files,omitempty"`      // 分享的文件列表
}

// CreateShareRequest 创建分享请求
type CreateShareRequest struct {
	FileIDs    []uint64 `json:"file_ids"`              // 要分享的文件/文件夹 ID 列表
	ExpireDays int      `json:"expire_days,omitempty"` // 有效期天数（0 或省略表示永久）
	Password   string   `json:"password,omitempty"`    // 访问密码（4-8 字符，空表示无密码）
	Permission string   `json:"permission,omitempty"`  // 权限（view/download，默认 view）
}

// CreateShareResponse 创建分享响应
type CreateShareResponse struct {
	ShareID   string     `json:"share_id"`             // 分享 ID
	ShareLink string     `json:"share_link"`           // 完整分享链接
	Password  string     `json:"password,omitempty"`   // 访问密码（如果有）
	ExpiresAt *time.Time `json:"expires_at,omitempty"` // 过期时间
}

// ShareList 分享列表
//
// 我的分享列表的分页响应。
type ShareList struct {
	Items      []Share    `json:"items"`      // 分享列表
	Pagination Pagination `json:"pagination"` // 分页信息
}
