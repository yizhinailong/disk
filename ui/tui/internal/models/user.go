package models

import "time"

// User 用户信息
type User struct {
	ID           uint64    `json:"id"`
	Username     string    `json:"username"`
	Email        string    `json:"email"`
	Nickname     string    `json:"nickname"`
	Avatar       string    `json:"avatar"`
	StorageUsed  uint64    `json:"storage_used"`
	StorageQuota uint64    `json:"storage_quota"`
	FileCount    int       `json:"file_count"`
	FolderCount  int       `json:"folder_count"`
	CreatedAt    time.Time `json:"created_at"`
	UpdatedAt    time.Time `json:"updated_at"`
}

// StorageStats 存储统计
type StorageStats struct {
	Used       uint64            `json:"used"`
	Quota      uint64            `json:"quota"`
	Percentage float64           `json:"percentage"`
	Categories []StorageCategory `json:"categories"`
}

// StorageCategory 存储分类
type StorageCategory struct {
	Type  string `json:"type"`
	Size  uint64 `json:"size"`
	Count int    `json:"count"`
}
