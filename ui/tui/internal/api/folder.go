package api

import (
	"context"
	"net/url"
	"strconv"

	"github.com/liufeng/disk/ui/tui/internal/models"
)

type FolderAPI struct {
	client *Client
}

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

func (f *FolderAPI) Breadcrumb(ctx context.Context, folderID uint64) (*models.Breadcrumb, error) {
	path := "/api/folder/" + strconv.FormatUint(folderID, 10) + "/breadcrumb"

	var resp models.Breadcrumb
	if err := f.client.doRequest(ctx, "GET", path, nil, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}
