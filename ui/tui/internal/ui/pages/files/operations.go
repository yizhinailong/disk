// Package files 文件操作
//
// 提供文件操作的具体实现：上传、下载、重命名、删除、移动、复制、创建文件夹。
// 所有操作返回 tea.Cmd 以便在 Bubble Tea 框架中执行。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package files

import (
	"context"
	"fmt"
	"os"
	"path/filepath"

	tea "github.com/charmbracelet/bubbletea"

	"github.com/liufeng/disk/ui/tui/internal/downloader"
	"github.com/liufeng/disk/ui/tui/internal/uploader"
)

// OperationMsg 操作结果消息
type OperationMsg struct {
	Operation string // 操作类型（upload/download/delete/rename/move/copy/createFolder）
	Success   bool   // 是否成功
	Error     error  // 错误信息
	FileID    uint64 // 文件 ID
	FileName  string // 文件名
}

// UploadProgressMsg 上传进度消息
type UploadProgressMsg struct {
	TaskID   string              // 任务 ID
	Phase    string              // 当前阶段
	Progress float64             // 进度百分比
	Uploaded int64               // 已上传字节数
	Total    int64               // 总字节数
	Speed    string              // 当前速度
	Status   uploader.TaskStatus // 上传状态
	Error    error               // 错误信息
	FileName string              // 文件名
}

// DownloadProgressMsg 下载进度消息
type DownloadProgressMsg struct {
	TaskID     string                    // 任务 ID
	Progress   float64                   // 进度百分比
	Downloaded int64                     // 已下载字节数
	Total      int64                     // 总字节数
	Speed      string                    // 当前速度
	Status     downloader.DownloadStatus // 下载状态
	Error      error                     // 错误信息
	FileName   string                    // 文件名
}

// DoUpload 执行上传
//
// 参数:
//   - filePath: 本地文件路径
//
// 返回:
//   - tea.Cmd: 上传命令
func (m *Model) DoUpload(filePath string) tea.Cmd {
	return func() tea.Msg {
		u := uploader.New(m.client)
		ctx := context.Background()

		task, err := u.UploadWithProgress(ctx, filePath, m.currentFolder, func(info uploader.ProgressInfo) {
		})

		if err != nil {
			return OperationMsg{
				Operation: "upload",
				Success:   false,
				Error:     err,
				FileName:  filepath.Base(filePath),
			}
		}

		return OperationMsg{
			Operation: "upload",
			Success:   task.Status == uploader.StatusSuccess,
			Error:     task.Error,
			FileName:  task.FileName,
		}
	}
}

// DoDownload 执行下载
//
// 返回:
//   - tea.Cmd: 下载命令
func (m *Model) DoDownload() tea.Cmd {
	return func() tea.Msg {
		file := m.fileList.SelectedFile()
		if file == nil || file.IsFolder() {
			return OperationMsg{
				Operation: "download",
				Success:   false,
				Error:     fmt.Errorf("请选择一个文件"),
			}
		}

		downloadDir, err := getDownloadDir()
		if err != nil {
			return OperationMsg{
				Operation: "download",
				Success:   false,
				Error:     err,
			}
		}

		d := downloader.New(m.client)
		ctx := context.Background()

		task, err := d.DownloadWithProgress(ctx, file, downloadDir, func(info downloader.ProgressInfo) {
		})

		if err != nil {
			return OperationMsg{
				Operation: "download",
				Success:   false,
				Error:     err,
				FileID:    file.ID,
				FileName:  file.Name,
			}
		}

		return OperationMsg{
			Operation: "download",
			Success:   task.Status == downloader.StatusSuccess,
			Error:     task.Error,
			FileID:    file.ID,
			FileName:  file.Name,
		}
	}
}

// DoRename 执行重命名
//
// 参数:
//   - newName: 新文件名
//
// 返回:
//   - tea.Cmd: 重命名命令
func (m *Model) DoRename(newName string) tea.Cmd {
	return func() tea.Msg {
		file := m.fileList.SelectedFile()
		if file == nil {
			return OperationMsg{
				Operation: "rename",
				Success:   false,
				Error:     fmt.Errorf("未选择文件"),
			}
		}

		ctx := context.Background()
		_, err := m.client.File.Rename(ctx, file.ID, newName)

		return OperationMsg{
			Operation: "rename",
			Success:   err == nil,
			Error:     err,
			FileID:    file.ID,
			FileName:  newName,
		}
	}
}

// DoDelete 执行删除
//
// 返回:
//   - tea.Cmd: 删除命令
func (m *Model) DoDelete() tea.Cmd {
	return func() tea.Msg {
		var fileIDs []uint64

		if m.fileList.HasSelection() {
			for _, f := range m.fileList.SelectedFiles() {
				fileIDs = append(fileIDs, f.ID)
			}
		} else {
			file := m.fileList.SelectedFile()
			if file == nil {
				return OperationMsg{
					Operation: "delete",
					Success:   false,
					Error:     fmt.Errorf("未选择文件"),
				}
			}
			fileIDs = append(fileIDs, file.ID)
		}

		ctx := context.Background()
		err := m.client.File.Delete(ctx, fileIDs)

		return OperationMsg{
			Operation: "delete",
			Success:   err == nil,
			Error:     err,
		}
	}
}

// DoMove 执行移动
//
// 参数:
//   - targetFolderID: 目标文件夹 ID
//
// 返回:
//   - tea.Cmd: 移动命令
func (m *Model) DoMove(targetFolderID uint64) tea.Cmd {
	return func() tea.Msg {
		var fileIDs []uint64

		if m.fileList.HasSelection() {
			for _, f := range m.fileList.SelectedFiles() {
				fileIDs = append(fileIDs, f.ID)
			}
		} else {
			file := m.fileList.SelectedFile()
			if file == nil {
				return OperationMsg{
					Operation: "move",
					Success:   false,
					Error:     fmt.Errorf("未选择文件"),
				}
			}
			fileIDs = append(fileIDs, file.ID)
		}

		ctx := context.Background()
		err := m.client.File.Move(ctx, fileIDs, targetFolderID)

		return OperationMsg{
			Operation: "move",
			Success:   err == nil,
			Error:     err,
		}
	}
}

// DoCopy 执行复制
//
// 参数:
//   - targetFolderID: 目标文件夹 ID
//
// 返回:
//   - tea.Cmd: 复制命令
func (m *Model) DoCopy(targetFolderID uint64) tea.Cmd {
	return func() tea.Msg {
		var fileIDs []uint64

		if m.fileList.HasSelection() {
			for _, f := range m.fileList.SelectedFiles() {
				fileIDs = append(fileIDs, f.ID)
			}
		} else {
			file := m.fileList.SelectedFile()
			if file == nil {
				return OperationMsg{
					Operation: "copy",
					Success:   false,
					Error:     fmt.Errorf("未选择文件"),
				}
			}
			fileIDs = append(fileIDs, file.ID)
		}

		ctx := context.Background()
		_, err := m.client.File.Copy(ctx, fileIDs, targetFolderID)

		return OperationMsg{
			Operation: "copy",
			Success:   err == nil,
			Error:     err,
		}
	}
}

// DoCreateFolder 执行创建文件夹
//
// 参数:
//   - name: 文件夹名称
//
// 返回:
//   - tea.Cmd: 创建命令
func (m *Model) DoCreateFolder(name string) tea.Cmd {
	return func() tea.Msg {
		ctx := context.Background()
		_, err := m.client.Folder.Create(ctx, name, m.currentFolder)

		return OperationMsg{
			Operation: "createFolder",
			Success:   err == nil,
			Error:     err,
			FileName:  name,
		}
	}
}

// getDownloadDir 获取下载目录
//
// 返回:
//   - string: 下载目录路径
//   - error: 错误信息
func getDownloadDir() (string, error) {
	homeDir, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("无法获取用户目录: %w", err)
	}

	downloadDir := filepath.Join(homeDir, "Downloads")
	if _, err := os.Stat(downloadDir); os.IsNotExist(err) {
		if err := os.MkdirAll(downloadDir, 0755); err != nil {
			return "", fmt.Errorf("无法创建下载目录: %w", err)
		}
	}

	return downloadDir, nil
}

// FormatSize 格式化文件大小
//
// 参数:
//   - bytes: 字节数
//
// 返回:
//   - string: 格式化后的大小字符串
func FormatSize(bytes uint64) string {
	const KB = 1024
	const MB = KB * 1024
	const GB = MB * 1024

	switch {
	case bytes >= GB:
		return fmt.Sprintf("%.1f GB", float64(bytes)/float64(GB))
	case bytes >= MB:
		return fmt.Sprintf("%.1f MB", float64(bytes)/float64(MB))
	case bytes >= KB:
		return fmt.Sprintf("%.1f KB", float64(bytes)/float64(KB))
	default:
		return fmt.Sprintf("%d B", bytes)
	}
}
