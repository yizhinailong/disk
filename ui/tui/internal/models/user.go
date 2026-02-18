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

// User 用户信息
//
// 当前登录用户的完整信息。
type User struct {
	ID           uint64    `json:"id"`            // 用户 ID
	Username     string    `json:"username"`      // 用户名
	Email        string    `json:"email"`         // 邮箱
	Nickname     string    `json:"nickname"`      // 昵称
	Avatar       string    `json:"avatar"`        // 头像 URL
	StorageUsed  uint64    `json:"storage_used"`  // 已用存储空间（字节）
	StorageQuota uint64    `json:"storage_quota"` // 存储配额（字节）
	FileCount    int       `json:"file_count"`    // 文件数量
	FolderCount  int       `json:"folder_count"`  // 文件夹数量
	CreatedAt    time.Time `json:"created_at"`    // 注册时间
	UpdatedAt    time.Time `json:"updated_at"`    // 更新时间
}

// StorageStats 存储统计
//
// 存储空间使用情况的详细统计。
type StorageStats struct {
	Used       uint64            `json:"used"`       // 已用空间（字节）
	Quota      uint64            `json:"quota"`      // 总配额（字节）
	Percentage float64           `json:"percentage"` // 使用百分比
	Categories []StorageCategory `json:"categories"` // 分类统计
}

// StorageCategory 存储分类
//
// 按文件类型统计的存储使用情况。
type StorageCategory struct {
	Type  string `json:"type"`  // 文件类型（如 image、video、document）
	Size  uint64 `json:"size"`  // 该类型占用空间（字节）
	Count int    `json:"count"` // 该类型文件数量
}
