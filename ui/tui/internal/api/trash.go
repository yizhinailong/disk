package api

import (
	"context"
	"net/url"
	"strconv"

	"github.com/liufeng/disk/ui/tui/internal/models"
)

type TrashAPI struct {
	client *Client
}

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

func (t *TrashAPI) Restore(ctx context.Context, trashIDs []uint64) (*models.TrashRestoreResponse, error) {
	body := map[string]any{"trash_ids": trashIDs}

	var resp models.TrashRestoreResponse
	if err := t.client.doRequest(ctx, "POST", "/api/trash/restore", body, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

type DeleteResult struct {
	Summary struct {
		Total        int `json:"total"`
		SuccessCount int `json:"success_count"`
		FailureCount int `json:"failure_count"`
	} `json:"summary"`
	Results []models.TrashDeleteResult `json:"results"`
}

func (t *TrashAPI) Delete(ctx context.Context, trashIDs []uint64) (*DeleteResult, error) {
	body := map[string]any{"trash_ids": trashIDs}

	var resp DeleteResult
	if err := t.client.doRequest(ctx, "DELETE", "/api/trash", body, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}
