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

// TrashAPI 回收站 API
//
// 提供回收站操作相关的 API 调用，包括列表、恢复、彻底删除等。
type TrashAPI struct {
	client *Client // API 客户端
}

// List 获取回收站文件列表
//
// 参数:
//   - ctx: 上下文
//   - page: 页码
//   - pageSize: 每页数量
//
// 返回:
//   - *models.TrashList: 回收站文件列表
//   - error: 错误信息
func (t *TrashAPI) List(ctx context.Context, page, pageSize int) (*models.TrashList, error) {
	params := url.Values{}
	if page > 0 {
		params.Set("page", strconv.Itoa(page))
	}
	if pageSize > 0 {
		params.Set("page_size", strconv.Itoa(pageSize))
	}

	path := "/api/trash"
	if len(params) > 0 {
		path += "?" + params.Encode()
	}

	var resp models.TrashList
	if err := t.client.doRequest(ctx, "GET", path, nil, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// Restore 恢复回收站文件
//
// 参数:
//   - ctx: 上下文
//   - trashIDs: 回收站项 ID 列表
//
// 返回:
//   - *models.TrashRestoreResponse: 恢复结果
//   - error: 错误信息
func (t *TrashAPI) Restore(ctx context.Context, trashIDs []uint64) (*models.TrashRestoreResponse, error) {
	body := map[string]any{"trash_ids": trashIDs}

	var resp models.TrashRestoreResponse
	if err := t.client.doRequest(ctx, "POST", "/api/trash/restore", body, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// DeleteResult 彻底删除结果
type DeleteResult struct {
	Summary struct {
		Total        int `json:"total"`         // 总数
		SuccessCount int `json:"success_count"` // 成功数
		FailureCount int `json:"failure_count"` // 失败数
	} `json:"summary"` // 统计摘要
	Results []models.TrashDeleteResult `json:"results"` // 详细结果
}

// Delete 彻底删除回收站文件
//
// 参数:
//   - ctx: 上下文
//   - trashIDs: 回收站项 ID 列表
//
// 返回:
//   - *DeleteResult: 删除结果
//   - error: 错误信息
func (t *TrashAPI) Delete(ctx context.Context, trashIDs []uint64) (*DeleteResult, error) {
	body := map[string]any{"trash_ids": trashIDs}

	var resp DeleteResult
	if err := t.client.doRequest(ctx, "DELETE", "/api/trash", body, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}
