package client

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strconv"
)

// FileListParams are the query parameters for GET /api/file/list.
type FileListParams struct {
	ParentID  uint64
	Page      int
	PageSize  int
	SortBy    string // name|size|created_at|updated_at
	SortOrder string // asc|desc
	Type      string // all|file|folder
}

// ListFiles calls GET /api/file/list.
func (c *Client) ListFiles(ctx context.Context, p FileListParams) (FileListResponse, error) {
	q := url.Values{}
	q.Set("parent_id", strconv.FormatUint(p.ParentID, 10))
	if p.Page > 0 {
		q.Set("page", strconv.Itoa(p.Page))
	}
	if p.PageSize > 0 {
		q.Set("page_size", strconv.Itoa(p.PageSize))
	}
	if p.SortBy != "" {
		q.Set("sort_by", p.SortBy)
	}
	if p.SortOrder != "" {
		q.Set("sort_order", p.SortOrder)
	}
	if p.Type != "" {
		q.Set("type", p.Type)
	}
	var out FileListResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/file/list", requestOpts{query: q}, &out)
	return out, err
}

// GetFileDetail calls GET /api/file/{file_id}.
func (c *Client) GetFileDetail(ctx context.Context, fileID uint64) (FileDetail, error) {
	var out FileDetail
	err := c.decodeEnvelope(ctx, http.MethodGet, fmt.Sprintf("/api/file/%d", fileID), requestOpts{}, &out)
	return out, err
}

// DownloadInfo calls GET /api/file/download/{file_id}/info.
func (c *Client) DownloadInfo(ctx context.Context, fileID uint64) (DownloadInfoResponse, error) {
	var out DownloadInfoResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, fmt.Sprintf("/api/file/download/%d/info", fileID), requestOpts{}, &out)
	return out, err
}

// DownloadFile calls GET /api/file/download/{file_id} and streams the
// response body into dst. Returns the response headers (Content-Length,
// Content-Range, Content-Disposition) for caller use.
func (c *Client) DownloadFile(ctx context.Context, fileID uint64, dst io.Writer) (http.Header, error) {
	resp, err := c.doRawOwnerRetry(ctx, http.MethodGet, fmt.Sprintf("/api/file/download/%d", fileID), requestOpts{})
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode >= 400 {
		return resp.Header, readBodyError(resp, "/api/file/download")
	}
	if _, err := io.Copy(dst, resp.Body); err != nil {
		return resp.Header, err
	}
	return resp.Header, nil
}

// DownloadFileRange calls GET /api/file/download/{file_id} with a Range
// header and returns the raw response (caller closes body). Useful for
// resumable downloads.
func (c *Client) DownloadFileRange(ctx context.Context, fileID uint64, start, end int64) (*http.Response, error) {
	headers := map[string]string{}
	if start >= 0 && end >= start {
		headers["Range"] = fmt.Sprintf("bytes=%d-%d", start, end)
	} else if start >= 0 {
		headers["Range"] = fmt.Sprintf("bytes=%d-", start)
	}
	resp, err := c.doRawOwnerRetry(ctx, http.MethodGet, fmt.Sprintf("/api/file/download/%d", fileID), requestOpts{headers: headers})
	if err != nil {
		return nil, err
	}
	if resp.StatusCode >= 400 {
		defer resp.Body.Close()
		return resp, readBodyError(resp, "/api/file/download")
	}
	return resp, nil
}

// RenameFile calls PUT /api/file/{file_id}/rename.
func (c *Client) RenameFile(ctx context.Context, fileID uint64, newName string) (RenameResponse, error) {
	body, ct, err := writeJSON(map[string]string{"new_name": newName})
	if err != nil {
		return RenameResponse{}, err
	}
	var out RenameResponse
	err = c.decodeEnvelope(ctx, http.MethodPut, fmt.Sprintf("/api/file/%d/rename", fileID), requestOpts{
		body: body, contentType: ct,
	}, &out)
	return out, err
}

// MoveItems calls PUT /api/file/move.
func (c *Client) MoveItems(ctx context.Context, fileIDs, folderIDs []uint64, targetFolderID uint64) (MoveResponse, error) {
	body, ct, err := writeJSON(map[string]any{
		"file_ids":         orEmpty(fileIDs),
		"folder_ids":       orEmpty(folderIDs),
		"target_folder_id": targetFolderID,
	})
	if err != nil {
		return MoveResponse{}, err
	}
	var out MoveResponse
	err = c.decodeEnvelope(ctx, http.MethodPut, "/api/file/move", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// CopyItems calls POST /api/file/copy.
func (c *Client) CopyItems(ctx context.Context, fileIDs, folderIDs []uint64, targetFolderID uint64) (CopyResponse, error) {
	body, ct, err := writeJSON(map[string]any{
		"file_ids":         orEmpty(fileIDs),
		"folder_ids":       orEmpty(folderIDs),
		"target_folder_id": targetFolderID,
	})
	if err != nil {
		return CopyResponse{}, err
	}
	var out CopyResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/file/copy", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// DeleteItems calls DELETE /api/file (body: {file_ids, folder_ids}).
func (c *Client) DeleteItems(ctx context.Context, fileIDs, folderIDs []uint64) (DeleteResponse, error) {
	body, ct, err := writeJSON(map[string]any{
		"file_ids":   orEmpty(fileIDs),
		"folder_ids": orEmpty(folderIDs),
	})
	if err != nil {
		return DeleteResponse{}, err
	}
	var out DeleteResponse
	err = c.decodeEnvelope(ctx, http.MethodDelete, "/api/file", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// DeleteItemsAlt calls POST /api/file/delete (alias of DELETE).
func (c *Client) DeleteItemsAlt(ctx context.Context, fileIDs, folderIDs []uint64) (DeleteResponse, error) {
	body, ct, err := writeJSON(map[string]any{
		"file_ids":   orEmpty(fileIDs),
		"folder_ids": orEmpty(folderIDs),
	})
	if err != nil {
		return DeleteResponse{}, err
	}
	var out DeleteResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/file/delete", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// SearchFiles calls GET /api/file/search.
func (c *Client) SearchFiles(ctx context.Context, keyword, typ string, folderID uint64, page, pageSize int) (SearchResponse, error) {
	q := url.Values{}
	q.Set("keyword", keyword)
	if typ != "" {
		q.Set("type", typ)
	}
	if folderID > 0 {
		q.Set("folder_id", strconv.FormatUint(folderID, 10))
	}
	if page > 0 {
		q.Set("page", strconv.Itoa(page))
	}
	if pageSize > 0 {
		q.Set("page_size", strconv.Itoa(pageSize))
	}
	var out SearchResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/file/search", requestOpts{query: q}, &out)
	return out, err
}

// ==================== Upload flow ====================

// InitUpload calls POST /api/file/upload/init.
func (c *Client) InitUpload(ctx context.Context, filename string, fileSize uint64, fileHash string, parentID uint64) (InitUploadResponse, error) {
	body, ct, err := writeJSON(map[string]any{
		"filename":  filename,
		"file_size": fileSize,
		"file_hash": fileHash,
		"parent_id": parentID,
	})
	if err != nil {
		return InitUploadResponse{}, err
	}
	var out InitUploadResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/file/upload/init", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// UploadChunk calls POST /api/file/upload/chunk with the chunk binary in
// the request body and metadata in query parameters.
func (c *Client) UploadChunk(ctx context.Context, uploadID, chunkHash string, chunkIndex uint32, chunk []byte) (UploadChunkResponse, error) {
	q := url.Values{}
	q.Set("upload_id", uploadID)
	q.Set("chunk_index", strconv.FormatUint(uint64(chunkIndex), 10))
	q.Set("chunk_hash", chunkHash)
	var out UploadChunkResponse
	err := c.decodeEnvelope(ctx, http.MethodPost, "/api/file/upload/chunk", requestOpts{
		query:       q,
		rawReader:   bytes.NewReader(chunk),
		contentType: "application/octet-stream",
	}, &out)
	return out, err
}

// UploadChunkReader is like UploadChunk but reads from a reader (avoids
// buffering the whole chunk in memory).
func (c *Client) UploadChunkReader(ctx context.Context, uploadID, chunkHash string, chunkIndex uint32, r io.Reader) (UploadChunkResponse, error) {
	q := url.Values{}
	q.Set("upload_id", uploadID)
	q.Set("chunk_index", strconv.FormatUint(uint64(chunkIndex), 10))
	q.Set("chunk_hash", chunkHash)
	var out UploadChunkResponse
	err := c.decodeEnvelope(ctx, http.MethodPost, "/api/file/upload/chunk", requestOpts{
		query:       q,
		rawReader:   r,
		contentType: "application/octet-stream",
	}, &out)
	return out, err
}

// CompleteUpload calls POST /api/file/upload/complete.
func (c *Client) CompleteUpload(ctx context.Context, uploadID string) (CompleteUploadResponse, error) {
	body, ct, err := writeJSON(map[string]string{"upload_id": uploadID})
	if err != nil {
		return CompleteUploadResponse{}, err
	}
	var out CompleteUploadResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/file/upload/complete", requestOpts{body: body, contentType: ct}, &out)
	return out, err
}

// CancelUpload calls DELETE /api/file/upload/{upload_id}.
func (c *Client) CancelUpload(ctx context.Context, uploadID string) error {
	return c.decodeEnvelope(ctx, http.MethodDelete, fmt.Sprintf("/api/file/upload/%s", uploadID), requestOpts{}, nil)
}

// UploadFile is a high-level helper that performs init → chunked upload
// → complete for a local file path. progress is called after each chunk
// with (bytesUploaded, totalBytes). It is safe to pass nil for progress.
func (c *Client) UploadFile(ctx context.Context, localPath string, parentID uint64, progress func(uploaded, total uint64)) (CompleteUploadResponse, error) {
	info, err := os.Stat(localPath)
	if err != nil {
		return CompleteUploadResponse{}, err
	}
	totalSize := uint64(info.Size())

	fullHash, err := fileMD5(localPath)
	if err != nil {
		return CompleteUploadResponse{}, err
	}

	initResp, err := c.InitUpload(ctx, info.Name(), totalSize, fullHash, parentID)
	if err != nil {
		return CompleteUploadResponse{}, err
	}
	if initResp.InstantUpload && initResp.File != nil {
		if progress != nil {
			progress(totalSize, totalSize)
		}
		return CompleteUploadResponse{File: *initResp.File}, nil
	}
	if initResp.UploadID == "" {
		return CompleteUploadResponse{}, fmt.Errorf("upload init returned no upload_id")
	}

	chunkSize := uint64(initResp.ChunkSize)
	if chunkSize == 0 {
		chunkSize = 4 * 1024 * 1024 // fallback
	}
	totalChunks := initResp.TotalChunks
	if totalChunks == 0 {
		totalChunks = uint32((totalSize + chunkSize - 1) / chunkSize)
	}

	uploadedSet := make(map[uint32]struct{}, len(initResp.UploadedChunks))
	for _, idx := range initResp.UploadedChunks {
		uploadedSet[idx] = struct{}{}
	}

	f, err := os.Open(localPath)
	if err != nil {
		_ = c.CancelUpload(ctx, initResp.UploadID)
		return CompleteUploadResponse{}, err
	}
	defer f.Close()

	buf := make([]byte, chunkSize)
	var uploaded uint64
	for i := uint32(0); i < totalChunks; i++ {
		n, rerr := io.ReadFull(f, buf)
		if rerr != nil && rerr != io.EOF && rerr != io.ErrUnexpectedEOF {
			_ = c.CancelUpload(ctx, initResp.UploadID)
			return CompleteUploadResponse{}, rerr
		}
		if n == 0 {
			break
		}
		if _, skip := uploadedSet[i]; skip {
			uploaded += uint64(n)
			if progress != nil {
				progress(uploaded, totalSize)
			}
			continue
		}
		chunk := buf[:n]
		chunkHash := md5Hex(chunk)
		if _, err := c.UploadChunk(ctx, initResp.UploadID, chunkHash, i, chunk); err != nil {
			_ = c.CancelUpload(ctx, initResp.UploadID)
			return CompleteUploadResponse{}, err
		}
		uploaded += uint64(n)
		if progress != nil {
			progress(uploaded, totalSize)
		}
	}

	complete, err := c.CompleteUpload(ctx, initResp.UploadID)
	if err != nil {
		_ = c.CancelUpload(ctx, initResp.UploadID)
		return CompleteUploadResponse{}, err
	}
	return complete, nil
}

// readBodyError turns a failed JSON response into an *APIError.
func readBodyError(resp *http.Response, path string) error {
	defer resp.Body.Close()
	raw, _ := io.ReadAll(resp.Body)
	var env Envelope[json.RawMessage]
	if err := json.Unmarshal(raw, &env); err == nil && env.Message != "" {
		return &APIError{Code: env.Code, HTTPCode: resp.StatusCode, URL: path, Message: env.Message}
	}
	return &APIError{HTTPCode: resp.StatusCode, URL: path, Message: truncate(string(raw), 256)}
}

// orEmpty returns nil-slice-friendly JSON serialization. The backend
// expects arrays even when empty; we send empty arrays explicitly.
func orEmpty[T any](s []T) []T {
	if s == nil {
		return []T{}
	}
	return s
}
