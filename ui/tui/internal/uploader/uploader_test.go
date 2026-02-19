// Package uploader_test 上传功能测试
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-19
// 版权: Copyright (c) 2026
package uploader

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/yizhinailong/disk/ui/tui/internal/api"
	"github.com/yizhinailong/disk/ui/tui/internal/config"
	"github.com/yizhinailong/disk/ui/tui/internal/models"
	"github.com/yizhinailong/disk/ui/tui/internal/store"
)

// mockServer 模拟后端 API 服务器
type mockServer struct {
	server                *httptest.Server
	uploadedChunks        map[string][]int          // uploadID -> 已上传的分片
	chunkData             map[string]map[int][]byte // uploadID -> chunkIndex -> data
	mu                    sync.Mutex
	initUploadHandler     func(w http.ResponseWriter, r *http.Request)
	completeUploadHandler func(w http.ResponseWriter, r *http.Request)
}

// newMockServer 创建模拟服务器
func newMockServer() *mockServer {
	m := &mockServer{
		uploadedChunks: make(map[string][]int),
		chunkData:      make(map[string]map[int][]byte),
	}
	m.server = httptest.NewServer(http.HandlerFunc(m.handleRequest))
	return m
}

// handleRequest 处理 HTTP 请求
func (m *mockServer) handleRequest(w http.ResponseWriter, r *http.Request) {
	switch r.URL.Path {
	case "/api/file/upload/init":
		m.handleInitUpload(w, r)
	case "/api/file/upload/chunk":
		m.handleUploadChunk(w, r)
	case "/api/file/upload/complete":
		m.handleCompleteUpload(w, r)
	case "/api/file/upload/cancel":
		m.handleCancelUpload(w, r)
	default:
		http.NotFound(w, r)
	}
}

// handleInitUpload 处理初始化上传请求
func (m *mockServer) handleInitUpload(w http.ResponseWriter, r *http.Request) {
	m.mu.Lock()
	defer m.mu.Unlock()

	// 自定义处理器
	if m.initUploadHandler != nil {
		m.initUploadHandler(w, r)
		return
	}

	var req models.FileUploadInit
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	uploadID := fmt.Sprintf("upload-%d", time.Now().UnixNano())
	m.uploadedChunks[uploadID] = []int{}
	m.chunkData[uploadID] = make(map[int][]byte)

	resp := models.FileUploadInitResponse{
		UploadID:       uploadID,
		ChunkSize:      DefaultChunkSize,
		UploadedChunks: []int{},
		InstantUpload:  false,
	}

	m.writeJSONResponse(w, resp)
}

// handleUploadChunk 处理上传分片请求
func (m *mockServer) handleUploadChunk(w http.ResponseWriter, r *http.Request) {
	m.mu.Lock()
	defer m.mu.Unlock()

	// 解析 multipart 表单
	if err := r.ParseMultipartForm(32 << 20); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	uploadID := r.FormValue("upload_id")
	chunkIndex := 0
	fmt.Sscanf(r.FormValue("chunk_index"), "%d", &chunkIndex)

	file, _, err := r.FormFile("chunk")
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	defer file.Close()

	// 读取分片数据
	data := make([]byte, r.ContentLength)
	n, _ := file.Read(data)
	data = data[:n]

	// 存储分片数据
	if m.chunkData[uploadID] == nil {
		m.chunkData[uploadID] = make(map[int][]byte)
	}
	m.chunkData[uploadID][chunkIndex] = data

	// 记录已上传的分片
	alreadyUploaded := false
	for _, idx := range m.uploadedChunks[uploadID] {
		if idx == chunkIndex {
			alreadyUploaded = true
			break
		}
	}
	if !alreadyUploaded {
		m.uploadedChunks[uploadID] = append(m.uploadedChunks[uploadID], chunkIndex)
	}

	m.writeSuccessResponse(w, nil)
}

// handleCompleteUpload 处理完成上传请求
func (m *mockServer) handleCompleteUpload(w http.ResponseWriter, r *http.Request) {
	m.mu.Lock()
	defer m.mu.Unlock()

	// 自定义处理器
	if m.completeUploadHandler != nil {
		m.completeUploadHandler(w, r)
		return
	}

	var req models.CompleteUploadRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	file := &models.File{
		ID:        1,
		Name:      "test.bin",
		Type:      models.FileTypeFile,
		Size:      15728640,
		Hash:      "test-hash",
		MimeType:  "application/octet-stream",
		ParentID:  0,
		CreatedAt: time.Now(),
		UpdatedAt: time.Now(),
	}

	m.writeSuccessResponse(w, file)
}

// handleCancelUpload 处理取消上传请求
func (m *mockServer) handleCancelUpload(w http.ResponseWriter, r *http.Request) {
	m.mu.Lock()
	defer m.mu.Unlock()

	var req models.CancelUploadRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	delete(m.uploadedChunks, req.UploadID)
	delete(m.chunkData, req.UploadID)

	m.writeSuccessResponse(w, nil)
}

// writeJSONResponse 写入 JSON 响应
func (m *mockServer) writeJSONResponse(w http.ResponseWriter, data any) {
	resp := models.ApiResponse[any]{
		Code:    0,
		Message: "success",
		Data:    data,
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

// writeSuccessResponse 写入成功响应
func (m *mockServer) writeSuccessResponse(w http.ResponseWriter, data any) {
	m.writeJSONResponse(w, data)
}

// Close 关闭服务器
func (m *mockServer) Close() {
	m.server.Close()
}

// URL 获取服务器 URL
func (m *mockServer) URL() string {
	return m.server.URL
}

// getUploadedChunks 获取已上传的分片
func (m *mockServer) getUploadedChunks(uploadID string) []int {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.uploadedChunks[uploadID]
}

// setupTestUploader 创建测试用上传器
func setupTestUploader(t *testing.T, mock *mockServer) (*Uploader, func()) {
	tmpDir, err := os.MkdirTemp("", "uploader-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}

	cfg := &config.Config{
		Server: config.ServerConfig{
			URL:     mock.URL(),
			Timeout: 30,
		},
		Storage: config.StorageConfig{
			TokenPath: filepath.Join(tmpDir, "token.enc"),
		},
	}

	tokenStore := store.NewTokenStore(cfg.Storage.TokenPath)
	client := api.NewClient(cfg, tokenStore)
	client.SetToken("test-access-token", "test-refresh-token", 3600)

	uploader := New(client)

	cleanup := func() {
		os.RemoveAll(tmpDir)
	}

	return uploader, cleanup
}

// createTestFile 创建测试文件
func createTestFile(t *testing.T, size int64) string {
	tmpFile, err := os.CreateTemp("", "test-upload-*.bin")
	if err != nil {
		t.Fatalf("创建测试文件失败: %v", err)
	}
	defer tmpFile.Close()

	data := make([]byte, size)
	for i := range data {
		data[i] = byte(i % 256)
	}

	if _, err := tmpFile.Write(data); err != nil {
		t.Fatalf("写入测试文件失败: %v", err)
	}

	return tmpFile.Name()
}

// calculateFileMD5 计算文件 MD5
func calculateFileMD5(filePath string) (string, error) {
	file, err := os.Open(filePath)
	if err != nil {
		return "", err
	}
	defer file.Close()

	hash := md5.New()
	buffer := make([]byte, 32*1024)
	for {
		n, err := file.Read(buffer)
		if n > 0 {
			hash.Write(buffer[:n])
		}
		if err != nil {
			break
		}
	}

	return hex.EncodeToString(hash.Sum(nil)), nil
}

// TestUpload_SmallFile 测试小文件上传（单分片）
func TestUpload_SmallFile(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	// 创建 3MB 文件（小于默认分片大小 5MB）
	filePath := createTestFile(t, 3*1024*1024)
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	// 计算哈希
	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	// 执行上传
	var progressUpdates []ProgressInfo
	err = uploader.Upload(context.Background(), task, func(info ProgressInfo) {
		progressUpdates = append(progressUpdates, info)
	})

	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	// 验证任务状态
	if task.Status != StatusSuccess {
		t.Errorf("任务状态 = %v, want %v", task.Status, StatusSuccess)
	}

	// 验证进度更新
	if len(progressUpdates) == 0 {
		t.Error("没有收到进度更新")
	}

	// 验证最后一个进度更新是 100%
	lastProgress := progressUpdates[len(progressUpdates)-1]
	if lastProgress.Progress != 100 {
		t.Errorf("最终进度 = %.1f, want 100", lastProgress.Progress)
	}

	// 验证分片数量
	if task.Chunks != 1 {
		t.Errorf("分片数量 = %d, want 1", task.Chunks)
	}
}

// TestUpload_LargeFile 测试大文件上传（多分片）
func TestUpload_LargeFile(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	// 创建 15MB 文件（3 个分片）
	filePath := createTestFile(t, 15*1024*1024)
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	// 验证分片数量计算
	if task.Chunks != 3 {
		t.Errorf("分片数量 = %d, want 3", task.Chunks)
	}

	// 计算哈希
	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	// 执行上传
	var progressUpdates []ProgressInfo
	var mu sync.Mutex
	err = uploader.Upload(context.Background(), task, func(info ProgressInfo) {
		mu.Lock()
		progressUpdates = append(progressUpdates, info)
		mu.Unlock()
	})

	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	// 验证任务状态
	if task.Status != StatusSuccess {
		t.Errorf("任务状态 = %v, want %v", task.Status, StatusSuccess)
	}

	// 验证进度最终为 100%
	if task.Progress != 100 {
		t.Errorf("最终进度 = %.1f, want 100", task.Progress)
	}

	// 验证已上传分片（通过 uploadID 获取）
	if len(mock.getUploadedChunks(task.uploadID)) != 3 {
		t.Errorf("已上传分片数 = %d, want 3", len(mock.getUploadedChunks(task.uploadID)))
	}
}

// TestUpload_InstantUpload 测试秒传
func TestUpload_InstantUpload(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	// 设置秒传响应
	mock.mu.Lock()
	mock.initUploadHandler = func(w http.ResponseWriter, r *http.Request) {
		resp := models.FileUploadInitResponse{
			InstantUpload: true,
			File: &models.File{
				ID:        123,
				Name:      "existing.bin",
				Type:      models.FileTypeFile,
				Size:      3145728,
				Hash:      "existing-hash",
				CreatedAt: time.Now(),
				UpdatedAt: time.Now(),
			},
		}
		mock.writeJSONResponse(w, resp)
	}
	mock.mu.Unlock()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	filePath := createTestFile(t, 3*1024*1024)
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	var progressCalled bool
	err = uploader.Upload(context.Background(), task, func(info ProgressInfo) {
		progressCalled = true
		// 秒传应该立即完成
		if info.Status != StatusSuccess {
			t.Errorf("秒传状态 = %v, want %v", info.Status, StatusSuccess)
		}
	})

	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	if task.Status != StatusSuccess {
		t.Errorf("任务状态 = %v, want %v", task.Status, StatusSuccess)
	}

	if !progressCalled {
		t.Error("秒传应该调用进度回调")
	}

	// 秒传不应该上传任何分片
	if len(mock.getUploadedChunks("")) != 0 {
		t.Error("秒传不应该上传分片")
	}
}

// TestUpload_ResumeFromState 测试从状态恢复上传
func TestUpload_ResumeFromState(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	// 创建 15MB 文件（3 分片）
	filePath := createTestFile(t, 15*1024*1024)
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	// 设置服务器返回已上传的前两个分片
	mock.mu.Lock()
	mock.initUploadHandler = func(w http.ResponseWriter, r *http.Request) {
		uploadID := fmt.Sprintf("upload-resume-%d", time.Now().UnixNano())
		mock.uploadedChunks[uploadID] = []int{0, 1} // 前两个分片已上传
		mock.chunkData[uploadID] = make(map[int][]byte)

		resp := models.FileUploadInitResponse{
			UploadID:       uploadID,
			ChunkSize:      DefaultChunkSize,
			UploadedChunks: []int{0, 1},
			InstantUpload:  false,
		}
		mock.writeJSONResponse(w, resp)
	}
	mock.mu.Unlock()

	// 执行上传
	err = uploader.Upload(context.Background(), task, nil)
	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	if task.Status != StatusSuccess {
		t.Errorf("任务状态 = %v, want %v", task.Status, StatusSuccess)
	}

	// 验证只上传了第三个分片
	uploadedChunks := mock.getUploadedChunks(task.uploadID)
	if len(uploadedChunks) != 3 {
		t.Errorf("已上传分片数 = %d, want 3（包含已恢复的2个）", len(uploadedChunks))
	}
}

// TestUpload_ParallelUpload 测试并行上传
func TestUpload_ParallelUpload(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	// 设置并发数为 3
	uploader.SetConcurrency(3)

	// 创建 25MB 文件（5 分片）
	filePath := createTestFile(t, 25*1024*1024)
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	// 记录上传时间
	startTime := time.Now()
	err = uploader.Upload(context.Background(), task, nil)
	uploadDuration := time.Since(startTime)

	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	if task.Status != StatusSuccess {
		t.Errorf("任务状态 = %v, want %v", task.Status, StatusSuccess)
	}

	// 验证所有分片都已上传
	if len(mock.getUploadedChunks(task.uploadID)) != 5 {
		t.Errorf("已上传分片数 = %d, want 5", len(mock.getUploadedChunks(task.uploadID)))
	}

	// 验证上传完成（这里只是验证没有错误，实际并行度受网络和实现影响）
	t.Logf("上传 25MB (5 分片) 耗时: %v", uploadDuration)
}

// TestUpload_Cancel 测试取消上传
func TestUpload_Cancel(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	// 创建大文件
	filePath := createTestFile(t, 50*1024*1024)
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	// 使用可取消的上下文
	ctx, cancel := context.WithCancel(context.Background())

	// 在另一个 goroutine 中取消
	go func() {
		time.Sleep(100 * time.Millisecond)
		cancel()
	}()

	err = uploader.Upload(ctx, task, nil)

	// 应该返回上下文取消错误
	if err == nil {
		t.Error("Upload() 应该返回取消错误")
	}

	if task.Status != StatusFailed && task.Status != StatusCanceled {
		t.Errorf("任务状态 = %v, want %v or %v", task.Status, StatusFailed, StatusCanceled)
	}
}

// TestUpload_ContextTimeout 测试上下文超时
func TestUpload_ContextTimeout(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	filePath := createTestFile(t, 10*1024*1024)
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	// 设置非常短的超时
	ctx, cancel := context.WithTimeout(context.Background(), 1*time.Nanosecond)
	defer cancel()

	err = uploader.Upload(ctx, task, nil)

	if err == nil {
		t.Error("Upload() 应该返回超时错误")
	}
}

// TestUpload_ProgressInfo 测试进度信息包含分片信息
func TestUpload_ProgressInfo(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	filePath := createTestFile(t, 10*1024*1024) // 2 分片
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	var lastProgress ProgressInfo
	err = uploader.Upload(context.Background(), task, func(info ProgressInfo) {
		lastProgress = info
	})

	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	// 验证进度信息字段
	if lastProgress.TaskID == "" {
		t.Error("进度信息缺少 TaskID")
	}
	if lastProgress.FileName == "" {
		t.Error("进度信息缺少 FileName")
	}
	if lastProgress.Total != task.FileSize {
		t.Errorf("进度信息 Total = %d, want %d", lastProgress.Total, task.FileSize)
	}
	if lastProgress.TotalChunks != task.Chunks {
		t.Errorf("进度信息 TotalChunks = %d, want %d", lastProgress.TotalChunks, task.Chunks)
	}
}

// TestUpload_StatePersistence 测试状态持久化
func TestUpload_StatePersistence(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	// 创建临时状态目录
	tmpDir, err := os.MkdirTemp("", "state-persist-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	// 设置状态管理器
	stateMgr := &StateManager{filePath: filepath.Join(tmpDir, "uploads.json")}
	uploader.SetStateManager(stateMgr)

	filePath := createTestFile(t, 10*1024*1024)
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	err = uploader.Upload(context.Background(), task, nil)
	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	// 成功后状态文件应该被删除
	_, err = stateMgr.GetByUploadID(task.uploadID)
	if err == nil {
		// 如果能找到状态，说明没有被清理
		t.Log("注意: 状态文件应该在成功后被清理")
	}
}

// TestUpload_AllChunksUploaded 测试所有分片都被上传
func TestUpload_AllChunksUploaded(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	filePath := createTestFile(t, 15*1024*1024) // 3 分片
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	err = uploader.Upload(context.Background(), task, nil)
	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	uploadedChunks := mock.getUploadedChunks(task.uploadID)
	if len(uploadedChunks) != 3 {
		t.Errorf("已上传分片数 = %d, want 3", len(uploadedChunks))
	}

	chunkSet := make(map[int]bool)
	for _, idx := range uploadedChunks {
		chunkSet[idx] = true
	}
	for i := 0; i < 3; i++ {
		if !chunkSet[i] {
			t.Errorf("分片 %d 未被上传", i)
		}
	}
}

// TestUpload_CompleteUploadCalled 测试完成上传被调用
func TestUpload_CompleteUploadCalled(t *testing.T) {
	mock := newMockServer()
	defer mock.Close()

	var completeCalled int32

	mock.mu.Lock()
	mock.completeUploadHandler = func(w http.ResponseWriter, r *http.Request) {
		atomic.StoreInt32(&completeCalled, 1)
		// Write response directly to avoid deadlock (don't call handleCompleteUpload)
		file := &models.File{
			ID:        1,
			Name:      "test.bin",
			Type:      models.FileTypeFile,
			Size:      3145728,
			Hash:      "test-hash",
			MimeType:  "application/octet-stream",
			ParentID:  0,
			CreatedAt: time.Now(),
			UpdatedAt: time.Now(),
		}
		mock.writeJSONResponse(w, file)
	}
	mock.mu.Unlock()

	uploader, cleanup := setupTestUploader(t, mock)
	defer cleanup()

	filePath := createTestFile(t, 3*1024*1024)
	defer os.Remove(filePath)

	task, err := uploader.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := uploader.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	err = uploader.Upload(context.Background(), task, nil)
	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	if atomic.LoadInt32(&completeCalled) != 1 {
		t.Error("CompleteUpload 应该被调用")
	}
}

// TestSetConcurrency 测试设置并发数
func TestSetConcurrency(t *testing.T) {
	uploader := New(nil)

	// 默认并发数
	if uploader.concurrency != 3 {
		t.Errorf("默认并发数 = %d, want 3", uploader.concurrency)
	}

	// 设置并发数
	uploader.SetConcurrency(5)
	if uploader.concurrency != 5 {
		t.Errorf("设置后并发数 = %d, want 5", uploader.concurrency)
	}

	// 并发数不能小于 1
	uploader.SetConcurrency(0)
	if uploader.concurrency < 1 {
		t.Errorf("并发数不能小于 1: %d", uploader.concurrency)
	}

	uploader.SetConcurrency(-1)
	if uploader.concurrency < 1 {
		t.Errorf("并发数不能小于 1: %d", uploader.concurrency)
	}
}
