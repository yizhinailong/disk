// Package uploader 文件上传管理
//
// 提供文件上传功能，支持 MD5 哈希计算、秒传检测和进度回调。
// 上传流程：计算哈希 -> 检测秒传 -> 分片上传。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package uploader

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"sync"
	"sync/atomic"
	"time"

	"github.com/yizhinailong/disk/ui/tui/internal/api"
	"github.com/yizhinailong/disk/ui/tui/internal/models"
)

// DefaultChunkSize 默认分片大小（5MB）
const DefaultChunkSize = 5 * 1024 * 1024

// TaskStatus 上传任务状态
type TaskStatus string

// 上传状态常量
const (
	StatusPending   TaskStatus = "pending"   // 等待中
	StatusHashing   TaskStatus = "hashing"   // 计算哈希中
	StatusUploading TaskStatus = "uploading" // 上传中
	StatusSuccess   TaskStatus = "success"   // 上传成功
	StatusFailed    TaskStatus = "failed"    // 上传失败
	StatusCanceled  TaskStatus = "canceled"  // 已取消
)

// UploadTask 上传任务
type UploadTask struct {
	ID         string             // 任务唯一标识
	FilePath   string             // 本地文件路径
	FileName   string             // 文件名
	FileSize   int64              // 文件大小（字节）
	FileHash   string             // 文件 MD5 哈希
	ParentID   uint64             // 目标文件夹 ID
	ChunkSize  int                // 分片大小
	Chunks     int                // 分片数量
	Progress   float64            // 上传进度（百分比）
	Status     TaskStatus         // 上传状态
	Error      error              // 错误信息
	Speed      string             // 上传速度
	Uploaded   int64              // 已上传字节数
	uploadID   string             // 服务端上传 ID
	cancelFunc context.CancelFunc // 取消函数
}

// ProgressInfo 上传进度信息
type ProgressInfo struct {
	TaskID      string     // 任务 ID
	Phase       string     // 当前阶段（hashing/uploading）
	Progress    float64    // 进度百分比
	Uploaded    int64      // 已上传字节数
	Total       int64      // 总字节数
	Speed       string     // 当前速度
	Status      TaskStatus // 上传状态
	Error       error      // 错误信息
	FileName    string     // 文件名
	ChunkIndex  int        // 当前分片索引
	TotalChunks int        // 总分片数
}

// Uploader 上传器
type Uploader struct {
	client       *api.Client   // API 客户端
	chunkSize    int           // 分片大小
	concurrency  int           // 并发上传数
	stateManager *StateManager // 状态管理器
}

// New 创建上传器
//
// 参数:
//   - client: API 客户端
//
// 返回:
//   - *Uploader: 上传器实例
func New(client *api.Client) *Uploader {
	return &Uploader{
		client:      client,
		chunkSize:   DefaultChunkSize,
		concurrency: 3,
	}
}

// SetConcurrency 设置并发上传数
//
// 参数:
//   - n: 并发数（最小为 1）
func (u *Uploader) SetConcurrency(n int) {
	if n < 1 {
		n = 1
	}
	u.concurrency = n
}

// SetStateManager 设置状态管理器
//
// 参数:
//   - mgr: 状态管理器
func (u *Uploader) SetStateManager(mgr *StateManager) {
	u.stateManager = mgr
}

// CreateTask 创建上传任务
//
// 参数:
//   - filePath: 本地文件路径
//   - parentID: 目标文件夹 ID
//
// 返回:
//   - *UploadTask: 上传任务
//   - error: 错误信息
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

// CalculateHash 计算文件哈希
//
// 参数:
//   - task: 上传任务
//   - progressFunc: 进度回调函数
//
// 返回:
//   - error: 错误信息
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

// Upload 执行上传
//
// 参数:
//   - ctx: 上下文
//   - task: 上传任务
//   - progressFunc: 进度回调函数
//
// 返回:
//   - error: 错误信息
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
				TaskID:      task.ID,
				Phase:       "uploading",
				Progress:    100,
				Total:       task.FileSize,
				Status:      StatusSuccess,
				FileName:    task.FileName,
				TotalChunks: task.Chunks,
			})
		}
		return nil
	}

	task.uploadID = initResp.UploadID
	if initResp.ChunkSize > 0 {
		task.ChunkSize = initResp.ChunkSize
	}

	uploadedChunksMap := make(map[int]bool)
	for _, idx := range initResp.UploadedChunks {
		uploadedChunksMap[idx] = true
	}

	file, err := os.Open(task.FilePath)
	if err != nil {
		task.Status = StatusFailed
		task.Error = err
		return err
	}
	defer file.Close()

	totalChunks := task.Chunks
	var uploadedCount int
	for range uploadedChunksMap {
		uploadedCount++
	}
	task.Uploaded = int64(uploadedCount) * int64(task.ChunkSize)
	if task.Uploaded > task.FileSize {
		task.Uploaded = task.FileSize
	}

	if progressFunc != nil {
		progress := float64(task.Uploaded) / float64(task.FileSize) * 100
		progressFunc(ProgressInfo{
			TaskID:      task.ID,
			Phase:       "uploading",
			Progress:    progress,
			Uploaded:    task.Uploaded,
			Total:       task.FileSize,
			Status:      StatusUploading,
			FileName:    task.FileName,
			TotalChunks: totalChunks,
		})
	}

	var uploadedChunks []int
	for i := 0; i < totalChunks; i++ {
		if uploadedChunksMap[i] {
			uploadedChunks = append(uploadedChunks, i)
		}
	}

	uploadChunk := func(idx int) error {
		offset := int64(idx) * int64(task.ChunkSize)
		chunkData := make([]byte, task.ChunkSize)
		n, err := file.ReadAt(chunkData, offset)
		if err != nil && err != io.EOF {
			return err
		}
		chunkData = chunkData[:n]

		hash := md5.Sum(chunkData)
		chunkHash := hex.EncodeToString(hash[:])

		if err := u.client.File.UploadChunk(ctx, task.uploadID, idx, chunkHash, chunkData); err != nil {
			return err
		}

		uploadedChunks = append(uploadedChunks, idx)
		uploadedChunksMap[idx] = true
		task.Uploaded += int64(n)
		if task.Uploaded > task.FileSize {
			task.Uploaded = task.FileSize
		}

		if u.stateManager != nil {
			state := &UploadState{
				UploadID:       task.uploadID,
				FilePath:       task.FilePath,
				FileName:       task.FileName,
				FileSize:       task.FileSize,
				FileHash:       task.FileHash,
				ParentID:       task.ParentID,
				TotalChunks:    totalChunks,
				UploadedChunks: append([]int{}, uploadedChunks...),
				ChunkSize:      task.ChunkSize,
				CreatedAt:      time.Now().Unix(),
				UpdatedAt:      time.Now().Unix(),
			}
			u.stateManager.Save(state)
		}

		if progressFunc != nil {
			progress := float64(task.Uploaded) / float64(task.FileSize) * 100
			progressFunc(ProgressInfo{
				TaskID:      task.ID,
				Phase:       "uploading",
				Progress:    progress,
				Uploaded:    task.Uploaded,
				Total:       task.FileSize,
				Status:      StatusUploading,
				FileName:    task.FileName,
				ChunkIndex:  idx,
				TotalChunks: totalChunks,
			})
		}

		return nil
	}

	if u.concurrency <= 1 {
		for chunkIndex := 0; chunkIndex < totalChunks; chunkIndex++ {
			if uploadedChunksMap[chunkIndex] {
				continue
			}
			if ctx.Err() != nil {
				break
			}
			if err := uploadChunk(chunkIndex); err != nil {
				task.Status = StatusFailed
				task.Error = err
				return err
			}
		}
	} else {
		sem := make(chan struct{}, u.concurrency)
		var wg sync.WaitGroup
		var uploadErr atomic.Value
		var mu sync.Mutex

		for chunkIndex := 0; chunkIndex < totalChunks; chunkIndex++ {
			if uploadedChunksMap[chunkIndex] {
				continue
			}
			if ctx.Err() != nil {
				break
			}

			wg.Add(1)
			go func(idx int) {
				defer wg.Done()

				select {
				case sem <- struct{}{}:
				case <-ctx.Done():
					return
				}
				defer func() { <-sem }()

				if err := uploadErr.Load(); err != nil {
					return
				}

				offset := int64(idx) * int64(task.ChunkSize)
				chunkData := make([]byte, task.ChunkSize)
				n, err := file.ReadAt(chunkData, offset)
				if err != nil && err != io.EOF {
					uploadErr.Store(err)
					return
				}
				chunkData = chunkData[:n]

				hash := md5.Sum(chunkData)
				chunkHash := hex.EncodeToString(hash[:])

				if err := u.client.File.UploadChunk(ctx, task.uploadID, idx, chunkHash, chunkData); err != nil {
					uploadErr.Store(err)
					return
				}

				mu.Lock()
				uploadedChunks = append(uploadedChunks, idx)
				uploadedChunksMap[idx] = true
				task.Uploaded += int64(n)
				if task.Uploaded > task.FileSize {
					task.Uploaded = task.FileSize
				}

				if u.stateManager != nil {
					chunks := make([]int, 0, len(uploadedChunks))
					for i := 0; i < totalChunks; i++ {
						if uploadedChunksMap[i] {
							chunks = append(chunks, i)
						}
					}
					state := &UploadState{
						UploadID:       task.uploadID,
						FilePath:       task.FilePath,
						FileName:       task.FileName,
						FileSize:       task.FileSize,
						FileHash:       task.FileHash,
						ParentID:       task.ParentID,
						TotalChunks:    totalChunks,
						UploadedChunks: chunks,
						ChunkSize:      task.ChunkSize,
						CreatedAt:      time.Now().Unix(),
						UpdatedAt:      time.Now().Unix(),
					}
					u.stateManager.Save(state)
				}
				mu.Unlock()

				if progressFunc != nil {
					mu.Lock()
					progress := float64(task.Uploaded) / float64(task.FileSize) * 100
					info := ProgressInfo{
						TaskID:      task.ID,
						Phase:       "uploading",
						Progress:    progress,
						Uploaded:    task.Uploaded,
						Total:       task.FileSize,
						Status:      StatusUploading,
						FileName:    task.FileName,
						ChunkIndex:  idx,
						TotalChunks: totalChunks,
					}
					mu.Unlock()
					progressFunc(info)
				}
			}(chunkIndex)
		}

		wg.Wait()

		if err := ctx.Err(); err != nil {
			task.Status = StatusCanceled
			task.Error = err
			u.client.File.CancelUpload(context.Background(), task.uploadID)
			return err
		}

		if err, ok := uploadErr.Load().(error); ok && err != nil {
			task.Status = StatusFailed
			task.Error = err
			return err
		}
	}

	if err := ctx.Err(); err != nil {
		task.Status = StatusCanceled
		task.Error = err
		u.client.File.CancelUpload(context.Background(), task.uploadID)
		return err
	}

	_, err = u.client.File.CompleteUpload(ctx, task.uploadID)
	if err != nil {
		task.Status = StatusFailed
		task.Error = err
		return err
	}

	if u.stateManager != nil {
		u.stateManager.Delete(task.uploadID)
	}

	task.Status = StatusSuccess
	task.Progress = 100
	task.Uploaded = task.FileSize

	if progressFunc != nil {
		progressFunc(ProgressInfo{
			TaskID:      task.ID,
			Phase:       "uploading",
			Progress:    100,
			Uploaded:    task.FileSize,
			Total:       task.FileSize,
			Status:      StatusSuccess,
			FileName:    task.FileName,
			TotalChunks: totalChunks,
		})
	}

	return nil
}

// Cancel 取消上传任务
//
// 参数:
//   - task: 上传任务
func (u *Uploader) Cancel(task *UploadTask) {
	if task.cancelFunc != nil {
		task.cancelFunc()
		task.Status = StatusCanceled
	}
}

// UploadWithProgress 带进度回调的上传
//
// 创建任务、计算哈希并执行上传，通过回调实时报告进度。
//
// 参数:
//   - ctx: 上下文
//   - filePath: 本地文件路径
//   - parentID: 目标文件夹 ID
//   - progressFunc: 进度回调函数
//
// 返回:
//   - *UploadTask: 上传任务
//   - error: 错误信息
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

// generateID 生成任务 ID
func generateID() string {
	return fmt.Sprintf("%d", time.Now().UnixNano())
}

// FormatSpeed 格式化上传速度
//
// 参数:
//   - bytesPerSec: 每秒字节数
//
// 返回:
//   - string: 格式化后的速度字符串
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
