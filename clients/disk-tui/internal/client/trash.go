package client

import (
	"context"
	"net/http"
	"net/url"
	"strconv"
)

// ListTrash calls GET /api/trash.
func (c *Client) ListTrash(ctx context.Context, page, pageSize int) (TrashListResponse, error) {
	q := url.Values{}
	if page > 0 {
		q.Set("page", strconv.Itoa(page))
	}
	if pageSize > 0 {
		q.Set("page_size", strconv.Itoa(pageSize))
	}
	var out TrashListResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/trash", requestOpts{query: q}, &out)
	return out, err
}

// RestoreTrash calls POST /api/trash/restore.
func (c *Client) RestoreTrash(ctx context.Context, trashIDs []uint64) (BatchRestoreResponse, error) {
	body, ct, err := writeJSON(map[string]any{"trash_ids": orEmpty(trashIDs)})
	if err != nil {
		return BatchRestoreResponse{}, err
	}
	var out BatchRestoreResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/trash/restore", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// DeleteTrash calls DELETE /api/trash (body: {trash_ids}).
func (c *Client) DeleteTrash(ctx context.Context, trashIDs []uint64) (BatchDeleteResponse, error) {
	body, ct, err := writeJSON(map[string]any{"trash_ids": orEmpty(trashIDs)})
	if err != nil {
		return BatchDeleteResponse{}, err
	}
	var out BatchDeleteResponse
	err = c.decodeEnvelope(ctx, http.MethodDelete, "/api/trash", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// DeleteTrashAlt calls POST /api/trash/delete.
func (c *Client) DeleteTrashAlt(ctx context.Context, trashIDs []uint64) (BatchDeleteResponse, error) {
	body, ct, err := writeJSON(map[string]any{"trash_ids": orEmpty(trashIDs)})
	if err != nil {
		return BatchDeleteResponse{}, err
	}
	var out BatchDeleteResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/trash/delete", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// EmptyTrash calls DELETE /api/trash/all.
func (c *Client) EmptyTrash(ctx context.Context) (DeleteAllResponse, error) {
	var out DeleteAllResponse
	err := c.decodeEnvelope(ctx, http.MethodDelete, "/api/trash/all", requestOpts{}, &out)
	return out, err
}
