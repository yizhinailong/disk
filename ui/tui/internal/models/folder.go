package models

// Folder 文件夹
type Folder struct {
	ID        uint64 `json:"id"`
	Name      string `json:"name"`
	ParentID  uint64 `json:"parent_id"`
	Path      string `json:"path"`
	Depth     int    `json:"depth"`
	ItemCount int    `json:"item_count"`
}

// FolderTree 目录树节点
type FolderTree struct {
	ID       uint64       `json:"id"`
	Name     string       `json:"name"`
	Children []FolderTree `json:"children"`
}

// BreadcrumbItem 面包屑项
type BreadcrumbItem struct {
	ID   uint64 `json:"id"`
	Name string `json:"name"`
}

// Breadcrumb 面包屑路径
type Breadcrumb struct {
	Path []BreadcrumbItem `json:"path"`
}

// CreateFolderRequest 创建文件夹请求
type CreateFolderRequest struct {
	Name     string `json:"name"`
	ParentID uint64 `json:"parent_id,omitempty"`
}
