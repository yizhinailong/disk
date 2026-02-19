// Package api_test 文件 API 测试
//
// 测试文件上传相关的 API 调用，包括分片上传、完成上传和取消上传。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-19
// 版权: Copyright (c) 2026
package api

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/yizhinailong/disk/ui/tui/internal/config"
	"github.com/yizhinailong/disk/ui/tui/internal/models"
	"github.com/yizhinailong/disk/ui/tui/internal/store"
)

// TestUploadChunk_Success 测试分片上传成功
func TestUploadChunk_Success(t *testing.T) {
	// 创建模拟服务器
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// 验证请求路径
		if r.URL.Path != "/api/file/upload/chunk" {
			t.Errorf("请求路径 = %s, want /api/file/upload/chunk", r.URL.Path)
		}

		// 验证请求方法
		if r.Method != "POST" {
			t.Errorf("请求方法 = %s, want POST", r.Method)
		}

		// 验证 Content-Type 是 multipart/form-data
		contentType := r.Header.Get("Content-Type")
		if !strings.HasPrefix(contentType, "multipart/form-data") {
			t.Errorf("Content-Type = %s, want multipart/form-data", contentType)
		}

		// 解析 multipart 表单
		reader, err := r.MultipartReader()
		if err != nil {
			t.Errorf("解析 multipart 失败: %v", err)
		}

		// 验证表单字段
		fields := make(map[string]string)
		for {
			part, err := reader.NextPart()
			if err == io.EOF {
				break
			}
			if err != nil {
				t.Errorf("读取 part 失败: %v", err)
				break
			}

			data, err := io.ReadAll(part)
			if err != nil {
				t.Errorf("读取 part 数据失败: %v", err)
				continue
			}

			fields[part.FormName()] = string(data)
		}

		// 验证必需字段
		if fields["upload_id"] != "test-upload-id" {
			t.Errorf("upload_id = %s, want test-upload-id", fields["upload_id"])
		}
		if fields["chunk_index"] != "0" {
			t.Errorf("chunk_index = %s, want 0", fields["chunk_index"])
		}
		if fields["chunk_hash"] != "abc123" {
			t.Errorf("chunk_hash = %s, want abc123", fields["chunk_hash"])
		}
		if fields["chunk"] != "test chunk data" {
			t.Errorf("chunk data = %s, want 'test chunk data'", fields["chunk"])
		}

		// 返回成功响应
		resp := models.ApiResponse[any]{
			Code:    0,
			Message: "success",
			Data:    nil,
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	// 创建测试客户端
	client := createTestClient(t, server.URL)

	// 执行上传
	chunkData := []byte("test chunk data")
	err := client.File.UploadChunk(context.Background(), "test-upload-id", 0, "abc123", chunkData)
	if err != nil {
		t.Fatalf("UploadChunk() 失败: %v", err)
	}
}

// TestUploadChunk_Error 测试分片上传错误
func TestUploadChunk_Error(t *testing.T) {
	// 创建模拟服务器 - 返回错误
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		resp := models.ApiResponse[any]{
			Code:    50001,
			Message: "上传分片失败",
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusBadRequest)
		json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	client := createTestClient(t, server.URL)

	chunkData := []byte("test chunk data")
	err := client.File.UploadChunk(context.Background(), "test-upload-id", 0, "abc123", chunkData)
	if err == nil {
		t.Fatal("UploadChunk() 应该返回错误")
	}

	// 验证错误类型
	apiErr, ok := err.(*APIError)
	if !ok {
		t.Fatalf("错误类型 = %T, want *APIError", err)
	}
	if apiErr.Code != 50001 {
		t.Errorf("错误码 = %d, want 50001", apiErr.Code)
	}
}

// TestCompleteUpload_Success 测试完成上传成功
func TestCompleteUpload_Success(t *testing.T) {
	// 创建模拟服务器
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// 验证请求路径
		if r.URL.Path != "/api/file/upload/complete" {
			t.Errorf("请求路径 = %s, want /api/file/upload/complete", r.URL.Path)
		}

		// 验证请求方法
		if r.Method != "POST" {
			t.Errorf("请求方法 = %s, want POST", r.Method)
		}

		// 验证请求体
		var req models.CompleteUploadRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			t.Errorf("解析请求体失败: %v", err)
		}
		if req.UploadID != "test-upload-id" {
			t.Errorf("upload_id = %s, want test-upload-id", req.UploadID)
		}

		// 返回成功响应
		now := time.Now()
		file := models.File{
			ID:        123,
			Name:      "test.txt",
			Type:      models.FileTypeFile,
			Size:      100,
			Hash:      "file-hash-abc",
			MimeType:  "text/plain",
			ParentID:  0,
			CreatedAt: now,
			UpdatedAt: now,
		}
		resp := models.ApiResponse[models.File]{
			Code:    0,
			Message: "success",
			Data:    file,
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	client := createTestClient(t, server.URL)

	file, err := client.File.CompleteUpload(context.Background(), "test-upload-id")
	if err != nil {
		t.Fatalf("CompleteUpload() 失败: %v", err)
	}

	// 验证返回的文件信息
	if file.ID != 123 {
		t.Errorf("file.ID = %d, want 123", file.ID)
	}
	if file.Name != "test.txt" {
		t.Errorf("file.Name = %s, want test.txt", file.Name)
	}
}

// TestCompleteUpload_Error 测试完成上传错误
func TestCompleteUpload_Error(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		resp := models.ApiResponse[any]{
			Code:    50002,
			Message: "完成上传失败：分片不完整",
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusBadRequest)
		json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	client := createTestClient(t, server.URL)

	_, err := client.File.CompleteUpload(context.Background(), "test-upload-id")
	if err == nil {
		t.Fatal("CompleteUpload() 应该返回错误")
	}

	apiErr, ok := err.(*APIError)
	if !ok {
		t.Fatalf("错误类型 = %T, want *APIError", err)
	}
	if apiErr.Code != 50002 {
		t.Errorf("错误码 = %d, want 50002", apiErr.Code)
	}
}

// TestCancelUpload_Success 测试取消上传成功
func TestCancelUpload_Success(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// 验证请求路径
		if r.URL.Path != "/api/file/upload/cancel" {
			t.Errorf("请求路径 = %s, want /api/file/upload/cancel", r.URL.Path)
		}

		// 验证请求方法
		if r.Method != "POST" {
			t.Errorf("请求方法 = %s, want POST", r.Method)
		}

		// 验证请求体
		var req models.CancelUploadRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			t.Errorf("解析请求体失败: %v", err)
		}
		if req.UploadID != "test-upload-id" {
			t.Errorf("upload_id = %s, want test-upload-id", req.UploadID)
		}

		// 返回成功响应
		resp := models.ApiResponse[any]{
			Code:    0,
			Message: "success",
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	client := createTestClient(t, server.URL)

	err := client.File.CancelUpload(context.Background(), "test-upload-id")
	if err != nil {
		t.Fatalf("CancelUpload() 失败: %v", err)
	}
}

// TestCancelUpload_Error 测试取消上传错误
func TestCancelUpload_Error(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		resp := models.ApiResponse[any]{
			Code:    50003,
			Message: "取消上传失败：上传任务不存在",
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusNotFound)
		json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	client := createTestClient(t, server.URL)

	err := client.File.CancelUpload(context.Background(), "nonexistent-id")
	if err == nil {
		t.Fatal("CancelUpload() 应该返回错误")
	}

	apiErr, ok := err.(*APIError)
	if !ok {
		t.Fatalf("错误类型 = %T, want *APIError", err)
	}
	if apiErr.Code != 50003 {
		t.Errorf("错误码 = %d, want 50003", apiErr.Code)
	}
}

// TestUploadChunk_WithAuth 测试分片上传带认证
func TestUploadChunk_WithAuth(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// 验证 Authorization 头
		auth := r.Header.Get("Authorization")
		if auth != "Bearer test-access-token" {
			t.Errorf("Authorization = %s, want 'Bearer test-access-token'", auth)
		}

		// 解析 multipart 表单并验证
		reader, err := r.MultipartReader()
		if err != nil {
			t.Errorf("解析 multipart 失败: %v", err)
		}

		for {
			part, err := reader.NextPart()
			if err == io.EOF {
				break
			}
			if err != nil {
				break
			}
			// 简单读取所有数据
			io.Copy(io.Discard, part)
		}

		resp := models.ApiResponse[any]{
			Code:    0,
			Message: "success",
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	client := createTestClientWithToken(t, server.URL, "test-access-token", "test-refresh-token", 3600)

	chunkData := []byte("test chunk data")
	err := client.File.UploadChunk(context.Background(), "test-upload-id", 0, "abc123", chunkData)
	if err != nil {
		t.Fatalf("UploadChunk() 失败: %v", err)
	}
}

// createTestClient 创建测试用的 API 客户端
func createTestClient(t *testing.T, serverURL string) *Client {
	t.Helper()

	cfg := &config.Config{
		Server: config.ServerConfig{
			URL:     serverURL,
			Timeout: 30,
		},
	}

	tokenStore := store.NewTokenStore("")
	return NewClient(cfg, tokenStore)
}

// createTestClientWithToken 创建带令牌的测试客户端
func createTestClientWithToken(t *testing.T, serverURL, accessToken, refreshToken string, expiresIn int) *Client {
	t.Helper()

	client := createTestClient(t, serverURL)
	client.SetToken(accessToken, refreshToken, expiresIn)
	return client
}

// TestUploadChunk_LargeData 测试大分片上传
func TestUploadChunk_LargeData(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// 验证分片数据大小
		reader, err := r.MultipartReader()
		if err != nil {
			t.Errorf("解析 multipart 失败: %v", err)
		}

		var chunkSize int64
		for {
			part, err := reader.NextPart()
			if err == io.EOF {
				break
			}
			if err != nil {
				break
			}

			if part.FormName() == "chunk" {
				// 读取分片数据
				data, err := io.ReadAll(part)
				if err != nil {
					t.Errorf("读取分片数据失败: %v", err)
				}
				chunkSize = int64(len(data))
			}
		}

		// 验证分片大小为 1MB
		expectedSize := int64(1024 * 1024)
		if chunkSize != expectedSize {
			t.Errorf("分片大小 = %d, want %d", chunkSize, expectedSize)
		}

		resp := models.ApiResponse[any]{
			Code:    0,
			Message: "success",
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	client := createTestClient(t, server.URL)

	// 创建 1MB 的测试数据
	chunkData := make([]byte, 1024*1024)
	for i := range chunkData {
		chunkData[i] = byte(i % 256)
	}

	err := client.File.UploadChunk(context.Background(), "test-upload-id", 0, "large-chunk-hash", chunkData)
	if err != nil {
		t.Fatalf("UploadChunk() 失败: %v", err)
	}
}

// TestUploadChunk_ContextCancel 测试分片上传时上下文取消
func TestUploadChunk_ContextCancel(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// 模拟延迟
		time.Sleep(100 * time.Millisecond)

		resp := models.ApiResponse[any]{
			Code:    0,
			Message: "success",
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	client := createTestClient(t, server.URL)

	// 创建可取消的上下文
	ctx, cancel := context.WithCancel(context.Background())
	cancel() // 立即取消

	chunkData := []byte("test chunk data")
	err := client.File.UploadChunk(ctx, "test-upload-id", 0, "abc123", chunkData)
	if err == nil {
		t.Fatal("UploadChunk() 应该返回错误（上下文已取消）")
	}
}

// TestUploadChunk_InvalidChunkIndex 测试无效的分片索引
func TestUploadChunk_InvalidChunkIndex(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// 解析并验证分片索引
		reader, err := r.MultipartReader()
		if err != nil {
			t.Errorf("解析 multipart 失败: %v", err)
		}

		var chunkIndex int
		for {
			part, err := reader.NextPart()
			if err == io.EOF {
				break
			}
			if err != nil {
				break
			}

			if part.FormName() == "chunk_index" {
				data, _ := io.ReadAll(part)
				chunkIndex, _ = strconv.Atoi(string(data))
			}
		}

		// 模拟无效分片索引错误
		if chunkIndex < 0 {
			resp := models.ApiResponse[any]{
				Code:    40001,
				Message: "无效的分片索引",
			}
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusBadRequest)
			json.NewEncoder(w).Encode(resp)
			return
		}

		resp := models.ApiResponse[any]{
			Code:    0,
			Message: "success",
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	client := createTestClient(t, server.URL)

	// 测试负数分片索引
	chunkData := []byte("test chunk data")
	err := client.File.UploadChunk(context.Background(), "test-upload-id", -1, "abc123", chunkData)
	if err == nil {
		t.Fatal("UploadChunk() 应该返回错误（无效分片索引）")
	}

	apiErr, ok := err.(*APIError)
	if !ok {
		t.Fatalf("错误类型 = %T, want *APIError", err)
	}
	if apiErr.Code != 40001 {
		t.Errorf("错误码 = %d, want 40001", apiErr.Code)
	}
}
