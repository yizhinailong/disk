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

// FileType 文件类型枚举
type FileType string

const (
	FileTypeFile   FileType = "file"   // 文件类型
	FileTypeFolder FileType = "folder" // 文件夹类型
)

// File 文件/文件夹信息
//
// 用于表示文件系统中的文件或文件夹。Size、Hash、MimeType 仅对文件有效，
// ItemCount 仅对文件夹有效。
type File struct {
	ID        uint64    `json:"id"`                   // 文件 ID
	Name      string    `json:"name"`                 // 文件名
	Type      FileType  `json:"type"`                 // 文件类型（file/folder）
	Size      uint64    `json:"size,omitempty"`       // 文件大小（仅文件）
	Hash      string    `json:"hash,omitempty"`       // 文件哈希（仅文件）
	MimeType  string    `json:"mime_type,omitempty"`  // MIME 类型（仅文件）
	ItemCount int       `json:"item_count,omitempty"` // 子项数量（仅文件夹）
	ParentID  uint64    `json:"parent_id"`            // 父文件夹 ID（0 表示根目录）
	Path      string    `json:"path,omitempty"`       // 完整路径
	CreatedAt time.Time `json:"created_at"`           // 创建时间
	UpdatedAt time.Time `json:"updated_at"`           // 更新时间
}

// IsFolder 判断是否为文件夹
func (f *File) IsFolder() bool {
	return f.Type == FileTypeFolder
}

// FileList 文件列表响应
//
// 用于文件列表查询的分页响应。
type FileList struct {
	Items      []File     `json:"items"`      // 文件列表
	Pagination Pagination `json:"pagination"` // 分页信息
}

// Pagination 分页信息
type Pagination struct {
	Page       int `json:"page"`        // 当前页码（从 1 开始）
	PageSize   int `json:"page_size"`   // 每页数量
	Total      int `json:"total"`       // 总记录数
	TotalPages int `json:"total_pages"` // 总页数
}

// FileUploadInit 上传初始化请求
type FileUploadInit struct {
	Filename string `json:"filename"`            // 文件名
	FileSize uint64 `json:"file_size"`           // 文件大小（字节）
	FileHash string `json:"file_hash"`           // 文件 SHA256 哈希
	ParentID uint64 `json:"parent_id,omitempty"` // 目标文件夹 ID（0 表示根目录）
}

// FileUploadInitResponse 上传初始化响应
type FileUploadInitResponse struct {
	UploadID       string `json:"upload_id,omitempty"`       // 上传任务 ID（用于分片上传）
	ChunkSize      int    `json:"chunk_size,omitempty"`      // 分片大小（字节）
	UploadedChunks []int  `json:"uploaded_chunks,omitempty"` // 已上传的分片索引
	InstantUpload  bool   `json:"instant_upload"`            // 是否秒传（文件已存在）
	File           *File  `json:"file,omitempty"`            // 秒传时的文件信息
}

// FileDownloadInfo 下载信息
type FileDownloadInfo struct {
	FileID   uint64 `json:"file_id"`   // 文件 ID
	Filename string `json:"filename"`  // 文件名
	FileSize uint64 `json:"file_size"` // 文件大小（字节）
	FileHash string `json:"file_hash"` // 文件 SHA256 哈希
}
