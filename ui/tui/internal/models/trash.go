package models

import "time"

// TrashItem 回收站项目
type TrashItem struct {
	ID           uint64    `json:"id"`
	OriginalID   uint64    `json:"original_id"`
	Name         string    `json:"name"`
	Type         FileType  `json:"type"`
	Size         uint64    `json:"size"`
	OriginalPath string    `json:"original_path"`
	DeletedAt    time.Time `json:"deleted_at"`
	ExpiresAt    time.Time `json:"expires_at"`
}

// TrashList 回收站列表
type TrashList struct {
	Items      []TrashItem `json:"items"`
	Pagination Pagination  `json:"pagination"`
}

// TrashRestoreResult 恢复结果
type TrashRestoreResult struct {
	TrashID uint64 `json:"trash_id"`
	Status  string `json:"status"` // success, failed
	FileID  uint64 `json:"file_id,omitempty"`
	Path    string `json:"path,omitempty"`
	Error   *Error `json:"error,omitempty"`
}

// TrashRestoreSummary 恢复摘要
type TrashRestoreSummary struct {
	Total        int `json:"total"`
	SuccessCount int `json:"success_count"`
	FailureCount int `json:"failure_count"`
}

// TrashRestoreResponse 恢复响应
type TrashRestoreResponse struct {
	Summary TrashRestoreSummary  `json:"summary"`
	Results []TrashRestoreResult `json:"results"`
}

// TrashDeleteResult 彻底删除结果
type TrashDeleteResult struct {
	TrashID    uint64 `json:"trash_id"`
	Status     string `json:"status"`
	FreedSpace uint64 `json:"freed_space,omitempty"`
	Error      *Error `json:"error,omitempty"`
}
