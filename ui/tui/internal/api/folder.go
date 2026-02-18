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

// FolderAPI 文件夹 API
//
// 提供文件夹操作相关的 API 调用，包括创建、树形结构、面包屑导航等。
type FolderAPI struct {
	client *Client // API 客户端
}

// Create 创建文件夹
//
// 参数:
//   - ctx: 上下文
//   - name: 文件夹名称
//   - parentID: 父文件夹 ID（0 表示根目录）
//
// 返回:
//   - *models.Folder: 创建的文件夹信息
//   - error: 错误信息
func (f *FolderAPI) Create(ctx context.Context, name string, parentID uint64) (*models.Folder, error) {
	body := &models.CreateFolderRequest{
		Name:     name,
		ParentID: parentID,
	}

	var resp models.Folder
	if err := f.client.doRequest(ctx, "POST", "/api/folder/create", body, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// Tree 获取文件夹树
//
// 参数:
//   - ctx: 上下文
//   - parentID: 起始文件夹 ID（0 表示根目录）
//   - depth: 深度（0 或负数表示全部）
//
// 返回:
//   - *models.FolderTree: 文件夹树
//   - error: 错误信息
func (f *FolderAPI) Tree(ctx context.Context, parentID uint64, depth int) (*models.FolderTree, error) {
	params := url.Values{}
	if parentID > 0 {
		params.Set("parent_id", strconv.FormatUint(parentID, 10))
	}
	if depth > 0 {
		params.Set("depth", strconv.Itoa(depth))
	}

	path := "/api/folder/tree"
	if len(params) > 0 {
		path += "?" + params.Encode()
	}

	var resp models.FolderTree
	if err := f.client.doRequest(ctx, "GET", path, nil, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// Breadcrumb 获取文件夹面包屑路径
//
// 参数:
//   - ctx: 上下文
//   - folderID: 文件夹 ID
//
// 返回:
//   - *models.Breadcrumb: 面包屑路径
//   - error: 错误信息
func (f *FolderAPI) Breadcrumb(ctx context.Context, folderID uint64) (*models.Breadcrumb, error) {
	path := "/api/folder/" + strconv.FormatUint(folderID, 10) + "/breadcrumb"

	var resp models.Breadcrumb
	if err := f.client.doRequest(ctx, "GET", path, nil, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}
