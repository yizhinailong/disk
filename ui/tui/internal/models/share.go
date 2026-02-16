package models

import "time"

// Share 分享信息
type Share struct {
	ID            uint64     `json:"id"`
	ShareCode     string     `json:"share_code"`
	HasPassword   bool       `json:"has_password"`
	Permission    string     `json:"permission"` // view, download
	ViewCount     int        `json:"view_count"`
	DownloadCount int        `json:"download_count"`
	CreatedAt     time.Time  `json:"created_at"`
	ExpiresAt     *time.Time `json:"expires_at,omitempty"`
	Status        string     `json:"status"` // active, expired, cancelled
	Files         []File     `json:"files,omitempty"`
}

// CreateShareRequest 创建分享请求
type CreateShareRequest struct {
	FileIDs    []uint64 `json:"file_ids"`
	ExpireDays int      `json:"expire_days,omitempty"`
	Password   string   `json:"password,omitempty"`
	Permission string   `json:"permission,omitempty"`
}

// CreateShareResponse 创建分享响应
type CreateShareResponse struct {
	ShareID   string     `json:"share_id"`
	ShareLink string     `json:"share_link"`
	Password  string     `json:"password,omitempty"`
	ExpiresAt *time.Time `json:"expires_at,omitempty"`
}

// ShareList 分享列表
type ShareList struct {
	Items      []Share    `json:"items"`
	Pagination Pagination `json:"pagination"`
}
