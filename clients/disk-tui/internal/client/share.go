package client

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
)

// CreateShareParams are the parameters for POST /api/share.
type CreateShareParams struct {
	FileIDs    []uint64
	FolderIDs  []uint64
	ExpireDays *int
	Password   *string // empty pointer-to-"" is allowed to remove; nil omits
	Permission string  // "view" or "download"; empty defaults server-side
}

// CreateShare calls POST /api/share.
func (c *Client) CreateShare(ctx context.Context, p CreateShareParams) (CreateShareResponse, error) {
	payload := map[string]any{
		"file_ids":   orEmpty(p.FileIDs),
		"folder_ids": orEmpty(p.FolderIDs),
	}
	if p.ExpireDays != nil {
		payload["expire_days"] = *p.ExpireDays
	}
	if p.Password != nil {
		payload["password"] = *p.Password
	}
	if p.Permission != "" {
		payload["permission"] = p.Permission
	}
	body, ct, err := writeJSON(payload)
	if err != nil {
		return CreateShareResponse{}, err
	}
	var out CreateShareResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/share", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// ListShares calls GET /api/share.
func (c *Client) ListShares(ctx context.Context, status string, page, pageSize int) (ShareListResponse, error) {
	q := url.Values{}
	if status != "" {
		q.Set("status", status)
	}
	if page > 0 {
		q.Set("page", strconv.Itoa(page))
	}
	if pageSize > 0 {
		q.Set("page_size", strconv.Itoa(pageSize))
	}
	var out ShareListResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/share", requestOpts{query: q}, &out)
	return out, err
}

// GetShareDetail calls GET /api/share/{share_id}.
func (c *Client) GetShareDetail(ctx context.Context, shareID string) (ShareDetailResponse, error) {
	var out ShareDetailResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, fmt.Sprintf("/api/share/%s", shareID), requestOpts{}, &out)
	return out, err
}

// UpdateShareParams are the optional fields for PUT /api/share/{share_id}.
type UpdateShareParams struct {
	ExpireDays *int
	Password   *string
	Permission *string
}

// UpdateShare calls PUT /api/share/{share_id}.
func (c *Client) UpdateShare(ctx context.Context, shareID string, p UpdateShareParams) (UpdateShareResponse, error) {
	payload := map[string]any{}
	if p.ExpireDays != nil {
		payload["expire_days"] = *p.ExpireDays
	}
	if p.Password != nil {
		payload["password"] = *p.Password
	}
	if p.Permission != nil {
		payload["permission"] = *p.Permission
	}
	body, ct, err := writeJSON(payload)
	if err != nil {
		return UpdateShareResponse{}, err
	}
	var out UpdateShareResponse
	err = c.decodeEnvelope(ctx, http.MethodPut, fmt.Sprintf("/api/share/%s", shareID), requestOpts{
		body: body, contentType: ct,
	}, &out)
	return out, err
}

// CancelShares calls DELETE /api/share (body: {share_ids}).
func (c *Client) CancelShares(ctx context.Context, shareIDs []string) (CancelShareResponse, error) {
	body, ct, err := writeJSON(map[string]any{"share_ids": orEmpty(shareIDs)})
	if err != nil {
		return CancelShareResponse{}, err
	}
	var out CancelShareResponse
	err = c.decodeEnvelope(ctx, http.MethodDelete, "/api/share", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// CancelSharesAlt calls POST /api/share/cancel.
func (c *Client) CancelSharesAlt(ctx context.Context, shareIDs []string) (CancelShareResponse, error) {
	body, ct, err := writeJSON(map[string]any{"share_ids": orEmpty(shareIDs)})
	if err != nil {
		return CancelShareResponse{}, err
	}
	var out CancelShareResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/share/cancel", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// AccessShare calls POST /api/share/access/{share_id}.
// On success the returned share_token is stored on the client as the
// visitor share token (X-Share-Token) for subsequent browse/download calls.
func (c *Client) AccessShare(ctx context.Context, shareID, password string) (AccessShareResponse, error) {
	payload := map[string]any{}
	if password != "" {
		payload["password"] = password
	}
	body, ct, err := writeJSON(payload)
	if err != nil {
		return AccessShareResponse{}, err
	}
	var out AccessShareResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, fmt.Sprintf("/api/share/access/%s", shareID), requestOpts{
		body: body, contentType: ct, noAuth: true,
	}, &out)
	if err == nil {
		c.SetShareToken(out.ShareToken)
	}
	return out, err
}

// BrowseShare calls GET /api/share/browse/{share_id}.
// Requires a visitor share token set via AccessShare.
func (c *Client) BrowseShare(ctx context.Context, shareID string, folderID uint64) (BrowseShareResponse, error) {
	q := url.Values{}
	if folderID > 0 {
		q.Set("folder_id", strconv.FormatUint(folderID, 10))
	}
	var out BrowseShareResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, fmt.Sprintf("/api/share/browse/%s", shareID), requestOpts{
		query: q, shareAuth: true,
	}, &out)
	return out, err
}

// DownloadShareFile calls GET /api/share/download/{share_id}/{file_id}.
// Requires a visitor share token.
func (c *Client) DownloadShareFile(ctx context.Context, shareID string, fileID uint64, dst io.Writer) (http.Header, error) {
	resp, err := c.doRequest(ctx, http.MethodGet,
		fmt.Sprintf("/api/share/download/%s/%d", shareID, fileID),
		requestOpts{shareAuth: true})
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode >= 400 {
		return resp.Header, readBodyError(resp, "/api/share/download")
	}
	if _, err := io.Copy(dst, resp.Body); err != nil {
		return resp.Header, err
	}
	return resp.Header, nil
}

// SaveShareItemsParams are the parameters for POST /api/share/save/{share_id}.
type SaveShareItemsParams struct {
	FileIDs        []uint64
	FolderIDs      []uint64
	TargetFolderID uint64
}

// SaveShareItems calls POST /api/share/save/{share_id}.
// Requires both a visitor share token (X-Share-Token) and an owner JWT.
func (c *Client) SaveShareItems(ctx context.Context, shareID string, p SaveShareItemsParams) (SaveShareItemsResponse, error) {
	body, ct, err := writeJSON(map[string]any{
		"file_ids":         orEmpty(p.FileIDs),
		"folder_ids":       orEmpty(p.FolderIDs),
		"target_folder_id": p.TargetFolderID,
	})
	if err != nil {
		return SaveShareItemsResponse{}, err
	}
	var out SaveShareItemsResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, fmt.Sprintf("/api/share/save/%s", shareID), requestOpts{
		body: body, contentType: ct, shareAuth: true,
	}, &out)
	return out, err
}
