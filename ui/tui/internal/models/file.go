package models

import "time"

// FileType 文件类型
type FileType string

const (
	FileTypeFile   FileType = "file"
	FileTypeFolder FileType = "folder"
)

// File 文件/文件夹信息
type File struct {
	ID        uint64    `json:"id"`
	Name      string    `json:"name"`
	Type      FileType  `json:"type"`
	Size      uint64    `json:"size,omitempty"`       // 文件才有
	Hash      string    `json:"hash,omitempty"`       // 文件才有
	MimeType  string    `json:"mime_type,omitempty"`  // 文件才有
	ItemCount int       `json:"item_count,omitempty"` // 文件夹才有
	ParentID  uint64    `json:"parent_id"`
	Path      string    `json:"path,omitempty"`
	CreatedAt time.Time `json:"created_at"`
	UpdatedAt time.Time `json:"updated_at"`
}

// IsFolder 是否为文件夹
func (f *File) IsFolder() bool {
	return f.Type == FileTypeFolder
}

// FileList 文件列表响应
type FileList struct {
	Items      []File     `json:"items"`
	Pagination Pagination `json:"pagination"`
}

// Pagination 分页信息
type Pagination struct {
	Page       int `json:"page"`
	PageSize   int `json:"page_size"`
	Total      int `json:"total"`
	TotalPages int `json:"total_pages"`
}

// FileUploadInit 上传初始化请求
type FileUploadInit struct {
	Filename string `json:"filename"`
	FileSize uint64 `json:"file_size"`
	FileHash string `json:"file_hash"`
	ParentID uint64 `json:"parent_id,omitempty"`
}

// FileUploadInitResponse 上传初始化响应
type FileUploadInitResponse struct {
	UploadID       string `json:"upload_id,omitempty"`
	ChunkSize      int    `json:"chunk_size,omitempty"`
	UploadedChunks []int  `json:"uploaded_chunks,omitempty"`
	InstantUpload  bool   `json:"instant_upload"`
	File           *File  `json:"file,omitempty"`
}

// FileDownloadInfo 下载信息
type FileDownloadInfo struct {
	FileID   uint64 `json:"file_id"`
	Filename string `json:"filename"`
	FileSize uint64 `json:"file_size"`
	FileHash string `json:"file_hash"`
}
