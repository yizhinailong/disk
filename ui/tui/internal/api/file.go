// Package api API 客户端模块
//
// 提供与后端 API 通信的能力，包括认证、文件管理、分享等功能。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package api

import (
	"context"
	"net/url"
	"strconv"

	"github.com/yizhinailong/disk/ui/tui/internal/models"
)

// FileAPI 文件 API
//
// 提供文件操作相关的 API 调用，包括列表、重命名、移动、复制、删除等。
type FileAPI struct {
	client *Client // API 客户端
}

// ListOptions 文件列表查询选项
type ListOptions struct {
	ParentID  uint64 // 父文件夹 ID（0 表示根目录）
	Page      int    // 页码
	PageSize  int    // 每页数量
	SortBy    string // 排序字段
	SortOrder string // 排序方向（asc/desc）
	Type      string // 文件类型过滤
}

// List 获取文件列表
//
// 参数:
//   - ctx: 上下文
//   - opts: 查询选项
//
// 返回:
//   - *models.FileList: 文件列表
//   - error: 错误信息
func (f *FileAPI) List(ctx context.Context, opts ListOptions) (*models.FileList, error) {
	params := url.Values{}
	if opts.ParentID > 0 {
		params.Set("parent_id", strconv.FormatUint(opts.ParentID, 10))
	}
	if opts.Page > 0 {
		params.Set("page", strconv.Itoa(opts.Page))
	}
	if opts.PageSize > 0 {
		params.Set("page_size", strconv.Itoa(opts.PageSize))
	}
	if opts.SortBy != "" {
		params.Set("sort_by", opts.SortBy)
	}
	if opts.SortOrder != "" {
		params.Set("sort_order", opts.SortOrder)
	}
	if opts.Type != "" {
		params.Set("type", opts.Type)
	}

	path := "/api/file/list"
	if len(params) > 0 {
		path += "?" + params.Encode()
	}

	var resp models.FileList
	if err := f.client.doRequest(ctx, "GET", path, nil, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// Rename 重命名文件
//
// 参数:
//   - ctx: 上下文
//   - fileID: 文件 ID
//   - newName: 新文件名
//
// 返回:
//   - *models.File: 更新后的文件信息
//   - error: 错误信息
func (f *FileAPI) Rename(ctx context.Context, fileID uint64, newName string) (*models.File, error) {
	path := "/api/file/" + strconv.FormatUint(fileID, 10) + "/rename"
	body := map[string]string{"new_name": newName}

	var resp models.File
	if err := f.client.doRequest(ctx, "PUT", path, body, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// Move 移动文件到目标文件夹
//
// 参数:
//   - ctx: 上下文
//   - fileIDs: 文件 ID 列表
//   - targetFolderID: 目标文件夹 ID
//
// 返回:
//   - error: 错误信息
func (f *FileAPI) Move(ctx context.Context, fileIDs []uint64, targetFolderID uint64) error {
	body := map[string]any{
		"file_ids":         fileIDs,
		"target_folder_id": targetFolderID,
	}
	return f.client.doRequest(ctx, "PUT", "/api/file/move", body, nil)
}

// CopyResult 复制操作结果
type CopyResult struct {
	CopiedCount int `json:"copied_count"` // 复制成功数量
	NewFiles    []struct {
		OldID uint64 `json:"old_id"` // 原文件 ID
		NewID uint64 `json:"new_id"` // 新文件 ID
	} `json:"new_files"` // 新文件列表
}

// Copy 复制文件到目标文件夹
//
// 参数:
//   - ctx: 上下文
//   - fileIDs: 文件 ID 列表
//   - targetFolderID: 目标文件夹 ID
//
// 返回:
//   - *CopyResult: 复制结果
//   - error: 错误信息
func (f *FileAPI) Copy(ctx context.Context, fileIDs []uint64, targetFolderID uint64) (*CopyResult, error) {
	body := map[string]any{
		"file_ids":         fileIDs,
		"target_folder_id": targetFolderID,
	}

	var resp CopyResult
	if err := f.client.doRequest(ctx, "POST", "/api/file/copy", body, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// Delete 删除文件（移入回收站）
//
// 参数:
//   - ctx: 上下文
//   - fileIDs: 文件 ID 列表
//
// 返回:
//   - error: 错误信息
func (f *FileAPI) Delete(ctx context.Context, fileIDs []uint64) error {
	body := map[string]any{"file_ids": fileIDs}
	return f.client.doRequest(ctx, "DELETE", "/api/file", body, nil)
}

// InitUpload 初始化文件上传
//
// 参数:
//   - ctx: 上下文
//   - req: 上传初始化请求
//
// 返回:
//   - *models.FileUploadInitResponse: 上传初始化响应（包含分片信息）
//   - error: 错误信息
func (f *FileAPI) InitUpload(ctx context.Context, req *models.FileUploadInit) (*models.FileUploadInitResponse, error) {
	var resp models.FileUploadInitResponse
	if err := f.client.doRequest(ctx, "POST", "/api/file/upload/init", req, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// DownloadURL 获取文件下载 URL
//
// 参数:
//   - fileID: 文件 ID
//
// 返回:
//   - string: 下载 URL
func (f *FileAPI) DownloadURL(fileID uint64) string {
	return f.client.config.Server.URL + "/api/file/download/" + strconv.FormatUint(fileID, 10)
}
