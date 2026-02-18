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

	"github.com/liufeng/disk/ui/tui/internal/models"
)

// ShareAPI 分享 API
//
// 提供分享操作相关的 API 调用，包括创建分享、获取分享列表、取消分享等。
type ShareAPI struct {
	client *Client // API 客户端
}

// Create 创建分享
//
// 参数:
//   - ctx: 上下文
//   - req: 创建分享请求
//
// 返回:
//   - *models.CreateShareResponse: 分享信息（包含分享码和链接）
//   - error: 错误信息
func (s *ShareAPI) Create(ctx context.Context, req *models.CreateShareRequest) (*models.CreateShareResponse, error) {
	var resp models.CreateShareResponse
	if err := s.client.doRequest(ctx, "POST", "/api/share/create", req, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// List 获取分享列表
//
// 参数:
//   - ctx: 上下文
//   - page: 页码
//   - pageSize: 每页数量
//
// 返回:
//   - *models.ShareList: 分享列表
//   - error: 错误信息
func (s *ShareAPI) List(ctx context.Context, page, pageSize int) (*models.ShareList, error) {
	params := url.Values{}
	if page > 0 {
		params.Set("page", strconv.Itoa(page))
	}
	if pageSize > 0 {
		params.Set("page_size", strconv.Itoa(pageSize))
	}

	path := "/api/share/list"
	if len(params) > 0 {
		path += "?" + params.Encode()
	}

	var resp models.ShareList
	if err := s.client.doRequest(ctx, "GET", path, nil, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// Cancel 取消分享
//
// 参数:
//   - ctx: 上下文
//   - shareIDs: 分享 ID 列表
//
// 返回:
//   - error: 错误信息
func (s *ShareAPI) Cancel(ctx context.Context, shareIDs []string) error {
	body := map[string]any{"share_ids": shareIDs}
	return s.client.doRequest(ctx, "DELETE", "/api/share", body, nil)
}
