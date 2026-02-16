package api

import (
	"context"
	"net/url"
	"strconv"

	"github.com/liufeng/disk/ui/tui/internal/models"
)

type ShareAPI struct {
	client *Client
}

func (s *ShareAPI) Create(ctx context.Context, req *models.CreateShareRequest) (*models.CreateShareResponse, error) {
	var resp models.CreateShareResponse
	if err := s.client.doRequest(ctx, "POST", "/api/share/create", req, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

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

func (s *ShareAPI) Cancel(ctx context.Context, shareIDs []string) error {
	body := map[string]any{"share_ids": shareIDs}
	return s.client.doRequest(ctx, "DELETE", "/api/share", body, nil)
}
