package client

import (
	"context"
	"net/http"
	"net/url"
	"strconv"
)

// ListOperationLogs calls GET /api/logs.
func (c *Client) ListOperationLogs(ctx context.Context, page, pageSize int) (OperationLogListResponse, error) {
	q := url.Values{}
	if page > 0 {
		q.Set("page", strconv.Itoa(page))
	}
	if pageSize > 0 {
		q.Set("page_size", strconv.Itoa(pageSize))
	}
	var out OperationLogListResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/logs", requestOpts{query: q}, &out)
	return out, err
}

// GetSystemInfo calls GET /api/system/info.
func (c *Client) GetSystemInfo(ctx context.Context) (SystemInfo, error) {
	var out SystemInfo
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/system/info", requestOpts{}, &out)
	return out, err
}
