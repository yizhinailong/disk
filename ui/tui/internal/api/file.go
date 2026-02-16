package api

import (
	"context"
	"net/url"
	"strconv"

	"github.com/liufeng/disk/ui/tui/internal/models"
)

type FileAPI struct {
	client *Client
}

type ListOptions struct {
	ParentID  uint64
	Page      int
	PageSize  int
	SortBy    string
	SortOrder string
	Type      string
}

func (f *FileAPI) List(ctx context.Context, opts ListOptions) (*models.FileList, error) {
	params := url.Values{}
	if opts.ParentID > 0 {
		params.Set("parent_id", strconv.FormatUint(opts.ParentID, 10))
	}
	if opts.Page > 0 {
		params.Set("page", strconv.Itoa(opts.Page))
	}
	if opts.PageSize > 0 {
		params.Set("page_size", strconv.Itoa(opts.PageSize))
	}
	if opts.SortBy != "" {
		params.Set("sort_by", opts.SortBy)
	}
	if opts.SortOrder != "" {
		params.Set("sort_order", opts.SortOrder)
	}
	if opts.Type != "" {
		params.Set("type", opts.Type)
	}

	path := "/api/file/list"
	if len(params) > 0 {
		path += "?" + params.Encode()
	}

	var resp models.FileList
	if err := f.client.doRequest(ctx, "GET", path, nil, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

func (f *FileAPI) Rename(ctx context.Context, fileID uint64, newName string) (*models.File, error) {
	path := "/api/file/" + strconv.FormatUint(fileID, 10) + "/rename"
	body := map[string]string{"new_name": newName}

	var resp models.File
	if err := f.client.doRequest(ctx, "PUT", path, body, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

func (f *FileAPI) Move(ctx context.Context, fileIDs []uint64, targetFolderID uint64) error {
	body := map[string]any{
		"file_ids":         fileIDs,
		"target_folder_id": targetFolderID,
	}
	return f.client.doRequest(ctx, "PUT", "/api/file/move", body, nil)
}

type CopyResult struct {
	CopiedCount int `json:"copied_count"`
	NewFiles    []struct {
		OldID uint64 `json:"old_id"`
		NewID uint64 `json:"new_id"`
	} `json:"new_files"`
}

func (f *FileAPI) Copy(ctx context.Context, fileIDs []uint64, targetFolderID uint64) (*CopyResult, error) {
	body := map[string]any{
		"file_ids":         fileIDs,
		"target_folder_id": targetFolderID,
	}

	var resp CopyResult
	if err := f.client.doRequest(ctx, "POST", "/api/file/copy", body, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

func (f *FileAPI) Delete(ctx context.Context, fileIDs []uint64) error {
	body := map[string]any{"file_ids": fileIDs}
	return f.client.doRequest(ctx, "DELETE", "/api/file", body, nil)
}

func (f *FileAPI) InitUpload(ctx context.Context, req *models.FileUploadInit) (*models.FileUploadInitResponse, error) {
	var resp models.FileUploadInitResponse
	if err := f.client.doRequest(ctx, "POST", "/api/file/upload/init", req, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

func (f *FileAPI) DownloadURL(fileID uint64) string {
	return f.client.config.Server.URL + "/api/file/download/" + strconv.FormatUint(fileID, 10)
}
