// Package files 文件列表页面
//
// 提供文件浏览、导航、选择和操作功能。
// 支持键盘导航、多选、排序和文件操作（上传/下载/删除等）。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package files

import (
	"context"
	"fmt"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/yizhinailong/disk/ui/tui/internal/api"
	"github.com/yizhinailong/disk/ui/tui/internal/models"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/breadcrumb"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/filelist"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/statusbar"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/topbar"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/transfer"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/styles"
	"github.com/yizhinailong/disk/ui/tui/internal/uploader"
)

// FilesLoadedMsg 文件加载成功消息
type FilesLoadedMsg struct {
	Files      []models.File           // 文件列表
	Pagination models.Pagination       // 分页信息
	Breadcrumb []models.BreadcrumbItem // 面包屑路径
	ParentID   uint64                  // 父文件夹 ID
}

// FilesLoadErrorMsg 文件加载失败消息
type FilesLoadErrorMsg struct {
	Error error // 错误信息
}

// OpenFolderMsg 打开文件夹消息
type OpenFolderMsg struct {
	FolderID uint64 // 文件夹 ID
}

// GoBackMsg 返回上级目录消息
type GoBackMsg struct{}

// SwitchToTrashMsg 切换到回收站页面消息
type SwitchToTrashMsg struct{}

// SwitchToShareMsg 切换到分享管理页面消息
type SwitchToShareMsg struct{}

// Model 文件列表页面模型
type Model struct {
	client        *api.Client // API 客户端
	currentFolder uint64      // 当前文件夹 ID
	parentID      uint64      // 父文件夹 ID
	username      string      // 当前用户名
	usedSpace     uint64      // 已用空间
	totalSpace    uint64      // 总空间

	fileList    filelist.Model   // 文件列表组件
	breadcrumb  breadcrumb.Model // 面包屑组件
	statusBar   statusbar.Model  // 状态栏组件
	transferBar transfer.Model   // 传输进度组件
	topBar      topbar.Model     // 顶部状态栏组件

	page     int // 当前页码
	pageSize int // 每页数量
	total    int // 总数量

	sortBy    string // 排序字段
	sortOrder string // 排序方向

	loading bool   // 是否加载中
	err     string // 错误消息
	width   int    // 组件宽度
	height  int    // 组件高度
}

// New 创建文件列表页面
//
// 参数:
//   - client: API 客户端
//
// 返回:
//   - Model: 页面模型
func New(client *api.Client) Model {
	return Model{
		client:        client,
		currentFolder: 0,
		parentID:      0,
		page:          1,
		pageSize:      100,
		sortBy:        "name",
		sortOrder:     "asc",
		breadcrumb:    breadcrumb.New(),
		statusBar:     statusbar.New(),
		transferBar:   transfer.New(),
		topBar:        topbar.New(),
	}
}

// NewWithSize 创建指定尺寸的文件列表页面
//
// 参数:
//   - client: API 客户端
//   - width: 宽度
//   - height: 高度
//
// 返回:
//   - Model: 页面模型
func NewWithSize(client *api.Client, width, height int) Model {
	m := New(client)
	m.width = width
	m.height = height
	m.fileList = filelist.New(width, height-4)
	return m
}

// Init 初始化页面
func (m Model) Init() tea.Cmd {
	return m.loadFiles()
}

// Update 更新页面
func (m Model) Update(msg tea.Msg) (Model, tea.Cmd) {
	var cmds []tea.Cmd

	switch msg := msg.(type) {
	case tea.KeyMsg:
		return m.handleKeyPress(msg)

	case tea.WindowSizeMsg:
		m.setSize(msg.Width, msg.Height)
		return m, nil

	case FilesLoadedMsg:
		m.loading = false
		m.currentFolder = msg.ParentID
		m.page = msg.Pagination.Page
		m.total = msg.Pagination.Total
		m.fileList.SetFiles(msg.Files)
		m.breadcrumb.SetPath(msg.Breadcrumb)
		m.updateStatusBar()
		return m, nil

	case FilesLoadErrorMsg:
		m.loading = false
		m.err = msg.Error.Error()
		m.statusBar.SetError(m.err)
		return m, nil

	case OpenFolderMsg:
		m.parentID = m.currentFolder
		m.currentFolder = msg.FolderID
		m.loading = true
		return m, m.loadFiles()

	case GoBackMsg:
		return m.handleGoBack()

	case OperationMsg:
		return m.handleOperationResult(msg)

	case ShareCreatedMsg:
		return m.handleShareCreated(msg)

	case UploadProgressMsg:
		return m.handleUploadProgress(msg)
	}

	var cmd tea.Cmd
	m.fileList, cmd = m.fileList.Update(msg)
	cmds = append(cmds, cmd)

	return m, tea.Batch(cmds...)
}

// View 渲染页面
func (m Model) View() string {
	var sections []string

	// 顶部状态栏
	sections = append(sections, m.topBar.View())

	sections = append(sections, m.breadcrumb.View())

	if m.loading {
		loadingHeight := m.height - 4
		if loadingHeight < 3 {
			loadingHeight = 3
		}
		loading := lipgloss.NewStyle().
			Width(m.width).
			Height(loadingHeight).
			Align(lipgloss.Center, lipgloss.Center).
			Render(styles.LoadingStyle.Render(styles.IconLoading + " 加载中..."))
		sections = append(sections, loading)
	} else {
		sections = append(sections, m.fileList.View())
	}

	if m.transferBar.HasActiveTasks() {
		sections = append(sections, m.transferBar.View())
	}

	sections = append(sections, m.statusBar.View())

	return lipgloss.JoinVertical(lipgloss.Left, sections...)
}

// loadFiles 加载文件列表
func (m Model) loadFiles() tea.Cmd {
	return func() tea.Msg {
		ctx := context.Background()
		result, err := m.client.File.List(ctx, api.ListOptions{
			ParentID:  m.currentFolder,
			Page:      m.page,
			PageSize:  m.pageSize,
			SortBy:    m.sortBy,
			SortOrder: m.sortOrder,
		})

		if err != nil {
			return FilesLoadErrorMsg{Error: err}
		}

		breadcrumbPath := m.buildBreadcrumb(result.Items)

		return FilesLoadedMsg{
			Files:      result.Items,
			Pagination: result.Pagination,
			Breadcrumb: breadcrumbPath,
			ParentID:   m.currentFolder,
		}
	}
}

func (m *Model) buildBreadcrumb(files []models.File) []models.BreadcrumbItem {
	if m.currentFolder == 0 {
		return []models.BreadcrumbItem{{ID: 0, Name: "根目录"}}
	}

	// TODO: Backend should return complete path info
	return []models.BreadcrumbItem{
		{ID: 0, Name: "根目录"},
		{ID: m.currentFolder, Name: "当前目录"},
	}
}

func (m Model) handleKeyPress(msg tea.KeyMsg) (Model, tea.Cmd) {
	key := msg.String()

	switch key {
	case "j", "down":
		m.fileList.CursorDown()
		m.updateSelectionStatus()
		return m, nil

	case "k", "up":
		m.fileList.CursorUp()
		m.updateSelectionStatus()
		return m, nil

	case "g":
		m.fileList.CursorHome()
		m.updateSelectionStatus()
		return m, nil

	case "G":
		m.fileList.CursorEnd()
		m.updateSelectionStatus()
		return m, nil

	case "enter", "l", "right":
		return m.handleEnter()

	case "h", "left", "backspace":
		return m.handleGoBack()

	case "u":
		// 上传功能需要文件选择器，暂时显示提示
		m.statusBar.SetInfo(styles.IconUploading + " 上传: 请使用命令行 disk-tui upload <文件>")
		return m, nil

	case "d":
		return m.handleDownload()

	case "r":
		return m.handleRename()

	case "m":
		return m.handleMove()

	case "c":
		return m.handleCopy()

	case "x", "dd":
		return m.handleDelete()

	case "n":
		return m.handleCreateFolder()

	case "R", "f5":
		m.loading = true
		m.statusBar.SetInfo("刷新中...")
		return m, m.loadFiles()

	case "?":
		return m.handleHelp()

	case " ":
		m.fileList.ToggleSelected()
		m.updateSelectionStatus()
		return m, nil

	case "a":
		if m.fileList.HasSelection() {
			m.fileList.ClearSelection()
		} else {
			m.fileList.SelectAll()
		}
		m.updateSelectionStatus()
		return m, nil

	case "q", "esc":
		if m.currentFolder == 0 {
			return m, tea.Quit
		}
		return m.handleGoBack()

	case "t":
		return m.changeSort("updated_at")

	case "s":
		return m.changeSort("size")

	case "T":
		return m, func() tea.Msg { return SwitchToTrashMsg{} }

	case "S":
		return m, func() tea.Msg { return SwitchToShareMsg{} }

	case "y":
		return m.handleCreateShare()

	case "ctrl+d", "ctrl+u":
		return m, nil
	}

	return m, nil
}

func (m Model) handleEnter() (Model, tea.Cmd) {
	file := m.fileList.SelectedFile()
	if file == nil {
		return m, nil
	}

	if file.IsFolder() {
		m.parentID = m.currentFolder
		m.currentFolder = file.ID
		m.loading = true
		m.page = 1
		return m, m.loadFiles()
	}

	m.statusBar.SetInfo("按 [d] 下载文件: " + file.Name)
	return m, nil
}

func (m Model) handleGoBack() (Model, tea.Cmd) {
	if m.currentFolder == 0 {
		return m, nil
	}

	m.currentFolder = m.parentID
	m.parentID = 0 // TODO: Get real parent ID from backend
	m.loading = true
	m.page = 1
	m.breadcrumb.Pop()
	return m, m.loadFiles()
}

func (m Model) changeSort(sortBy string) (Model, tea.Cmd) {
	if m.sortBy == sortBy {
		if m.sortOrder == "asc" {
			m.sortOrder = "desc"
		} else {
			m.sortOrder = "asc"
		}
	} else {
		m.sortBy = sortBy
		m.sortOrder = "asc"
	}

	m.loading = true
	sortDesc := "按名称"
	switch sortBy {
	case "updated_at":
		sortDesc = "按修改时间"
	case "size":
		sortDesc = "按大小"
	}
	if m.sortOrder == "desc" {
		sortDesc += " (降序)"
	}
	m.statusBar.SetInfo("排序: " + sortDesc)

	return m, m.loadFiles()
}

func (m *Model) setSize(width, height int) {
	m.width = width
	m.height = height

	topBarHeight := 1
	statusBarHeight := 1
	breadcrumbHeight := 1
	transferHeight := 0
	if m.transferBar.HasActiveTasks() {
		transferHeight = 2
	}

	fileListHeight := height - topBarHeight - statusBarHeight - breadcrumbHeight - transferHeight
	if fileListHeight < 5 {
		fileListHeight = 5
	}

	m.fileList.SetSize(width, fileListHeight)
	m.breadcrumb.SetWidth(width)
	m.statusBar.SetWidth(width)
	m.transferBar.SetWidth(width)
	m.topBar.SetWidth(width)
}

func (m *Model) updateStatusBar() {
	count := m.fileList.Count()
	selected := len(m.fileList.SelectedFiles())

	var msg string
	if selected > 0 {
		msg = fmt.Sprintf("已选中 %d / 共 %d 项", selected, count)
	} else {
		msg = fmt.Sprintf("共 %d 项", count)
	}

	if m.total > m.pageSize {
		msg += fmt.Sprintf(" | 第 %d 页", m.page)
	}

	m.statusBar.SetInfo(msg)
}

func (m *Model) updateSelectionStatus() {
	selected := len(m.fileList.SelectedFiles())
	count := m.fileList.Count()

	if selected > 0 {
		msg := fmt.Sprintf("已选中 %d / %d 项", selected, count)
		m.statusBar.SetInfo(msg)
	} else {
		if file := m.fileList.SelectedFile(); file != nil {
			info := file.Name
			if !file.IsFolder() {
				info += " | " + styles.FormatSize(file.Size)
			}
			m.statusBar.SetInfo(info)
		}
	}
}

// SetClient 设置 API 客户端
func (m *Model) SetClient(client *api.Client) {
	m.client = client
}

// CurrentFolder 返回当前文件夹 ID
func (m *Model) CurrentFolder() uint64 {
	return m.currentFolder
}

// IsLoading 返回是否正在加载
func (m *Model) IsLoading() bool {
	return m.loading
}

// Error 返回错误消息
func (m *Model) Error() string {
	return m.err
}

// Refresh 刷新文件列表
func (m *Model) Refresh() tea.Cmd {
	m.loading = true
	return m.loadFiles()
}

// NavigateTo 导航到指定文件夹
func (m *Model) NavigateTo(folderID uint64) tea.Cmd {
	m.parentID = m.currentFolder
	m.currentFolder = folderID
	m.loading = true
	m.page = 1
	return m.loadFiles()
}

// GoToRoot 导航到根目录
func (m *Model) GoToRoot() tea.Cmd {
	m.currentFolder = 0
	m.parentID = 0
	m.loading = true
	m.page = 1
	m.breadcrumb.Reset()
	return m.loadFiles()
}

// GetSelectedFiles 返回选中的文件
func (m *Model) GetSelectedFiles() []models.File {
	return m.fileList.SelectedFiles()
}

// GetCurrentFile 返回当前聚焦的文件
func (m *Model) GetCurrentFile() *models.File {
	return m.fileList.SelectedFile()
}

func (m Model) handleDownload() (Model, tea.Cmd) {
	file := m.fileList.SelectedFile()
	if file == nil {
		m.statusBar.SetError("请先选择一个文件")
		return m, nil
	}
	if file.IsFolder() {
		m.statusBar.SetError("暂不支持下载文件夹")
		return m, nil
	}
	m.statusBar.SetInfo(styles.IconDownloading + " 正在下载: " + file.Name)
	return m, m.DoDownload()
}

func (m Model) handleDelete() (Model, tea.Cmd) {
	count := 1
	if m.fileList.HasSelection() {
		count = len(m.fileList.SelectedFiles())
	}
	m.statusBar.SetInfo(fmt.Sprintf("正在删除 %d 个项目...", count))
	return m, tea.Sequence(m.DoDelete(), m.loadFiles())
}

func (m Model) handleRename() (Model, tea.Cmd) {
	file := m.fileList.SelectedFile()
	if file == nil {
		m.statusBar.SetError("请先选择一个文件或文件夹")
		return m, nil
	}
	m.statusBar.SetInfo(fmt.Sprintf("重命名: %s (请在命令行使用 :rename <新名称>)", file.Name))
	return m, nil
}

func (m Model) handleMove() (Model, tea.Cmd) {
	if !m.fileList.HasSelection() && m.fileList.SelectedFile() == nil {
		m.statusBar.SetError("请先选择要移动的文件")
		return m, nil
	}
	m.statusBar.SetInfo("移动: 请使用命令行 :move <目标文件夹ID>")
	return m, nil
}

func (m Model) handleCopy() (Model, tea.Cmd) {
	if !m.fileList.HasSelection() && m.fileList.SelectedFile() == nil {
		m.statusBar.SetError("请先选择要复制的文件")
		return m, nil
	}
	m.statusBar.SetInfo("复制: 请使用命令行 :copy <目标文件夹ID>")
	return m, nil
}

func (m Model) handleCreateFolder() (Model, tea.Cmd) {
	m.statusBar.SetInfo("新建文件夹: 请使用命令行 :mkdir <文件夹名>")
	return m, nil
}

func (m Model) handleHelp() (Model, tea.Cmd) {
	m.statusBar.SetInfo(styles.IconHelp + " 帮助: j/k导航, d下载, x删除, r重命名, y分享, n新建, S分享列表, R刷新")
	return m, nil
}

func (m Model) handleCreateShare() (Model, tea.Cmd) {
	if !m.fileList.HasSelection() && m.fileList.SelectedFile() == nil {
		m.statusBar.SetError("请先选择要分享的文件")
		return m, nil
	}

	count := 1
	if m.fileList.HasSelection() {
		count = len(m.fileList.SelectedFiles())
	}

	m.statusBar.SetInfo(fmt.Sprintf("正在创建分享 (%d 个文件)...", count))
	return m, m.DoCreateShare(7, "")
}

func (m Model) handleShareCreated(msg ShareCreatedMsg) (Model, tea.Cmd) {
	if msg.Error != nil {
		m.statusBar.SetError("创建分享失败: " + msg.Error.Error())
		return m, nil
	}

	shareInfo := msg.ShareLink
	if msg.Password != "" {
		shareInfo += " (密码: " + msg.Password + ")"
	}
	m.statusBar.SetSuccess("分享成功: " + shareInfo + " | 过期: " + msg.ExpiresAt)
	m.fileList.ClearSelection()
	return m, nil
}

func (m Model) handleOperationResult(msg OperationMsg) (Model, tea.Cmd) {
	if msg.Success {
		switch msg.Operation {
		case "download":
			m.statusBar.SetInfo(styles.IconDownloading + " 下载完成: " + msg.FileName)
		case "delete":
			m.statusBar.SetInfo("删除成功")
			return m, m.loadFiles()
		case "rename":
			m.statusBar.SetInfo("重命名成功: " + msg.FileName)
			return m, m.loadFiles()
		case "move":
			m.statusBar.SetInfo("移动成功")
			return m, m.loadFiles()
		case "copy":
			m.statusBar.SetInfo("复制成功")
			return m, m.loadFiles()
		case "createFolder":
			m.statusBar.SetInfo("文件夹创建成功: " + msg.FileName)
			return m, m.loadFiles()
		default:
			m.statusBar.SetInfo(msg.Operation + " 完成")
		}
	} else {
		errMsg := "操作失败"
		if msg.Error != nil {
			errMsg = msg.Error.Error()
		}
		m.statusBar.SetError(errMsg)
	}
	return m, nil
}

func (m Model) handleUploadProgress(msg UploadProgressMsg) (Model, tea.Cmd) {
	taskStatus := transfer.TaskStatusRunning
	switch msg.Status {
	case uploader.StatusPending:
		taskStatus = transfer.TaskStatusPending
	case uploader.StatusHashing:
		taskStatus = transfer.TaskStatusRunning
	case uploader.StatusUploading:
		taskStatus = transfer.TaskStatusRunning
	case uploader.StatusSuccess:
		taskStatus = transfer.TaskStatusCompleted
	case uploader.StatusFailed:
		taskStatus = transfer.TaskStatusError
	case uploader.StatusCanceled:
		taskStatus = transfer.TaskStatusError
	}

	existingTask := m.findTransferTask(msg.TaskID)
	if existingTask == nil {
		m.transferBar.AddTask(transfer.Task{
			ID:        msg.TaskID,
			Type:      transfer.TaskUpload,
			Filename:  msg.FileName,
			Progress:  msg.Progress,
			Speed:     msg.Speed,
			Status:    taskStatus,
			TotalSize: uint64(msg.Total),
			BytesDone: uint64(msg.Uploaded),
		})
	} else {
		m.transferBar.UpdateTask(msg.TaskID, msg.Progress, msg.Speed)
		m.transferBar.SetTaskStatus(msg.TaskID, taskStatus)
	}

	if msg.Error != nil {
		m.transferBar.SetTaskError(msg.TaskID, msg.Error.Error())
	}

	return m, nil
}

func (m *Model) findTransferTask(id string) *transfer.Task {
	for _, t := range m.transferBar.Tasks() {
		if t.ID == id {
			return &t
		}
	}
	return nil
}
