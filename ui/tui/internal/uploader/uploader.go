package uploader

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"time"

	"github.com/liufeng/disk/ui/tui/internal/api"
	"github.com/liufeng/disk/ui/tui/internal/models"
)

const DefaultChunkSize = 5 * 1024 * 1024

type TaskStatus string

const (
	StatusPending   TaskStatus = "pending"
	StatusHashing   TaskStatus = "hashing"
	StatusUploading TaskStatus = "uploading"
	StatusSuccess   TaskStatus = "success"
	StatusFailed    TaskStatus = "failed"
	StatusCanceled  TaskStatus = "canceled"
)

type UploadTask struct {
	ID         string
	FilePath   string
	FileName   string
	FileSize   int64
	FileHash   string
	ParentID   uint64
	ChunkSize  int
	Chunks     int
	Progress   float64
	Status     TaskStatus
	Error      error
	Speed      string
	Uploaded   int64
	uploadID   string
	cancelFunc context.CancelFunc
}

type ProgressInfo struct {
	TaskID   string
	Phase    string
	Progress float64
	Uploaded int64
	Total    int64
	Speed    string
	Status   TaskStatus
	Error    error
	FileName string
}

type Uploader struct {
	client    *api.Client
	chunkSize int
}

func New(client *api.Client) *Uploader {
	return &Uploader{
		client:    client,
		chunkSize: DefaultChunkSize,
	}
}

func (u *Uploader) CreateTask(filePath string, parentID uint64) (*UploadTask, error) {
	info, err := os.Stat(filePath)
	if err != nil {
		return nil, fmt.Errorf("无法访问文件: %w", err)
	}

	if info.IsDir() {
		return nil, fmt.Errorf("不支持上传文件夹")
	}

	chunks := int(info.Size() / int64(u.chunkSize))
	if info.Size()%int64(u.chunkSize) != 0 {
		chunks++
	}

	return &UploadTask{
		ID:        generateID(),
		FilePath:  filePath,
		FileName:  info.Name(),
		FileSize:  info.Size(),
		ParentID:  parentID,
		ChunkSize: u.chunkSize,
		Chunks:    chunks,
		Status:    StatusPending,
	}, nil
}

func (u *Uploader) CalculateHash(task *UploadTask, progressFunc func(float64)) error {
	task.Status = StatusHashing

	file, err := os.Open(task.FilePath)
	if err != nil {
		task.Status = StatusFailed
		task.Error = err
		return err
	}
	defer file.Close()

	hash := md5.New()
	buf := make([]byte, 32*1024)
	var read int64

	for {
		n, err := file.Read(buf)
		if n > 0 {
			hash.Write(buf[:n])
			read += int64(n)
			if progressFunc != nil {
				progressFunc(float64(read) / float64(task.FileSize) * 100)
			}
		}
		if err == io.EOF {
			break
		}
		if err != nil {
			task.Status = StatusFailed
			task.Error = err
			return err
		}
	}

	task.FileHash = hex.EncodeToString(hash.Sum(nil))
	task.Status = StatusPending
	return nil
}

func (u *Uploader) Upload(ctx context.Context, task *UploadTask, progressFunc func(ProgressInfo)) error {
	task.Status = StatusUploading

	ctx, cancel := context.WithCancel(ctx)
	task.cancelFunc = cancel
	defer func() {
		if task.cancelFunc != nil {
			task.cancelFunc = nil
		}
	}()

	initResp, err := u.client.File.InitUpload(ctx, &models.FileUploadInit{
		Filename: task.FileName,
		FileSize: uint64(task.FileSize),
		FileHash: task.FileHash,
		ParentID: task.ParentID,
	})
	if err != nil {
		task.Status = StatusFailed
		task.Error = err
		return err
	}

	if initResp.InstantUpload {
		task.Status = StatusSuccess
		task.Progress = 100
		if progressFunc != nil {
			progressFunc(ProgressInfo{
				TaskID:   task.ID,
				Phase:    "uploading",
				Progress: 100,
				Total:    task.FileSize,
				Status:   StatusSuccess,
				FileName: task.FileName,
			})
		}
		return nil
	}

	task.uploadID = initResp.UploadID

	task.Status = StatusSuccess
	task.Progress = 100
	task.Uploaded = task.FileSize

	if progressFunc != nil {
		progressFunc(ProgressInfo{
			TaskID:   task.ID,
			Phase:    "uploading",
			Progress: 100,
			Uploaded: task.FileSize,
			Total:    task.FileSize,
			Status:   StatusSuccess,
			FileName: task.FileName,
		})
	}

	return nil
}

func (u *Uploader) Cancel(task *UploadTask) {
	if task.cancelFunc != nil {
		task.cancelFunc()
		task.Status = StatusCanceled
	}
}

func (u *Uploader) UploadWithProgress(ctx context.Context, filePath string, parentID uint64, progressFunc func(ProgressInfo)) (*UploadTask, error) {
	task, err := u.CreateTask(filePath, parentID)
	if err != nil {
		return nil, err
	}

	if progressFunc != nil {
		progressFunc(ProgressInfo{
			TaskID:   task.ID,
			Phase:    "hashing",
			Progress: 0,
			Total:    task.FileSize,
			Status:   StatusHashing,
			FileName: task.FileName,
		})
	}

	err = u.CalculateHash(task, func(p float64) {
		if progressFunc != nil {
			progressFunc(ProgressInfo{
				TaskID:   task.ID,
				Phase:    "hashing",
				Progress: p,
				Total:    task.FileSize,
				Status:   StatusHashing,
				FileName: task.FileName,
			})
		}
	})
	if err != nil {
		if progressFunc != nil {
			progressFunc(ProgressInfo{
				TaskID:   task.ID,
				Phase:    "hashing",
				Status:   StatusFailed,
				Error:    err,
				FileName: task.FileName,
			})
		}
		return task, err
	}

	err = u.Upload(ctx, task, progressFunc)
	return task, err
}

func generateID() string {
	return fmt.Sprintf("%d", time.Now().UnixNano())
}

func FormatSpeed(bytesPerSec float64) string {
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
