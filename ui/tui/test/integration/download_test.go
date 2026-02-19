//go:build integration

// Package integration 集成测试 - 下载功能
//
// 这些测试需要真实后端服务运行。运行方式:
//
//	# 设置环境变量
//	export DISK_TEST_SERVER=http://localhost:8080
//	export DISK_TEST_TOKEN=<your-access-token>
//	export DISK_TEST_FILE_ID=<file-id-to-download>
//
//	# 运行集成测试
//	go test ./test/integration/... -v -tags=integration -run TestIntegration_Download
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-19
// 版权: Copyright (c) 2026
package integration

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/yizhinailong/disk/ui/tui/internal/api"
	"github.com/yizhinailong/disk/ui/tui/internal/config"
	"github.com/yizhinailong/disk/ui/tui/internal/downloader"
	"github.com/yizhinailong/disk/ui/tui/internal/models"
	"github.com/yizhinailong/disk/ui/tui/internal/store"
)

// setupDownloadTest 设置下载集成测试环境
func setupDownloadTest(t *testing.T) (*api.Client, func()) {
	t.Helper()

	serverURL := os.Getenv("DISK_TEST_SERVER")
	if serverURL == "" {
		t.Skip("DISK_TEST_SERVER not set, skipping integration test")
	}

	accessToken := os.Getenv("DISK_TEST_TOKEN")
	if accessToken == "" {
		t.Skip("DISK_TEST_TOKEN not set, skipping integration test")
	}

	tmpDir, err := os.MkdirTemp("", "disk-download-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}

	cfg := &config.Config{
		Server: config.ServerConfig{
			URL:     serverURL,
			Timeout: 60,
		},
		Storage: config.StorageConfig{
			TokenPath: filepath.Join(tmpDir, "token.enc"),
		},
	}

	tokenStore := store.NewTokenStore(cfg.Storage.TokenPath)
	client := api.NewClient(cfg, tokenStore)
	client.SetToken(accessToken, "test-refresh-token", 3600)

	cleanup := func() {
		os.RemoveAll(tmpDir)
	}

	return client, cleanup
}

// getTestFileID 获取测试文件 ID
func getTestFileID(t *testing.T) uint64 {
	t.Helper()

	fileIDStr := os.Getenv("DISK_TEST_FILE_ID")
	if fileIDStr == "" {
		t.Skip("DISK_TEST_FILE_ID not set, skipping download test")
	}

	var fileID uint64
	_, err := filepath.Match(fileIDStr, "*")
	if err != nil {
		t.Fatalf("无效的文件 ID: %s", fileIDStr)
	}

	_, err = filepath.Match(fileIDStr, "*")
	for _, c := range fileIDStr {
		if c >= '0' && c <= '9' {
			fileID = fileID*10 + uint64(c-'0')
		}
	}

	return fileID
}

// calculateMD5 计算数据的 MD5
func calculateMD5(data []byte) string {
	hash := md5.Sum(data)
	return hex.EncodeToString(hash[:])
}

// TestIntegration_DownloadFile 测试文件下载
//
// 场景: 下载指定文件
// 预期: 文件成功下载，内容正确
func TestIntegration_DownloadFile(t *testing.T) {
	client, cleanup := setupDownloadTest(t)
	defer cleanup()

	fileID := getTestFileID(t)

	// 获取文件信息
	file, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

	t.Logf("下载文件: ID=%d, Name=%s, Size=%d", file.ID, file.Name, file.Size)

	// 创建下载器
	dl := downloader.New(client)

	// 创建临时保存目录
	saveDir, err := os.MkdirTemp("", "disk-download")
	if err != nil {
		t.Fatalf("创建保存目录失败: %v", err)
	}
	defer os.RemoveAll(saveDir)

	// 执行下载
	var lastProgress downloader.ProgressInfo
	task, err := dl.DownloadWithProgress(context.Background(), file, saveDir, func(info downloader.ProgressInfo) {
		lastProgress = info
		if info.Progress > 0 && int(info.Progress)%20 == 0 {
			t.Logf("下载进度: %.1f%% (%s)", info.Progress, info.Speed)
		}
	})

	if err != nil {
		t.Fatalf("下载失败: %v", err)
	}

	// 验证任务状态
	if task.Status != downloader.StatusSuccess {
		t.Errorf("任务状态 = %v, want %v", task.Status, downloader.StatusSuccess)
	}

	// 验证文件存在
	if _, err := os.Stat(task.SavePath); os.IsNotExist(err) {
		t.Fatalf("下载的文件不存在: %s", task.SavePath)
	}

	// 验证文件大小
	fileInfo, err := os.Stat(task.SavePath)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

	if fileInfo.Size() != int64(file.Size) {
		t.Errorf("文件大小 = %d, want %d", fileInfo.Size(), file.Size)
	}

	// 验证最终进度
	if lastProgress.Progress != 100 {
		t.Errorf("最终进度 = %.1f, want 100", lastProgress.Progress)
	}

	t.Logf("下载成功: %s -> %s (%d bytes)", file.Name, task.SavePath, fileInfo.Size())
}

// TestIntegration_DownloadLargeFile 测试大文件下载
//
// 场景: 下载大文件（>10MB）
// 预期: 文件成功下载，进度更新正常
func TestIntegration_DownloadLargeFile(t *testing.T) {
	client, cleanup := setupDownloadTest(t)
	defer cleanup()

	fileID := getTestFileID(t)

	file, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

	// 只测试大于 10MB 的文件
	if file.Size < 10*1024*1024 {
		t.Skipf("文件太小 (%d bytes)，跳过大文件测试", file.Size)
	}

	dl := downloader.New(client)

	saveDir, err := os.MkdirTemp("", "disk-download")
	if err != nil {
		t.Fatalf("创建保存目录失败: %v", err)
	}
	defer os.RemoveAll(saveDir)

	startTime := time.Now()

	var progressCount int
	task, err := dl.DownloadWithProgress(context.Background(), file, saveDir, func(info downloader.ProgressInfo) {
		progressCount++
		if progressCount%50 == 0 {
			t.Logf("下载进度: %.1f%% - %s", info.Progress, info.Speed)
		}
	})

	downloadDuration := time.Since(startTime)

	if err != nil {
		t.Fatalf("下载失败: %v", err)
	}

	if task.Status != downloader.StatusSuccess {
		t.Errorf("任务状态 = %v, want %v", task.Status, downloader.StatusSuccess)
	}

	// 验证进度更新次数（大文件应该有多次更新）
	if progressCount < 10 {
		t.Logf("警告: 进度更新次数较少 (%d)", progressCount)
	}

	// 计算下载速度
	fileSizeMB := float64(file.Size) / 1024 / 1024
	speedMBps := fileSizeMB / downloadDuration.Seconds()

	t.Logf("大文件下载成功: %.2f MB, 耗时 %v, 平均速度 %.2f MB/s",
		fileSizeMB, downloadDuration, speedMBps)
}

// TestIntegration_DownloadCancel 测试取消下载
//
// 场景: 下载过程中取消
// 预期: 下载被正确取消，临时文件被删除
func TestIntegration_DownloadCancel(t *testing.T) {
	client, cleanup := setupDownloadTest(t)
	defer cleanup()

	fileID := getTestFileID(t)

	file, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

	// 只测试大文件
	if file.Size < 5*1024*1024 {
		t.Skipf("文件太小 (%d bytes)，跳过取消测试", file.Size)
	}

	dl := downloader.New(client)

	saveDir, err := os.MkdirTemp("", "disk-download")
	if err != nil {
		t.Fatalf("创建保存目录失败: %v", err)
	}
	defer os.RemoveAll(saveDir)

	// 使用可取消的上下文
	ctx, cancel := context.WithCancel(context.Background())

	task := dl.CreateTask(file, saveDir)

	// 在下载过程中取消
	go func() {
		time.Sleep(200 * time.Millisecond)
		cancel()
	}()

	var progressReceived bool
	err = dl.Download(ctx, task, func(info downloader.ProgressInfo) {
		progressReceived = true
	})

	// 应该返回错误
	if err == nil {
		t.Error("Download() 应该返回错误")
	}

	// 验证任务状态
	if task.Status != downloader.StatusCanceled && task.Status != downloader.StatusFailed {
		t.Errorf("任务状态 = %v, want %v or %v", task.Status, downloader.StatusCanceled, downloader.StatusFailed)
	}

	// 验证临时文件被删除
	if _, err := os.Stat(task.SavePath); !os.IsNotExist(err) {
		t.Errorf("取消后文件应该被删除: %s", task.SavePath)
	}

	t.Logf("下载取消成功: 状态=%v, 收到进度=%v", task.Status, progressReceived)
}

// TestIntegration_UploadAndDownload 测试上传后下载
//
// 场景: 上传文件后立即下载
// 预期: 上传和下载都成功，文件内容一致
func TestIntegration_UploadAndDownload(t *testing.T) {
	client, cleanup := setupDownloadTest(t)
	defer cleanup()

	// 上传测试文件
	filePath := createTestFile(t, 5*1024*1024) // 5MB
	defer os.Remove(filePath)

	// 计算原始文件 MD5
	originalMD5, err := calculateFileMD5(filePath)
	if err != nil {
		t.Fatalf("计算原始文件 MD5 失败: %v", err)
	}

	// 上传文件
	upTask, err := uploadFile(client, filePath, 0)
	if err != nil {
		t.Fatalf("上传失败: %v", err)
	}

	t.Logf("上传成功: ID=%d", upTask.ResultFile.ID)

	// 下载文件
	dl := downloader.New(client)
	saveDir, err := os.MkdirTemp("", "disk-download")
	if err != nil {
		t.Fatalf("创建保存目录失败: %v", err)
	}
	defer os.RemoveAll(saveDir)

	dlTask, err := dl.DownloadWithProgress(context.Background(), upTask.ResultFile, saveDir, nil)
	if err != nil {
		t.Fatalf("下载失败: %v", err)
	}

	// 计算下载文件 MD5
	downloadedMD5, err := calculateFileMD5(dlTask.SavePath)
	if err != nil {
		t.Fatalf("计算下载文件 MD5 失败: %v", err)
	}

	// 验证 MD5 一致
	if originalMD5 != downloadedMD5 {
		t.Errorf("MD5 不一致: 原始=%s, 下载=%s", originalMD5, downloadedMD5)
	}

	t.Logf("上传下载验证成功: MD5=%s", originalMD5)
}

// uploadFile 辅助函数：上传文件
func uploadFile(client *api.Client, filePath string, parentID uint64) (*uploader.Task, error) {
	up := uploader.New(client)

	task, err := up.CreateTask(filePath, parentID)
	if err != nil {
		return nil, err
	}

	if err := up.CalculateHash(task, nil); err != nil {
		return nil, err
	}

	if err := up.Upload(context.Background(), task, nil); err != nil {
		return nil, err
	}

	return task, nil
}

// TestIntegration_DownloadFileInfo 测试获取文件信息
//
// 场景: 获取文件的详细信息
// 预期: 返回正确的文件元数据
func TestIntegration_DownloadFileInfo(t *testing.T) {
	client, cleanup := setupDownloadTest(t)
	defer cleanup()

	fileID := getTestFileID(t)

	file, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

	// 验证文件信息
	if file.ID != fileID {
		t.Errorf("文件 ID = %d, want %d", file.ID, fileID)
	}

	if file.Name == "" {
		t.Error("文件名为空")
	}

	if file.Type != models.FileTypeFile {
		t.Errorf("文件类型 = %s, want %s", file.Type, models.FileTypeFile)
	}

	if file.Size == 0 {
		t.Error("文件大小为 0")
	}

	t.Logf("文件信息: ID=%d, Name=%s, Type=%s, Size=%d, Hash=%s",
		file.ID, file.Name, file.Type, file.Size, file.Hash)
}
