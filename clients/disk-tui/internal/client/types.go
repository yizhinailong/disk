package client

import "encoding/json"

// CodeSuccess is the value of `code` for a successful API response.
const CodeSuccess = 0

// Envelope is the unified API envelope returned by the backend.
// Success: {"code": 0, "message": "success", "data": {...}}
// Error:   {"code": <errcode>, "message": "...", "data": null}
type Envelope[T any] struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
	Data    T      `json:"data"`
}

// Pagination mirrors backend disk::Pagination.
type Pagination struct {
	Page       int `json:"page"`
	PageSize   int `json:"page_size"`
	Total      int `json:"total"`
	TotalPages int `json:"total_pages"`
}

// Paged wraps a list response with items + pagination.
type Paged[T any] struct {
	Items      []T        `json:"items"`
	Pagination Pagination `json:"pagination"`
}

// User is the user profile shape returned across auth/user endpoints.
type User struct {
	ID           uint64 `json:"id"`
	Username     string `json:"username"`
	Email        string `json:"email"`
	Nickname     string `json:"nickname"`
	Avatar       string `json:"avatar"`
	Role         int    `json:"role,omitempty"`
	Status       int    `json:"status,omitempty"`
	StorageUsed  uint64 `json:"storage_used,omitempty"`
	StorageQuota uint64 `json:"storage_quota,omitempty"`
	FileCount    uint32 `json:"file_count,omitempty"`
	FolderCount  uint32 `json:"folder_count,omitempty"`
	CreatedAt    string `json:"created_at"`
	UpdatedAt    string `json:"updated_at,omitempty"`
	LastLoginAt  string `json:"last_login_at,omitempty"`
}

// RegisterResponse is the data payload of POST /api/auth/register.
type RegisterResponse struct {
	ID           uint64 `json:"id"`
	Username     string `json:"username"`
	Email        string `json:"email"`
	Nickname     string `json:"nickname"`
	StorageQuota uint64 `json:"storage_quota"`
	StorageUsed  uint64 `json:"storage_used"`
	CreatedAt    string `json:"created_at"`
}

// LoginResponse is the data payload of POST /api/auth/login.
type LoginResponse struct {
	AccessToken  string           `json:"access_token"`
	RefreshToken string           `json:"refresh_token"`
	TokenType    string           `json:"token_type"`
	ExpiresIn    int              `json:"expires_in"`
	User         RegisterResponse `json:"user"`
}

// RefreshResponse is the data payload of POST /api/auth/refresh.
type RefreshResponse struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ExpiresIn    int    `json:"expires_in"`
}

// StorageResponse mirrors disk::user::StorageResponse.
type StorageResponse struct {
	Used        uint64  `json:"used"`
	Quota       uint64  `json:"quota"`
	Percentage  float64 `json:"percentage"`
	FileCount   uint32  `json:"file_count"`
	FolderCount uint32  `json:"folder_count"`
}

// FileDetail mirrors GET /api/file/{file_id}.
type FileDetail struct {
	ID        uint64 `json:"id"`
	Name      string `json:"name"`
	Type      string `json:"type"`
	Size      int64  `json:"size"`
	Hash      string `json:"hash"`
	MimeType  string `json:"mime_type"`
	ParentID  uint64 `json:"parent_id"`
	Path      string `json:"path"`
	CreatedAt string `json:"created_at"`
	UpdatedAt string `json:"updated_at"`
}

// FileListItem mirrors disk::file::FileListItem.
type FileListItem struct {
	ID        uint64 `json:"id"`
	Name      string `json:"name"`
	Type      string `json:"type"`
	Size      uint64 `json:"size,omitempty"`
	MimeType  string `json:"mime_type,omitempty"`
	Hash      string `json:"hash,omitempty"`
	ItemCount int    `json:"item_count,omitempty"`
	CreatedAt string `json:"created_at"`
	UpdatedAt string `json:"updated_at"`
}

// SearchResultItem extends FileListItem with a path field.
type SearchResultItem struct {
	FileListItem
	Path string `json:"path"`
}

// FileListResponse mirrors disk::file::FileListResponse.
type FileListResponse = Paged[FileListItem]

// SearchResponse mirrors disk::file::SearchResponse.
type SearchResponse = Paged[SearchResultItem]

// InitUploadResponse mirrors disk::file::InitUploadResponse.
type InitUploadResponse struct {
	UploadID       string    `json:"upload_id"`
	ChunkSize      uint32    `json:"chunk_size"`
	TotalChunks    uint32    `json:"total_chunks"`
	UploadedChunks []uint32  `json:"uploaded_chunks"`
	InstantUpload  bool      `json:"instant_upload"`
	File           *FileItem `json:"file,omitempty"`
}

// FileItem mirrors disk::file::FileItem.
type FileItem struct {
	ID        uint64 `json:"id"`
	Name      string `json:"name"`
	Size      uint64 `json:"size"`
	Hash      string `json:"hash"`
	MimeType  string `json:"mime_type"`
	ParentID  uint64 `json:"parent_id"`
	CreatedAt string `json:"created_at"`
}

// UploadChunkResponse mirrors disk::file::UploadChunkResponse.
type UploadChunkResponse struct {
	ChunkIndex uint32 `json:"chunk_index"`
	Uploaded   bool   `json:"uploaded"`
}

// CompleteUploadResponse mirrors disk::file::CompleteUploadResponse.
type CompleteUploadResponse struct {
	File FileItem `json:"file"`
}

// DownloadInfoResponse mirrors disk::file::DownloadInfoResponse.
type DownloadInfoResponse struct {
	FileID        uint64 `json:"file_id"`
	Filename      string `json:"filename"`
	FileSize      uint64 `json:"file_size"`
	FileHash      string `json:"file_hash"`
	MimeType      string `json:"mime_type"`
	SupportsRange bool   `json:"supports_range"`
}

// RenameResponse mirrors disk::file::RenameResponse.
type RenameResponse struct {
	ID        uint64 `json:"id"`
	Name      string `json:"name"`
	UpdatedAt string `json:"updated_at"`
}

// FileIDMapping mirrors disk::file::FileIdMapping.
type FileIDMapping struct {
	OldID uint64 `json:"old_id"`
	NewID uint64 `json:"new_id"`
}

// MoveResponse mirrors disk::file::MoveResponse.
type MoveResponse struct {
	MovedCount       int `json:"moved_count"`
	MovedFileCount   int `json:"moved_file_count"`
	MovedFolderCount int `json:"moved_folder_count"`
}

// CopyResponse mirrors disk::file::CopyResponse.
type CopyResponse struct {
	CopiedCount       int             `json:"copied_count"`
	CopiedFileCount   int             `json:"copied_file_count"`
	CopiedFolderCount int             `json:"copied_folder_count"`
	NewFiles          []FileIDMapping `json:"new_files"`
	NewFolders        []FileIDMapping `json:"new_folders"`
}

// DeleteResponse mirrors disk::file::DeleteResponse.
type DeleteResponse struct {
	DeletedCount       int `json:"deleted_count"`
	DeletedFileCount   int `json:"deleted_file_count"`
	DeletedFolderCount int `json:"deleted_folder_count"`
}

// CreateFolderResponse mirrors disk::folder::CreateFolderResponse.
type CreateFolderResponse struct {
	ID        uint64 `json:"id"`
	Name      string `json:"name"`
	ParentID  uint64 `json:"parent_id"`
	Path      string `json:"path"`
	CreatedAt string `json:"created_at"`
}

// RenameFolderResponse mirrors disk::folder::RenameFolderResponse.
type RenameFolderResponse struct {
	ID        uint64 `json:"id"`
	Name      string `json:"name"`
	Path      string `json:"path"`
	UpdatedAt string `json:"updated_at"`
}

// FolderTreeNode mirrors disk::folder::FolderTreeNode.
type FolderTreeNode struct {
	ID       uint64           `json:"id"`
	Name     string           `json:"name"`
	Children []FolderTreeNode `json:"children"`
}

// BreadcrumbItem mirrors disk::folder::BreadcrumbItem.
type BreadcrumbItem struct {
	ID   uint64 `json:"id"`
	Name string `json:"name"`
}

// BreadcrumbResponse mirrors disk::folder::BreadcrumbResponse.
type BreadcrumbResponse struct {
	Path []BreadcrumbItem `json:"path"`
}

// ShareFile mirrors disk::share::ShareFile.
type ShareFile struct {
	ID        uint64 `json:"id"`
	Name      string `json:"name"`
	Type      string `json:"type"`
	Size      uint64 `json:"size"`
	ItemCount uint32 `json:"item_count,omitempty"`
}

// CreateShareResponse mirrors disk::share::CreateShareResponse.
type CreateShareResponse struct {
	ShareID    string  `json:"share_id"`
	ShareLink  string  `json:"share_link"`
	Password   *string `json:"password,omitempty"`
	Permission string  `json:"permission"`
	ExpiresAt  string  `json:"expires_at"`
	CreatedAt  string  `json:"created_at"`
}

// ShareItem mirrors disk::share::ShareItem.
type ShareItem struct {
	ShareID       string `json:"share_id"`
	FileName      string `json:"file_name"`
	FileCount     int    `json:"file_count"`
	ShareLink     string `json:"share_link"`
	HasPassword   bool   `json:"has_password"`
	Permission    string `json:"permission"`
	ViewCount     int    `json:"view_count"`
	DownloadCount int    `json:"download_count"`
	CreatedAt     string `json:"created_at"`
	ExpiresAt     string `json:"expires_at"`
	Status        string `json:"status"`
}

// ShareListResponse mirrors disk::share::ShareListResponse.
type ShareListResponse = Paged[ShareItem]

// ShareDetailResponse mirrors disk::share::ShareDetailResponse.
type ShareDetailResponse struct {
	ShareID       string      `json:"share_id"`
	Files         []ShareFile `json:"files"`
	ShareLink     string      `json:"share_link"`
	HasPassword   bool        `json:"has_password"`
	Permission    string      `json:"permission"`
	ViewCount     int         `json:"view_count"`
	DownloadCount int         `json:"download_count"`
	CreatedAt     string      `json:"created_at"`
	ExpiresAt     string      `json:"expires_at"`
	Status        string      `json:"status"`
}

// UpdateShareResponse mirrors disk::share::UpdateShareResponse.
type UpdateShareResponse struct {
	ShareID     string `json:"share_id"`
	ExpiresAt   string `json:"expires_at"`
	HasPassword bool   `json:"has_password"`
	Permission  string `json:"permission"`
	UpdatedAt   string `json:"updated_at"`
}

// CancelShareError mirrors disk::share::CancelShareError.
type CancelShareError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
	Reason  string `json:"reason"`
}

// CancelShareResult mirrors disk::share::CancelShareResult.
type CancelShareResult struct {
	ShareID string            `json:"share_id"`
	Status  string            `json:"status"`
	Error   *CancelShareError `json:"error,omitempty"`
}

// CancelShareSummary mirrors disk::share::CancelShareSummary.
type CancelShareSummary struct {
	Total     int `json:"total"`
	Succeeded int `json:"succeeded"`
	Failed    int `json:"failed"`
}

// CancelShareResponse mirrors disk::share::CancelShareResponse.
type CancelShareResponse struct {
	Summary CancelShareSummary  `json:"summary"`
	Results []CancelShareResult `json:"results"`
}

// AccessShareResponse mirrors disk::share::AccessShareResponse.
type AccessShareResponse struct {
	ShareToken string      `json:"share_token"`
	ExpiresIn  int         `json:"expires_in"`
	Permission string      `json:"permission"`
	Files      []ShareFile `json:"files"`
}

// BrowseItem mirrors disk::share::BrowseItem.
type BrowseItem struct {
	ID        uint64 `json:"id"`
	Name      string `json:"name"`
	Type      string `json:"type"`
	Size      uint64 `json:"size"`
	ItemCount uint32 `json:"item_count,omitempty"`
}

// BrowseBreadcrumb mirrors disk::share::BrowseBreadcrumb.
type BrowseBreadcrumb struct {
	ID   uint64 `json:"id"`
	Name string `json:"name"`
}

// BrowseShareResponse mirrors disk::share::BrowseShareResponse.
type BrowseShareResponse struct {
	Items      []BrowseItem       `json:"items"`
	Breadcrumb []BrowseBreadcrumb `json:"breadcrumb"`
}

// SaveShareItemsResponse mirrors disk::share::SaveShareItemsResponse.
type SaveShareItemsResponse struct {
	SavedCount       int `json:"saved_count"`
	SavedFileCount   int `json:"saved_file_count"`
	SavedFolderCount int `json:"saved_folder_count"`
}

// TrashItemResponse mirrors disk::trash::TrashItemResponse.
type TrashItemResponse struct {
	ID           uint64 `json:"id"`
	Type         string `json:"type"`
	OriginalID   uint64 `json:"original_id"`
	Name         string `json:"name"`
	Size         uint64 `json:"size"`
	OriginalPath string `json:"original_path"`
	DeletedAt    string `json:"deleted_at"`
	ExpiresAt    string `json:"expires_at"`
}

// TrashListResponse is the data shape for GET /api/trash.
type TrashListResponse = Paged[TrashItemResponse]

// BatchSummary mirrors disk::trash::BatchSummary.
type BatchSummary struct {
	Total        int `json:"total"`
	SuccessCount int `json:"success_count"`
	FailureCount int `json:"failure_count"`
}

// BatchResultItem mirrors disk::trash::BatchResultItem.
type BatchResultItem struct {
	TrashID    uint64  `json:"trash_id"`
	Status     string  `json:"status"`
	Code       *uint16 `json:"code,omitempty"`
	Message    *string `json:"message,omitempty"`
	Field      *string `json:"field,omitempty"`
	Value      *string `json:"value,omitempty"`
	FileID     *uint64 `json:"file_id,omitempty"`
	FolderID   *uint64 `json:"folder_id,omitempty"`
	Path       *string `json:"path,omitempty"`
	FreedSpace *uint64 `json:"freed_space,omitempty"`

	// raw error block (used when status=="failed")
	rawError json.RawMessage `json:"-"`
}

// BatchRestoreResponse mirrors disk::trash::BatchRestoreResponse.
type BatchRestoreResponse struct {
	Summary BatchSummary      `json:"summary"`
	Results []BatchResultItem `json:"results"`
}

// BatchDeleteResponse mirrors disk::trash::BatchDeleteResponse.
type BatchDeleteResponse = BatchRestoreResponse

// DeleteAllResponse mirrors disk::trash::DeleteAllResponse.
type DeleteAllResponse struct {
	DeletedCount int    `json:"deleted_count"`
	FreedSpace   uint64 `json:"freed_space"`
}

// AdminUserDetail mirrors disk::admin::UserDetailResponse.
type AdminUserDetail struct {
	ID              uint64 `json:"id"`
	Username        string `json:"username"`
	Email           string `json:"email"`
	Nickname        string `json:"nickname"`
	Avatar          string `json:"avatar"`
	Role            int    `json:"role"`
	Status          int    `json:"status"`
	StorageQuota    uint64 `json:"storage_quota"`
	StorageUsed     uint64 `json:"storage_used"`
	StorageReserved uint64 `json:"storage_reserved"`
	CreatedAt       string `json:"created_at"`
	LastLoginAt     string `json:"last_login_at"`
}

// AdminUserListResponse mirrors disk::admin::UserListResponse.
type AdminUserListResponse = Paged[AdminUserDetail]

// AdminStorageStats mirrors disk::admin::StorageStatsResponse.
type AdminStorageStats struct {
	TotalUsers        int    `json:"total_users"`
	TotalFiles        int    `json:"total_files"`
	TotalStorageUsed  uint64 `json:"total_storage_used"`
	TotalStorageQuota uint64 `json:"total_storage_quota"`
	ActiveShares      int    `json:"active_shares"`
}

// AdminSystemStatus mirrors disk::admin::SystemStatusResponse.
type AdminSystemStatus struct {
	DBConnected    bool   `json:"db_connected"`
	RedisConnected bool   `json:"redis_connected"`
	DiskTotal      uint64 `json:"disk_total"`
	DiskUsed       uint64 `json:"disk_used"`
	DiskFree       uint64 `json:"disk_free"`
	UptimeSeconds  uint64 `json:"uptime_seconds"`
}

// AdminShareDetail mirrors disk::admin::ShareDetailResponse.
type AdminShareDetail struct {
	ID          uint64 `json:"id"`
	UserID      uint64 `json:"user_id"`
	Username    string `json:"username"`
	FileID      uint64 `json:"file_id"`
	FileName    string `json:"file_name"`
	ShareCode   string `json:"share_code"`
	Status      int    `json:"status"`
	AccessCount int    `json:"access_count"`
	PasswordSet bool   `json:"password_set"`
	CreatedAt   string `json:"created_at"`
	ExpiresAt   string `json:"expires_at"`
}

// AdminShareListResponse mirrors disk::admin::ShareListResponse.
type AdminShareListResponse = Paged[AdminShareDetail]

// AdminLogDetail mirrors disk::admin::AdminLogDetailResponse.
type AdminLogDetail struct {
	ID         uint64  `json:"id"`
	UserID     uint64  `json:"user_id"`
	Action     string  `json:"action"`
	TargetType string  `json:"target_type"`
	TargetID   *uint64 `json:"target_id"`
	Details    *string `json:"details"`
	IPAddress  string  `json:"ip_address"`
	CreatedAt  string  `json:"created_at"`
}

// AdminLogListResponse mirrors disk::admin::AdminLogListResponse.
type AdminLogListResponse = Paged[AdminLogDetail]

// OperationLogItem mirrors disk::log::OperationLogItem.
type OperationLogItem struct {
	ID         uint64 `json:"id"`
	UserID     uint64 `json:"user_id"`
	Action     string `json:"action"`
	TargetType string `json:"target_type"`
	TargetID   uint64 `json:"target_id"`
	TargetName string `json:"target_name"`
	Details    string `json:"details"`
	IPAddress  string `json:"ip_address"`
	CreatedAt  string `json:"created_at"`
}

// OperationLogListResponse mirrors disk::log::OperationLogListResponse.
type OperationLogListResponse struct {
	Items []OperationLogItem `json:"items"`
	Total int                `json:"total"`
}

// SystemInfo mirrors disk::system::SystemInfo.
type SystemInfo struct {
	Version       string                `json:"version"`
	DrogonVersion string                `json:"drogon_version"`
	BuildTime     string                `json:"build_time"`
	Uptime        int64                 `json:"uptime"`
	Connections   SystemConnectionStats `json:"connections"`
	Storage       SystemStorageStats    `json:"storage"`
}

// SystemConnectionStats mirrors disk::system::ConnectionStats.
type SystemConnectionStats struct {
	Current       int64 `json:"current"`
	Peak          int64 `json:"peak"`
	DBPoolSize    int64 `json:"db_pool_size"`
	RedisPoolSize int64 `json:"redis_pool_size"`
}

// SystemStorageStats mirrors disk::system::StorageStats.
type SystemStorageStats struct {
	TotalUsers   int64 `json:"total_users"`
	TotalFiles   int64 `json:"total_files"`
	TotalFolders int64 `json:"total_folders"`
	TotalSize    int64 `json:"total_size"`
}

// HealthResponse mirrors GET /api/health.
type HealthResponse struct {
	Status  string `json:"status"`
	Version string `json:"version,omitempty"`
}
