package client

import (
	"context"
	"fmt"
	"net/http"
	"net/url"
	"strconv"
)

// CreateFolder calls POST /api/folder/create.
func (c *Client) CreateFolder(ctx context.Context, name string, parentID uint64) (CreateFolderResponse, error) {
	body, ct, err := writeJSON(map[string]any{
		"name":      name,
		"parent_id": parentID,
	})
	if err != nil {
		return CreateFolderResponse{}, err
	}
	var out CreateFolderResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/folder/create", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// GetFolderTree calls GET /api/folder/tree.
// depth = -1 means unlimited.
func (c *Client) GetFolderTree(ctx context.Context, parentID uint64, depth int) (FolderTreeNode, error) {
	q := url.Values{}
	q.Set("parent_id", strconv.FormatUint(parentID, 10))
	q.Set("depth", strconv.Itoa(depth))
	var out FolderTreeNode
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/folder/tree", requestOpts{query: q}, &out)
	return out, err
}

// GetBreadcrumb calls GET /api/folder/{folder_id}/breadcrumb.
func (c *Client) GetBreadcrumb(ctx context.Context, folderID uint64) (BreadcrumbResponse, error) {
	var out BreadcrumbResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, fmt.Sprintf("/api/folder/%d/breadcrumb", folderID), requestOpts{}, &out)
	return out, err
}

// RenameFolder calls PUT /api/folder/{folder_id}/rename.
func (c *Client) RenameFolder(ctx context.Context, folderID uint64, newName string) (RenameFolderResponse, error) {
	body, ct, err := writeJSON(map[string]string{"new_name": newName})
	if err != nil {
		return RenameFolderResponse{}, err
	}
	var out RenameFolderResponse
	err = c.decodeEnvelope(ctx, http.MethodPut, fmt.Sprintf("/api/folder/%d/rename", folderID), requestOpts{
		body: body, contentType: ct,
	}, &out)
	return out, err
}
