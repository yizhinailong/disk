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

// TrashItem 回收站项目
//
// 软删除的文件/文件夹，可在过期前恢复。
type TrashItem struct {
	ID           uint64    `json:"id"`            // 回收站项目 ID
	OriginalID   uint64    `json:"original_id"`   // 原文件/文件夹 ID
	Name         string    `json:"name"`          // 文件/文件夹名称
	Type         FileType  `json:"type"`          // 类型（file/folder）
	Size         uint64    `json:"size"`          // 大小（字节）
	OriginalPath string    `json:"original_path"` // 原始路径
	DeletedAt    time.Time `json:"deleted_at"`    // 删除时间
	ExpiresAt    time.Time `json:"expires_at"`    // 过期时间（30 天后自动清理）
}

// TrashList 回收站列表
type TrashList struct {
	Items      []TrashItem `json:"items"`      // 回收站项目列表
	Pagination Pagination  `json:"pagination"` // 分页信息
}

// TrashRestoreResult 单项恢复结果
type TrashRestoreResult struct {
	TrashID uint64 `json:"trash_id"`          // 回收站项目 ID
	Status  string `json:"status"`            // 状态（success/failed）
	FileID  uint64 `json:"file_id,omitempty"` // 恢复后的文件 ID（成功时）
	Path    string `json:"path,omitempty"`    // 恢复后的路径（成功时）
	Error   *Error `json:"error,omitempty"`   // 错误信息（失败时）
}

// TrashRestoreSummary 恢复摘要
type TrashRestoreSummary struct {
	Total        int `json:"total"`         // 总数
	SuccessCount int `json:"success_count"` // 成功数
	FailureCount int `json:"failure_count"` // 失败数
}

// TrashRestoreResponse 批量恢复响应
type TrashRestoreResponse struct {
	Summary TrashRestoreSummary  `json:"summary"` // 恢复摘要
	Results []TrashRestoreResult `json:"results"` // 各项恢复结果
}

// TrashDeleteResult 彻底删除结果
type TrashDeleteResult struct {
	TrashID    uint64 `json:"trash_id,omitempty"`    // 回收站项目 ID
	Status     string `json:"status"`                // 状态（success/failed）
	FreedSpace uint64 `json:"freed_space,omitempty"` // 释放空间（字节）
	Error      *Error `json:"error,omitempty"`       // 错误信息
}
