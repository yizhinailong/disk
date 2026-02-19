//go:build integration

package integration

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/yizhinailong/disk/ui/tui/internal/api"
	"github.com/yizhinailong/disk/ui/tui/internal/config"
	"github.com/yizhinailong/disk/ui/tui/internal/downloader"
	"github.com/yizhinailong/disk/ui/tui/internal/models"
	"github.com/yizhinailong/disk/ui/tui/internal/store"
	"github.com/yizhinailong/disk/ui/tui/internal/uploader"
)

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

func getTestFileID(t *testing.T) uint64 {
	t.Helper()

	fileIDStr := os.Getenv("DISK_TEST_FILE_ID")
	if fileIDStr == "" {
		t.Skip("DISK_TEST_FILE_ID not set, skipping download test")
	}

	var fileID uint64
	for _, c := range fileIDStr {
		if c >= '0' && c <= '9' {
			fileID = fileID*10 + uint64(c-'0')
		}
	}

	return fileID
}

func TestIntegration_DownloadFile(t *testing.T) {
	client, cleanup := setupDownloadTest(t)
	defer cleanup()

	fileID := getTestFileID(t)

	file, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

	t.Logf("下载文件: ID=%d, Name=%s, Size=%d", file.ID, file.Name, file.Size)

	dl := downloader.New(client)

	saveDir, err := os.MkdirTemp("", "disk-download")
	if err != nil {
		t.Fatalf("创建保存目录失败: %v", err)
	}
	defer os.RemoveAll(saveDir)

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

	if task.Status != downloader.StatusSuccess {
		t.Errorf("任务状态 = %v, want %v", task.Status, downloader.StatusSuccess)
	}

	if _, err := os.Stat(task.SavePath); os.IsNotExist(err) {
		t.Fatalf("下载的文件不存在: %s", task.SavePath)
	}

	fileInfo, err := os.Stat(task.SavePath)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

	if fileInfo.Size() != int64(file.Size) {
		t.Errorf("文件大小 = %d, want %d", fileInfo.Size(), file.Size)
	}

	if lastProgress.Progress != 100 {
		t.Errorf("最终进度 = %.1f, want 100", lastProgress.Progress)
	}

	t.Logf("下载成功: %s -> %s (%d bytes)", file.Name, task.SavePath, fileInfo.Size())
}

func TestIntegration_DownloadLargeFile(t *testing.T) {
	client, cleanup := setupDownloadTest(t)
	defer cleanup()

	fileID := getTestFileID(t)

	file, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

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

	if progressCount < 10 {
		t.Logf("警告: 进度更新次数较少 (%d)", progressCount)
	}

	fileSizeMB := float64(file.Size) / 1024 / 1024
	speedMBps := fileSizeMB / downloadDuration.Seconds()

	t.Logf("大文件下载成功: %.2f MB, 耗时 %v, 平均速度 %.2f MB/s",
		fileSizeMB, downloadDuration, speedMBps)
}

func TestIntegration_DownloadCancel(t *testing.T) {
	client, cleanup := setupDownloadTest(t)
	defer cleanup()

	fileID := getTestFileID(t)

	file, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

	if file.Size < 5*1024*1024 {
		t.Skipf("文件太小 (%d bytes)，跳过取消测试", file.Size)
	}

	dl := downloader.New(client)

	saveDir, err := os.MkdirTemp("", "disk-download")
	if err != nil {
		t.Fatalf("创建保存目录失败: %v", err)
	}
	defer os.RemoveAll(saveDir)

	ctx, cancel := context.WithCancel(context.Background())

	task := dl.CreateTask(file, saveDir)

	go func() {
		time.Sleep(200 * time.Millisecond)
		cancel()
	}()

	var progressReceived bool
	err = dl.Download(ctx, task, func(info downloader.ProgressInfo) {
		progressReceived = true
	})

	if err == nil {
		t.Error("Download() 应该返回错误")
	}

	if task.Status != downloader.StatusCanceled && task.Status != downloader.StatusFailed {
		t.Errorf("任务状态 = %v, want %v or %v", task.Status, downloader.StatusCanceled, downloader.StatusFailed)
	}

	if _, err := os.Stat(task.SavePath); !os.IsNotExist(err) {
		t.Errorf("取消后文件应该被删除: %s", task.SavePath)
	}

	t.Logf("下载取消成功: 状态=%v, 收到进度=%v", task.Status, progressReceived)
}

func TestIntegration_UploadAndDownload(t *testing.T) {
	client, cleanup := setupDownloadTest(t)
	defer cleanup()

	filePath := createTestFile(nil, 5*1024*1024)
	defer os.Remove(filePath)

	originalMD5, err := calculateFileMD5(filePath)
	if err != nil {
		t.Fatalf("计算原始文件 MD5 失败: %v", err)
	}

	up := uploader.New(client)
	upTask, err := up.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := up.CalculateHash(upTask, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	if err := up.Upload(context.Background(), upTask, nil); err != nil {
		t.Fatalf("上传失败: %v", err)
	}

	fileID := findUploadedFile(t, client, upTask.FileName, 0)
	t.Logf("上传成功: ID=%d", fileID)

	dl := downloader.New(client)
	saveDir, err := os.MkdirTemp("", "disk-download")
	if err != nil {
		t.Fatalf("创建保存目录失败: %v", err)
	}
	defer os.RemoveAll(saveDir)

	fileInfo, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

	dlTask, err := dl.DownloadWithProgress(context.Background(), fileInfo, saveDir, nil)
	if err != nil {
		t.Fatalf("下载失败: %v", err)
	}

	downloadedMD5, err := calculateFileMD5(dlTask.SavePath)
	if err != nil {
		t.Fatalf("计算下载文件 MD5 失败: %v", err)
	}

	if originalMD5 != downloadedMD5 {
		t.Errorf("MD5 不一致: 原始=%s, 下载=%s", originalMD5, downloadedMD5)
	}

	t.Logf("上传下载验证成功: MD5=%s", originalMD5)
}

func TestIntegration_DownloadFileInfo(t *testing.T) {
	client, cleanup := setupDownloadTest(t)
	defer cleanup()

	fileID := getTestFileID(t)

	file, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		t.Fatalf("获取文件信息失败: %v", err)
	}

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
