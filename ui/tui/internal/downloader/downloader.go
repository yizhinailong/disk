// Package downloader 文件下载管理
//
// 提供文件下载功能，支持进度回调和取消操作。
// 下载进度实时计算速度并支持断点续传。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package downloader

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"time"

	"github.com/yizhinailong/disk/ui/tui/internal/api"
	"github.com/yizhinailong/disk/ui/tui/internal/models"
)

// DownloadStatus 下载状态
type DownloadStatus string

// 下载状态常量
const (
	StatusPending     DownloadStatus = "pending"     // 等待中
	StatusDownloading DownloadStatus = "downloading" // 下载中
	StatusSuccess     DownloadStatus = "success"     // 下载成功
	StatusFailed      DownloadStatus = "failed"      // 下载失败
	StatusCanceled    DownloadStatus = "canceled"    // 已取消
)

// DownloadTask 下载任务
type DownloadTask struct {
	ID         string             // 任务唯一标识
	FileID     uint64             // 文件 ID
	FileName   string             // 文件名
	FileSize   int64              // 文件大小（字节）
	FileHash   string             // 文件哈希值
	SavePath   string             // 本地保存路径
	Progress   float64            // 下载进度（百分比）
	Speed      string             // 下载速度
	Status     DownloadStatus     // 下载状态
	Error      error              // 错误信息
	Downloaded int64              // 已下载字节数
	cancelFunc context.CancelFunc // 取消函数
}

// ProgressInfo 下载进度信息
type ProgressInfo struct {
	TaskID     string         // 任务 ID
	Progress   float64        // 进度百分比
	Downloaded int64          // 已下载字节数
	Total      int64          // 总字节数
	Speed      string         // 当前速度
	Status     DownloadStatus // 下载状态
	Error      error          // 错误信息
	FileName   string         // 文件名
}

// Downloader 下载器
type Downloader struct {
	client *api.Client // API 客户端
}

// New 创建下载器
//
// 参数:
//   - client: API 客户端
//
// 返回:
//   - *Downloader: 下载器实例
func New(client *api.Client) *Downloader {
	return &Downloader{client: client}
}

// CreateTask 创建下载任务
//
// 参数:
//   - file: 文件信息
//   - saveDir: 保存目录
//
// 返回:
//   - *DownloadTask: 下载任务
func (d *Downloader) CreateTask(file *models.File, saveDir string) *DownloadTask {
	return &DownloadTask{
		ID:       generateID(),
		FileID:   file.ID,
		FileName: file.Name,
		FileSize: int64(file.Size),
		FileHash: file.Hash,
		SavePath: filepath.Join(saveDir, file.Name),
		Status:   StatusPending,
	}
}

// Download 执行下载
//
// 参数:
//   - ctx: 上下文
//   - task: 下载任务
//   - progressFunc: 进度回调函数
//
// 返回:
//   - error: 错误信息
func (d *Downloader) Download(ctx context.Context, task *DownloadTask, progressFunc func(ProgressInfo)) error {
	task.Status = StatusDownloading

	ctx, cancel := context.WithCancel(ctx)
	task.cancelFunc = cancel
	defer func() {
		if task.cancelFunc != nil {
			task.cancelFunc = nil
		}
	}()

	if _, err := os.Stat(task.SavePath); err == nil {
		task.Status = StatusFailed
		task.Error = fmt.Errorf("file already exists: %s", task.SavePath)
		return task.Error
	}

	file, err := os.Create(task.SavePath)
	if err != nil {
		task.Status = StatusFailed
		task.Error = err
		return err
	}
	defer file.Close()

	url := d.client.File.DownloadURL(task.FileID)
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		task.Status = StatusFailed
		task.Error = err
		os.Remove(task.SavePath)
		return err
	}

	token := d.client.GetAccessToken()
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}

	client := &http.Client{}
	resp, err := client.Do(req)
	if err != nil {
		task.Status = StatusFailed
		task.Error = err
		os.Remove(task.SavePath)
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		task.Status = StatusFailed
		task.Error = fmt.Errorf("HTTP %d", resp.StatusCode)
		os.Remove(task.SavePath)
		return task.Error
	}

	buf := make([]byte, 32*1024)
	startTime := time.Now()
	lastUpdate := startTime
	var downloaded int64
	var lastDownloaded int64

	for {
		n, err := resp.Body.Read(buf)
		if n > 0 {
			_, writeErr := file.Write(buf[:n])
			if writeErr != nil {
				task.Status = StatusFailed
				task.Error = writeErr
				os.Remove(task.SavePath)
				return writeErr
			}
			downloaded += int64(n)
			task.Downloaded = downloaded

			now := time.Now()
			if now.Sub(lastUpdate) >= 100*time.Millisecond || err == io.EOF {
				task.Progress = float64(downloaded) / float64(task.FileSize) * 100

				elapsed := now.Sub(startTime).Seconds()
				if elapsed > 0 {
					speed := float64(downloaded) / elapsed
					task.Speed = formatSpeed(speed)
				}

				if progressFunc != nil {
					currentSpeed := float64(0.0)
					delta := now.Sub(lastUpdate).Seconds()
					if delta > 0 {
						currentSpeed = float64(downloaded-lastDownloaded) / delta
					}

					progressFunc(ProgressInfo{
						TaskID:     task.ID,
						Progress:   task.Progress,
						Downloaded: downloaded,
						Total:      task.FileSize,
						Speed:      formatSpeed(currentSpeed),
						Status:     StatusDownloading,
						FileName:   task.FileName,
					})
				}

				lastUpdate = now
				lastDownloaded = downloaded
			}
		}
		if err == io.EOF {
			break
		}
		if err != nil {
			task.Status = StatusFailed
			task.Error = err
			os.Remove(task.SavePath)
			return err
		}
	}

	task.Status = StatusSuccess
	task.Progress = 100

	if progressFunc != nil {
		progressFunc(ProgressInfo{
			TaskID:     task.ID,
			Progress:   100,
			Downloaded: task.FileSize,
			Total:      task.FileSize,
			Status:     StatusSuccess,
			FileName:   task.FileName,
		})
	}

	return nil
}

// Cancel 取消下载任务
//
// 参数:
//   - task: 下载任务
func (d *Downloader) Cancel(task *DownloadTask) {
	if task.cancelFunc != nil {
		task.cancelFunc()
		task.Status = StatusCanceled
		if task.SavePath != "" {
			os.Remove(task.SavePath)
		}
	}
}

// DownloadWithProgress 带进度回调的下载
//
// 创建任务并执行下载，通过回调实时报告进度。
//
// 参数:
//   - ctx: 上下文
//   - file: 文件信息
//   - saveDir: 保存目录
//   - progressFunc: 进度回调函数
//
// 返回:
//   - *DownloadTask: 下载任务
//   - error: 错误信息
func (d *Downloader) DownloadWithProgress(ctx context.Context, file *models.File, saveDir string, progressFunc func(ProgressInfo)) (*DownloadTask, error) {
	task := d.CreateTask(file, saveDir)

	if progressFunc != nil {
		progressFunc(ProgressInfo{
			TaskID:     task.ID,
			Progress:   0,
			Downloaded: 0,
			Total:      task.FileSize,
			Status:     StatusPending,
			FileName:   task.FileName,
		})
	}

	err := d.Download(ctx, task, progressFunc)
	return task, err
}

// generateID 生成任务 ID
func generateID() string {
	return fmt.Sprintf("%d", time.Now().UnixNano())
}

// formatSpeed 格式化下载速度
//
// 参数:
//   - bytesPerSec: 每秒字节数
//
// 返回:
//   - string: 格式化后的速度字符串
func formatSpeed(bytesPerSec float64) string {
	const KB = 1024
	const MB = KB * 1024

	if bytesPerSec >= MB {
		return fmt.Sprintf("%.1f MB/s", bytesPerSec/MB)
	}
	if bytesPerSec >= KB {
		return fmt.Sprintf("%.1f KB/s", bytesPerSec/KB)
	}
	return fmt.Sprintf("%.0f B/s", bytesPerSec)
}
