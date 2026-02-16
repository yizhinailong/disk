// Package files implements the file list page for the TUI application.
package files

import (
	"context"
	"fmt"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/liufeng/disk/ui/tui/internal/api"
	"github.com/liufeng/disk/ui/tui/internal/models"
	"github.com/liufeng/disk/ui/tui/internal/ui/components/breadcrumb"
	"github.com/liufeng/disk/ui/tui/internal/ui/components/filelist"
	"github.com/liufeng/disk/ui/tui/internal/ui/components/statusbar"
	"github.com/liufeng/disk/ui/tui/internal/ui/components/transfer"
	"github.com/liufeng/disk/ui/tui/internal/ui/styles"
)

// FilesLoadedMsg is dispatched when files are successfully loaded.
type FilesLoadedMsg struct {
	Files      []models.File
	Pagination models.Pagination
	Breadcrumb []models.BreadcrumbItem
	ParentID   uint64
}

// FilesLoadErrorMsg is dispatched when file loading fails.
type FilesLoadErrorMsg struct {
	Error error
}

// OpenFolderMsg is dispatched to open a folder.
type OpenFolderMsg struct {
	FolderID uint64
}

// GoBackMsg is dispatched to navigate to parent directory.
type GoBackMsg struct{}

// Model represents the file list page.
type Model struct {
	client        *api.Client
	currentFolder uint64
	parentID      uint64

	fileList    filelist.Model
	breadcrumb  breadcrumb.Model
	statusBar   statusbar.Model
	transferBar transfer.Model

	page     int
	pageSize int
	total    int

	sortBy    string
	sortOrder string

	loading bool
	err     string
	width   int
	height  int
}

// New creates a new file list page model.
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
	}
}

// NewWithSize creates a new file list page with specified dimensions.
func NewWithSize(client *api.Client, width, height int) Model {
	m := New(client)
	m.width = width
	m.height = height
	m.fileList = filelist.New(width, height-4)
	return m
}

// Init initializes the model.
func (m Model) Init() tea.Cmd {
	return m.loadFiles()
}

// Update handles messages and updates the model.
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
	}

	var cmd tea.Cmd
	m.fileList, cmd = m.fileList.Update(msg)
	cmds = append(cmds, cmd)

	return m, tea.Batch(cmds...)
}

// View renders the file list page.
func (m Model) View() string {
	var sections []string

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
		m.statusBar.SetInfo(styles.IconUploading + " 上传功能开发中...")
		return m, nil

	case "d":
		m.statusBar.SetInfo(styles.IconDownloading + " 下载功能开发中...")
		return m, nil

	case "r":
		m.statusBar.SetInfo("重命名功能开发中...")
		return m, nil

	case "m":
		m.statusBar.SetInfo("移动功能开发中...")
		return m, nil

	case "c":
		m.statusBar.SetInfo("复制功能开发中...")
		return m, nil

	case "x", "dd":
		m.statusBar.SetInfo("删除功能开发中...")
		return m, nil

	case "n":
		m.statusBar.SetInfo("新建文件夹功能开发中...")
		return m, nil

	case "R", "f5":
		m.loading = true
		m.statusBar.SetInfo("刷新中...")
		return m, m.loadFiles()

	case "?":
		m.statusBar.SetInfo(styles.IconHelp + " 帮助功能开发中...")
		return m, nil

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

	statusBarHeight := 1
	breadcrumbHeight := 1
	transferHeight := 0
	if m.transferBar.HasActiveTasks() {
		transferHeight = 2
	}

	fileListHeight := height - statusBarHeight - breadcrumbHeight - transferHeight
	if fileListHeight < 5 {
		fileListHeight = 5
	}

	m.fileList.SetSize(width, fileListHeight)
	m.breadcrumb.SetWidth(width)
	m.statusBar.SetWidth(width)
	m.transferBar.SetWidth(width)
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

// SetClient sets the API client.
func (m *Model) SetClient(client *api.Client) {
	m.client = client
}

// CurrentFolder returns the current folder ID.
func (m *Model) CurrentFolder() uint64 {
	return m.currentFolder
}

// IsLoading returns whether files are being loaded.
func (m *Model) IsLoading() bool {
	return m.loading
}

// Error returns the error message if any.
func (m *Model) Error() string {
	return m.err
}

// Refresh reloads the file list.
func (m *Model) Refresh() tea.Cmd {
	m.loading = true
	return m.loadFiles()
}

// NavigateTo navigates to the specified folder.
func (m *Model) NavigateTo(folderID uint64) tea.Cmd {
	m.parentID = m.currentFolder
	m.currentFolder = folderID
	m.loading = true
	m.page = 1
	return m.loadFiles()
}

// GoToRoot navigates to the root folder.
func (m *Model) GoToRoot() tea.Cmd {
	m.currentFolder = 0
	m.parentID = 0
	m.loading = true
	m.page = 1
	m.breadcrumb.Reset()
	return m.loadFiles()
}

// GetSelectedFiles returns the selected files.
func (m *Model) GetSelectedFiles() []models.File {
	return m.fileList.SelectedFiles()
}

// GetCurrentFile returns the currently focused file.
func (m *Model) GetCurrentFile() *models.File {
	return m.fileList.SelectedFile()
}
