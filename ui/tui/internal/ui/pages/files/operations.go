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

type OperationMsg struct {
	Operation string
	Success   bool
	Error     error
	FileID    uint64
	FileName  string
}

type UploadProgressMsg struct {
	TaskID   string
	Phase    string
	Progress float64
	Uploaded int64
	Total    int64
	Speed    string
	Status   uploader.TaskStatus
	Error    error
	FileName string
}

type DownloadProgressMsg struct {
	TaskID     string
	Progress   float64
	Downloaded int64
	Total      int64
	Speed      string
	Status     downloader.DownloadStatus
	Error      error
	FileName   string
}

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
