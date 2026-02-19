//go:build integration

package integration

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/yizhinailong/disk/ui/tui/internal/api"
	"github.com/yizhinailong/disk/ui/tui/internal/config"
	"github.com/yizhinailong/disk/ui/tui/internal/store"
	"github.com/yizhinailong/disk/ui/tui/internal/uploader"
)

func setupIntegrationTest(t *testing.T) (*api.Client, func()) {
	t.Helper()

	serverURL := os.Getenv("DISK_TEST_SERVER")
	if serverURL == "" {
		t.Skip("DISK_TEST_SERVER not set, skipping integration test")
	}

	accessToken := os.Getenv("DISK_TEST_TOKEN")
	if accessToken == "" {
		t.Skip("DISK_TEST_TOKEN not set, skipping integration test")
	}

	tmpDir, err := os.MkdirTemp("", "disk-integration-test")
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

func createTestFile(t *testing.T, size int64) string {
	t.Helper()

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

func findUploadedFile(t *testing.T, client *api.Client, fileName string, parentID uint64) uint64 {
	t.Helper()

	list, err := client.File.List(context.Background(), api.ListOptions{ParentID: parentID, PageSize: 100})
	if err != nil {
		t.Fatalf("获取文件列表失败: %v", err)
	}

	for _, f := range list.Items {
		if f.Name == fileName {
			return f.ID
		}
	}

	t.Fatalf("未找到上传的文件: %s", fileName)
	return 0
}

func TestIntegration_UploadSmallFile(t *testing.T) {
	client, cleanup := setupIntegrationTest(t)
	defer cleanup()

	up := uploader.New(client)

	filePath := createTestFile(t, 3*1024*1024)
	defer os.Remove(filePath)

	task, err := up.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := up.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	var lastProgress uploader.ProgressInfo
	err = up.Upload(context.Background(), task, func(info uploader.ProgressInfo) {
		lastProgress = info
		t.Logf("上传进度: %.1f%% (%s)", info.Progress, info.FileName)
	})

	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	if task.Status != uploader.StatusSuccess {
		t.Errorf("任务状态 = %v, want %v", task.Status, uploader.StatusSuccess)
	}

	if lastProgress.Progress != 100 {
		t.Errorf("最终进度 = %.1f, want 100", lastProgress.Progress)
	}

	fileID := findUploadedFile(t, client, task.FileName, 0)
	t.Logf("上传成功: ID=%d, Name=%s, Size=%d", fileID, task.FileName, task.FileSize)
}

func TestIntegration_UploadLargeFile(t *testing.T) {
	client, cleanup := setupIntegrationTest(t)
	defer cleanup()

	up := uploader.New(client)
	up.SetConcurrency(3)

	filePath := createTestFile(t, 15*1024*1024)
	defer os.Remove(filePath)

	task, err := up.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if task.Chunks != 3 {
		t.Errorf("分片数量 = %d, want 3", task.Chunks)
	}

	if err := up.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	startTime := time.Now()

	var progressUpdates []uploader.ProgressInfo
	err = up.Upload(context.Background(), task, func(info uploader.ProgressInfo) {
		progressUpdates = append(progressUpdates, info)
		if len(progressUpdates)%10 == 0 {
			t.Logf("上传进度: %.1f%% - 分片 %d/%d", info.Progress, info.ChunkIndex, info.TotalChunks)
		}
	})

	uploadDuration := time.Since(startTime)

	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	if task.Status != uploader.StatusSuccess {
		t.Errorf("任务状态 = %v, want %v", task.Status, uploader.StatusSuccess)
	}

	if len(progressUpdates) == 0 {
		t.Error("没有收到进度更新")
	}

	if task.Progress != 100 {
		t.Errorf("最终进度 = %.1f, want 100", task.Progress)
	}

	fileID := findUploadedFile(t, client, task.FileName, 0)
	t.Logf("上传成功: ID=%d, 耗时=%v, 平均速度=%.2f MB/s",
		fileID, uploadDuration, float64(15)/uploadDuration.Seconds())
}

func TestIntegration_UploadToFolder(t *testing.T) {
	client, cleanup := setupIntegrationTest(t)
	defer cleanup()

	folderIDStr := os.Getenv("DISK_TEST_FOLDER_ID")
	if folderIDStr == "" {
		t.Skip("DISK_TEST_FOLDER_ID not set, skipping folder upload test")
	}

	var folderID uint64
	fmt.Sscanf(folderIDStr, "%d", &folderID)

	up := uploader.New(client)

	filePath := createTestFile(t, 1*1024*1024)
	defer os.Remove(filePath)

	task, err := up.CreateTask(filePath, folderID)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := up.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	err = up.Upload(context.Background(), task, nil)
	if err != nil {
		t.Fatalf("Upload() 失败: %v", err)
	}

	fileID := findUploadedFile(t, client, task.FileName, folderID)

	file, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		t.Fatalf("Get file 失败: %v", err)
	}

	if file.ParentID != folderID {
		t.Errorf("文件 ParentID = %d, want %d", file.ParentID, folderID)
	}

	t.Logf("上传到文件夹成功: FileID=%d, FolderID=%d", file.ID, folderID)
}

func TestIntegration_UploadCancel(t *testing.T) {
	client, cleanup := setupIntegrationTest(t)
	defer cleanup()

	up := uploader.New(client)

	filePath := createTestFile(t, 50*1024*1024)
	defer os.Remove(filePath)

	task, err := up.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := up.CalculateHash(task, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	ctx, cancel := context.WithCancel(context.Background())

	go func() {
		time.Sleep(500 * time.Millisecond)
		cancel()
	}()

	err = up.Upload(ctx, task, nil)

	if err == nil {
		t.Error("Upload() 应该返回错误")
	}

	if task.Status != uploader.StatusCanceled && task.Status != uploader.StatusFailed {
		t.Errorf("任务状态 = %v, want %v or %v", task.Status, uploader.StatusCanceled, uploader.StatusFailed)
	}

	t.Logf("上传取消成功: 状态=%v", task.Status)
}

func TestIntegration_UploadInstant(t *testing.T) {
	client, cleanup := setupIntegrationTest(t)
	defer cleanup()

	up := uploader.New(client)

	filePath := createTestFile(t, 3*1024*1024)
	defer os.Remove(filePath)

	task1, err := up.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := up.CalculateHash(task1, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	startTime := time.Now()
	err = up.Upload(context.Background(), task1, nil)
	if err != nil {
		t.Fatalf("第一次上传失败: %v", err)
	}
	firstUploadDuration := time.Since(startTime)

	task2, err := up.CreateTask(filePath, 0)
	if err != nil {
		t.Fatalf("CreateTask() 失败: %v", err)
	}

	if err := up.CalculateHash(task2, nil); err != nil {
		t.Fatalf("CalculateHash() 失败: %v", err)
	}

	startTime = time.Now()
	err = up.Upload(context.Background(), task2, nil)
	if err != nil {
		t.Fatalf("第二次上传失败: %v", err)
	}
	secondUploadDuration := time.Since(startTime)

	if secondUploadDuration >= firstUploadDuration {
		t.Logf("警告: 秒传耗时 %v >= 正常上传耗时 %v（可能未触发秒传）", secondUploadDuration, firstUploadDuration)
	}

	if task2.Status != uploader.StatusSuccess {
		t.Errorf("秒传状态 = %v, want %v", task2.Status, uploader.StatusSuccess)
	}

	t.Logf("秒传测试: 第一次=%v, 第二次=%v", firstUploadDuration, secondUploadDuration)
}
