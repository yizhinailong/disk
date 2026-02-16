package downloader

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"time"

	"github.com/liufeng/disk/ui/tui/internal/api"
	"github.com/liufeng/disk/ui/tui/internal/models"
)

type DownloadStatus string

const (
	StatusPending     DownloadStatus = "pending"
	StatusDownloading DownloadStatus = "downloading"
	StatusSuccess     DownloadStatus = "success"
	StatusFailed      DownloadStatus = "failed"
	StatusCanceled    DownloadStatus = "canceled"
)

type DownloadTask struct {
	ID         string
	FileID     uint64
	FileName   string
	FileSize   int64
	FileHash   string
	SavePath   string
	Progress   float64
	Speed      string
	Status     DownloadStatus
	Error      error
	Downloaded int64
	cancelFunc context.CancelFunc
}

type ProgressInfo struct {
	TaskID     string
	Progress   float64
	Downloaded int64
	Total      int64
	Speed      string
	Status     DownloadStatus
	Error      error
	FileName   string
}

type Downloader struct {
	client *api.Client
}

func New(client *api.Client) *Downloader {
	return &Downloader{client: client}
}

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
		task.Error = fmt.Errorf("文件已存在: %s", task.SavePath)
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

func (d *Downloader) Cancel(task *DownloadTask) {
	if task.cancelFunc != nil {
		task.cancelFunc()
		task.Status = StatusCanceled
		if task.SavePath != "" {
			os.Remove(task.SavePath)
		}
	}
}

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

func generateID() string {
	return fmt.Sprintf("%d", time.Now().UnixNano())
}

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
