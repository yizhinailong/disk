package client

import (
	"context"
	"fmt"
	"net/http"
	"net/url"
	"strconv"
)

// AdminListUsersParams are the query parameters for GET /api/admin/users.
type AdminListUsersParams struct {
	Page     int
	PageSize int
	Username string
	Email    string
	Status   *int
	Role     *int
}

// AdminListUsers calls GET /api/admin/users.
func (c *Client) AdminListUsers(ctx context.Context, p AdminListUsersParams) (AdminUserListResponse, error) {
	q := url.Values{}
	if p.Page > 0 {
		q.Set("page", strconv.Itoa(p.Page))
	}
	if p.PageSize > 0 {
		q.Set("page_size", strconv.Itoa(p.PageSize))
	}
	if p.Username != "" {
		q.Set("username", p.Username)
	}
	if p.Email != "" {
		q.Set("email", p.Email)
	}
	if p.Status != nil {
		q.Set("status", strconv.Itoa(*p.Status))
	}
	if p.Role != nil {
		q.Set("role", strconv.Itoa(*p.Role))
	}
	var out AdminUserListResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/admin/users", requestOpts{query: q}, &out)
	return out, err
}

// AdminGetUser calls GET /api/admin/users/{id}.
func (c *Client) AdminGetUser(ctx context.Context, userID uint64) (AdminUserDetail, error) {
	var out AdminUserDetail
	err := c.decodeEnvelope(ctx, http.MethodGet, fmt.Sprintf("/api/admin/users/%d", userID), requestOpts{}, &out)
	return out, err
}

// AdminChangeUserStatus calls PUT /api/admin/users/{id}/status.
func (c *Client) AdminChangeUserStatus(ctx context.Context, userID uint64, status int) (AdminUserDetail, error) {
	body, ct, err := writeJSON(map[string]int{"status": status})
	if err != nil {
		return AdminUserDetail{}, err
	}
	var out AdminUserDetail
	err = c.decodeEnvelope(ctx, http.MethodPut, fmt.Sprintf("/api/admin/users/%d/status", userID), requestOpts{
		body: body, contentType: ct,
	}, &out)
	return out, err
}

// AdminChangeUserRole calls PUT /api/admin/users/{id}/role.
func (c *Client) AdminChangeUserRole(ctx context.Context, userID uint64, role int) (AdminUserDetail, error) {
	body, ct, err := writeJSON(map[string]int{"role": role})
	if err != nil {
		return AdminUserDetail{}, err
	}
	var out AdminUserDetail
	err = c.decodeEnvelope(ctx, http.MethodPut, fmt.Sprintf("/api/admin/users/%d/role", userID), requestOpts{
		body: body, contentType: ct,
	}, &out)
	return out, err
}

// AdminChangeUserAvailableSpace calls PUT /api/admin/users/{id}/available-space.
func (c *Client) AdminChangeUserAvailableSpace(ctx context.Context, userID uint64, availableSpaceG uint64) (AdminUserDetail, error) {
	body, ct, err := writeJSON(map[string]uint64{"available_space_g": availableSpaceG})
	if err != nil {
		return AdminUserDetail{}, err
	}
	var out AdminUserDetail
	err = c.decodeEnvelope(ctx, http.MethodPut,
		fmt.Sprintf("/api/admin/users/%d/available-space", userID),
		requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// AdminDeleteUser calls DELETE /api/admin/users/{id} (soft delete).
func (c *Client) AdminDeleteUser(ctx context.Context, userID uint64) error {
	return c.decodeEnvelope(ctx, http.MethodDelete, fmt.Sprintf("/api/admin/users/%d", userID), requestOpts{}, nil)
}

// AdminStorageStats calls GET /api/admin/storage/stats.
func (c *Client) AdminStorageStats(ctx context.Context) (AdminStorageStats, error) {
	var out AdminStorageStats
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/admin/storage/stats", requestOpts{}, &out)
	return out, err
}

// AdminListSharesParams are the query parameters for GET /api/admin/shares.
type AdminListSharesParams struct {
	Page     int
	PageSize int
	Status   *int
	UserID   uint64
	Username string
}

// AdminListShares calls GET /api/admin/shares.
func (c *Client) AdminListShares(ctx context.Context, p AdminListSharesParams) (AdminShareListResponse, error) {
	q := url.Values{}
	if p.Page > 0 {
		q.Set("page", strconv.Itoa(p.Page))
	}
	if p.PageSize > 0 {
		q.Set("page_size", strconv.Itoa(p.PageSize))
	}
	if p.Status != nil {
		q.Set("status", strconv.Itoa(*p.Status))
	}
	if p.UserID > 0 {
		q.Set("user_id", strconv.FormatUint(p.UserID, 10))
	}
	if p.Username != "" {
		q.Set("username", p.Username)
	}
	var out AdminShareListResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/admin/shares", requestOpts{query: q}, &out)
	return out, err
}

// AdminGetShare calls GET /api/admin/shares/{id}.
func (c *Client) AdminGetShare(ctx context.Context, id uint64) (AdminShareDetail, error) {
	var out AdminShareDetail
	err := c.decodeEnvelope(ctx, http.MethodGet, fmt.Sprintf("/api/admin/shares/%d", id), requestOpts{}, &out)
	return out, err
}

// AdminForceCancelShare calls DELETE /api/admin/shares/{id}.
func (c *Client) AdminForceCancelShare(ctx context.Context, id uint64) error {
	return c.decodeEnvelope(ctx, http.MethodDelete, fmt.Sprintf("/api/admin/shares/%d", id), requestOpts{}, nil)
}

// AdminOverviewStats calls GET /api/admin/stats/overview.
// The backend response is loosely typed; we return it as raw map.
func (c *Client) AdminOverviewStats(ctx context.Context) (map[string]any, error) {
	var out map[string]any
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/admin/stats/overview", requestOpts{}, &out)
	return out, err
}

// AdminSystemStatus calls GET /api/admin/stats/system.
func (c *Client) AdminSystemStatus(ctx context.Context) (AdminSystemStatus, error) {
	var out AdminSystemStatus
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/admin/stats/system", requestOpts{}, &out)
	return out, err
}

// AdminLogsParams are the query parameters for GET /api/admin/logs.
type AdminLogsParams struct {
	Page      int
	PageSize  int
	Action    string
	StartDate string
	EndDate   string
}

// AdminLogs calls GET /api/admin/logs.
func (c *Client) AdminLogs(ctx context.Context, p AdminLogsParams) (AdminLogListResponse, error) {
	q := url.Values{}
	if p.Page > 0 {
		q.Set("page", strconv.Itoa(p.Page))
	}
	if p.PageSize > 0 {
		q.Set("page_size", strconv.Itoa(p.PageSize))
	}
	if p.Action != "" {
		q.Set("action", p.Action)
	}
	if p.StartDate != "" {
		q.Set("start_date", p.StartDate)
	}
	if p.EndDate != "" {
		q.Set("end_date", p.EndDate)
	}
	var out AdminLogListResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/admin/logs", requestOpts{query: q}, &out)
	return out, err
}
