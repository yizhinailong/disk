// Package models 数据模型定义
//
// 定义 TUI 客户端使用的所有数据结构，包括用户、文件、文件夹、分享和回收站等。
// 这些模型与后端 API 的 JSON 响应结构保持一致。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package models

// Folder 文件夹信息
//
// 用于文件夹详情展示和导航。
type Folder struct {
	ID        uint64 `json:"id"`         // 文件夹 ID
	Name      string `json:"name"`       // 文件夹名称
	ParentID  uint64 `json:"parent_id"`  // 父文件夹 ID（0 表示根目录）
	Path      string `json:"path"`       // 完整路径
	Depth     int    `json:"depth"`      // 目录深度（根目录为 0）
	ItemCount int    `json:"item_count"` // 子项数量（文件 + 文件夹）
}

// FolderTree 目录树节点
//
// 用于构建可视化的目录树结构。
type FolderTree struct {
	ID       uint64       `json:"id"`       // 文件夹 ID
	Name     string       `json:"name"`     // 文件夹名称
	Children []FolderTree `json:"children"` // 子文件夹列表
}

// BreadcrumbItem 面包屑项
//
// 用于路径导航的单一层级信息。
type BreadcrumbItem struct {
	ID   uint64 `json:"id"`   // 文件夹 ID
	Name string `json:"name"` // 文件夹名称
}

// Breadcrumb 面包屑路径
//
// 完整的导航路径，从根目录到当前文件夹。
type Breadcrumb struct {
	Path []BreadcrumbItem `json:"path"` // 路径项列表（从根到当前）
}

// CreateFolderRequest 创建文件夹请求
type CreateFolderRequest struct {
	Name     string `json:"name"`                // 文件夹名称
	ParentID uint64 `json:"parent_id,omitempty"` // 父文件夹 ID（0 或省略表示根目录）
}
