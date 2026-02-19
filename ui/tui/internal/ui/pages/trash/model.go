// Package trash 回收站页面
//
// 提供回收站文件管理功能，包括查看、恢复、彻底删除和清空回收站。
// 支持 vim 风格键盘导航、多选操作和确认对话框。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package trash

import (
	"context"
	"fmt"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/yizhinailong/disk/ui/tui/internal/api"
	"github.com/yizhinailong/disk/ui/tui/internal/models"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/statusbar"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/topbar"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/trashlist"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/styles"
)

// =============================================================================
// 消息类型 (Message Types)
// =============================================================================

// TrashLoadedMsg 回收站加载成功消息
type TrashLoadedMsg struct {
	Items      []models.TrashItem // 回收站项目列表
	Pagination models.Pagination  // 分页信息
}

// TrashLoadErrorMsg 回收站加载失败消息
type TrashLoadErrorMsg struct {
	Error error // 错误信息
}

// TrashRestoredMsg 恢复成功消息
type TrashRestoredMsg struct {
	Summary models.TrashRestoreSummary // 恢复摘要
}

// TrashDeletedMsg 彻底删除成功消息
type TrashDeletedMsg struct {
	Summary struct {
		Total        int // 总数
		SuccessCount int // 成功数
		FailureCount int // 失败数
	}
}

// SwitchToFilesMsg 切换到文件页面消息
type SwitchToFilesMsg struct{}

// =============================================================================
// 模型定义 (Model Definition)
// =============================================================================

// Model 回收站页面模型
type Model struct {
	client    *api.Client     // API 客户端
	trashList trashlist.Model // 回收站列表组件
	statusBar statusbar.Model // 状态栏组件
	topBar    topbar.Model    // 顶部状态栏组件

	page     int // 当前页码
	pageSize int // 每页数量
	total    int // 总数量

	loading bool   // 是否加载中
	err     string // 错误消息
	width   int    // 组件宽度
	height  int    // 组件高度

	// 确认模式
	confirmMode   bool   // 是否在确认模式
	confirmAction string // "delete" 或 "empty"
	confirmCount  int    // 待确认的项目数量
}

// =============================================================================
// 构造函数 (Constructors)
// =============================================================================

// New 创建回收站页面
//
// 参数:
//   - client: API 客户端
//
// 返回:
//   - Model: 页面模型
func New(client *api.Client) Model {
	return Model{
		client:    client,
		page:      1,
		pageSize:  100,
		statusBar: statusbar.New(),
		trashList: trashlist.New(0, 0),
		topBar:    topbar.New(),
	}
}

// NewWithSize 创建指定尺寸的回收站页面
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
	m.trashList = trashlist.New(width, height-4)
	return m
}

// =============================================================================
// 生命周期方法 (Lifecycle Methods)
// =============================================================================

// Init 初始化页面
func (m Model) Init() tea.Cmd {
	return m.loadTrash()
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

	case TrashLoadedMsg:
		m.loading = false
		m.page = msg.Pagination.Page
		m.total = msg.Pagination.Total
		m.trashList.SetItems(msg.Items)
		m.updateStatusBar()
		return m, nil

	case TrashLoadErrorMsg:
		m.loading = false
		m.err = msg.Error.Error()
		m.statusBar.SetError(m.err)
		return m, nil

	case TrashRestoredMsg:
		m.statusBar.SetSuccess(fmt.Sprintf("已恢复 %d 个项目 (%d 失败)",
			msg.Summary.SuccessCount, msg.Summary.FailureCount))
		return m, m.loadTrash()

	case TrashDeletedMsg:
		m.confirmMode = false
		m.trashList.ClearSelection()
		m.statusBar.SetSuccess(fmt.Sprintf("已删除 %d 个项目 (%d 失败)",
			msg.Summary.SuccessCount, msg.Summary.FailureCount))
		return m, m.loadTrash()
	}

	// 更新子组件
	var cmd tea.Cmd
	m.trashList, cmd = m.trashList.Update(msg)
	cmds = append(cmds, cmd)

	return m, tea.Batch(cmds...)
}

// View 渲染页面
func (m Model) View() string {
	var sections []string

	// 顶部状态栏
	sections = append(sections, m.topBar.View())

	// 内容区域
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
	} else if m.confirmMode {
		// 确认对话框
		sections = append(sections, m.renderConfirmDialog())
	} else {
		sections = append(sections, m.trashList.View())
	}

	// 状态栏
	sections = append(sections, m.statusBar.RenderForTrash())

	return lipgloss.JoinVertical(lipgloss.Left, sections...)
}

// =============================================================================
// 核心方法 (Core Methods)
// =============================================================================

// loadTrash 加载回收站列表
func (m Model) loadTrash() tea.Cmd {
	return func() tea.Msg {
		ctx := context.Background()
		result, err := m.client.Trash.List(ctx, m.page, m.pageSize)

		if err != nil {
			return TrashLoadErrorMsg{Error: err}
		}

		return TrashLoadedMsg{
			Items:      result.Items,
			Pagination: result.Pagination,
		}
	}
}

// handleKeyPress 处理键盘输入
func (m Model) handleKeyPress(msg tea.KeyMsg) (Model, tea.Cmd) {
	key := msg.String()

	// 确认模式下的按键处理
	if m.confirmMode {
		return m.handleConfirmKeyPress(key)
	}

	switch key {
	case "j", "down":
		m.trashList.CursorDown()
		m.updateSelectionStatus()
		return m, nil

	case "k", "up":
		m.trashList.CursorUp()
		m.updateSelectionStatus()
		return m, nil

	case "g":
		m.trashList.CursorHome()
		m.updateSelectionStatus()
		return m, nil

	case "G":
		m.trashList.CursorEnd()
		m.updateSelectionStatus()
		return m, nil

	case " ":
		m.trashList.ToggleSelected()
		m.updateSelectionStatus()
		return m, nil

	case "a":
		if m.trashList.HasSelection() {
			m.trashList.ClearSelection()
		} else {
			m.trashList.SelectAll()
		}
		m.updateSelectionStatus()
		return m, nil

	case "r":
		return m.handleRestore()

	case "x", "dd":
		return m.handleDelete()

	case "D", "shift+d":
		return m.handleEmptyTrash()

	case "R", "f5":
		m.loading = true
		m.statusBar.SetInfo("刷新中...")
		return m, m.loadTrash()

	case "esc", "h", "left", "backspace":
		return m, func() tea.Msg { return SwitchToFilesMsg{} }

	case "?":
		return m.handleHelp()

	case "q":
		return m, tea.Quit
	}

	return m, nil
}

// handleConfirmKeyPress 处理确认模式下的按键
func (m Model) handleConfirmKeyPress(key string) (Model, tea.Cmd) {
	switch key {
	case "y", "Y":
		return m.executeConfirmAction()

	case "n", "N", "esc":
		m.confirmMode = false
		m.confirmAction = ""
		m.confirmCount = 0
		m.statusBar.SetInfo("已取消")
		return m, nil
	}

	return m, nil
}

// handleRestore 处理恢复操作
func (m Model) handleRestore() (Model, tea.Cmd) {
	var trashIDs []uint64

	if m.trashList.HasSelection() {
		items := m.trashList.SelectedItems()
		for _, item := range items {
			trashIDs = append(trashIDs, item.ID)
		}
	} else if item := m.trashList.SelectedItem(); item != nil {
		trashIDs = append(trashIDs, item.ID)
	}

	if len(trashIDs) == 0 {
		m.statusBar.SetError("请先选择要恢复的项目")
		return m, nil
	}

	m.statusBar.SetInfo(fmt.Sprintf("正在恢复 %d 个项目...", len(trashIDs)))
	return m, m.doRestore(trashIDs)
}

// doRestore 执行恢复操作
func (m Model) doRestore(trashIDs []uint64) tea.Cmd {
	return func() tea.Msg {
		ctx := context.Background()
		result, err := m.client.Trash.Restore(ctx, trashIDs)

		if err != nil {
			return TrashLoadErrorMsg{Error: err}
		}

		return TrashRestoredMsg{Summary: result.Summary}
	}
}

// handleDelete 处理彻底删除操作
func (m Model) handleDelete() (Model, tea.Cmd) {
	count := 0

	if m.trashList.HasSelection() {
		count = len(m.trashList.SelectedItems())
	} else if m.trashList.SelectedItem() != nil {
		count = 1
	}

	if count == 0 {
		m.statusBar.SetError("请先选择要删除的项目")
		return m, nil
	}

	// 进入确认模式
	m.confirmMode = true
	m.confirmAction = "delete"
	m.confirmCount = count
	return m, nil
}

// handleEmptyTrash 处理清空回收站操作
func (m Model) handleEmptyTrash() (Model, tea.Cmd) {
	if m.trashList.IsEmpty() {
		m.statusBar.SetInfo("回收站已为空")
		return m, nil
	}

	// 进入确认模式
	m.confirmMode = true
	m.confirmAction = "empty"
	m.confirmCount = m.trashList.Count()
	return m, nil
}

// executeConfirmAction 执行确认的操作
func (m Model) executeConfirmAction() (Model, tea.Cmd) {
	switch m.confirmAction {
	case "delete":
		var trashIDs []uint64

		if m.trashList.HasSelection() {
			items := m.trashList.SelectedItems()
			for _, item := range items {
				trashIDs = append(trashIDs, item.ID)
			}
		} else if item := m.trashList.SelectedItem(); item != nil {
			trashIDs = append(trashIDs, item.ID)
		}

		m.statusBar.SetInfo(fmt.Sprintf("正在彻底删除 %d 个项目...", len(trashIDs)))
		return m, m.doDelete(trashIDs)

	case "empty":
		// 清空回收站：选中所有项目并删除
		m.trashList.SelectAll()
		items := m.trashList.SelectedItems()
		var trashIDs []uint64
		for _, item := range items {
			trashIDs = append(trashIDs, item.ID)
		}

		m.statusBar.SetInfo("正在清空回收站...")
		return m, m.doDelete(trashIDs)
	}

	m.confirmMode = false
	return m, nil
}

// doDelete 执行彻底删除操作
func (m Model) doDelete(trashIDs []uint64) tea.Cmd {
	return func() tea.Msg {
		ctx := context.Background()
		result, err := m.client.Trash.Delete(ctx, trashIDs)

		if err != nil {
			return TrashLoadErrorMsg{Error: err}
		}

		return TrashDeletedMsg{
			Summary: struct {
				Total        int
				SuccessCount int
				FailureCount int
			}{
				Total:        result.Summary.Total,
				SuccessCount: result.Summary.SuccessCount,
				FailureCount: result.Summary.FailureCount,
			},
		}
	}
}

// handleHelp 显示帮助信息
func (m Model) handleHelp() (Model, tea.Cmd) {
	m.statusBar.SetInfo(styles.IconHelp + " j/k导航, r恢复, x彻底删除, D清空, a全选, R刷新, Esc返回")
	return m, nil
}

// =============================================================================
// 渲染方法 (Render Methods)
// =============================================================================

// renderConfirmDialog 渲染确认对话框
func (m *Model) renderConfirmDialog() string {
	var actionText string
	var warningIcon string

	switch m.confirmAction {
	case "delete":
		actionText = fmt.Sprintf("彻底删除 %d 个项目？", m.confirmCount)
		warningIcon = styles.IconWarning
	case "empty":
		actionText = fmt.Sprintf("清空回收站？共 %d 个项目", m.confirmCount)
		warningIcon = styles.IconWarning
	default:
		actionText = "确认操作？"
		warningIcon = ""
	}

	dialogStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(styles.ColorWarning).
		Padding(1, 2).
		Width(m.width - 4)

	contentStyle := lipgloss.NewStyle().
		Foreground(styles.ColorWarning).
		Bold(true)

	hintStyle := lipgloss.NewStyle().
		Foreground(styles.ColorTextMuted)

	content := fmt.Sprintf("%s %s\n\n%s\n%s",
		warningIcon,
		actionText,
		hintStyle.Render("此操作不可撤销！"),
		hintStyle.Render("[y] 确认  [n] 取消"),
	)

	// 居中显示
	dialogHeight := 7
	availableHeight := m.height - 4
	topPadding := (availableHeight - dialogHeight) / 2
	if topPadding < 0 {
		topPadding = 0
	}

	centerStyle := lipgloss.NewStyle().
		Width(m.width).
		Height(availableHeight).
		Align(lipgloss.Center, lipgloss.Center)

	dialog := contentStyle.Render(content)
	dialog = dialogStyle.Render(dialog)

	return centerStyle.Render(dialog)
}

// =============================================================================
// 辅助方法 (Helper Methods)
// =============================================================================

// setSize 设置尺寸
func (m *Model) setSize(width, height int) {
	m.width = width
	m.height = height

	topBarHeight := 1
	statusBarHeight := 1

	listHeight := height - topBarHeight - statusBarHeight
	if listHeight < 5 {
		listHeight = 5
	}

	m.trashList.SetSize(width, listHeight)
	m.statusBar.SetWidth(width)
	m.topBar.SetWidth(width)
}

// updateStatusBar 更新状态栏
func (m *Model) updateStatusBar() {
	count := m.trashList.Count()
	selected := len(m.trashList.SelectedItems())

	var msg string
	if selected > 0 {
		msg = fmt.Sprintf("已选中 %d / 共 %d 项", selected, count)
	} else if count > 0 {
		msg = fmt.Sprintf("共 %d 项", count)
	} else {
		msg = "回收站为空"
	}

	if m.total > m.pageSize {
		msg += fmt.Sprintf(" | 第 %d 页", m.page)
	}

	m.statusBar.SetInfo(msg)
}

// updateSelectionStatus 更新选择状态
func (m *Model) updateSelectionStatus() {
	selected := len(m.trashList.SelectedItems())
	count := m.trashList.Count()

	if selected > 0 {
		msg := fmt.Sprintf("已选中 %d / %d 项", selected, count)
		m.statusBar.SetInfo(msg)
	} else {
		if item := m.trashList.SelectedItem(); item != nil {
			info := item.Name
			if item.Type != models.FileTypeFolder {
				info += " | " + styles.FormatSize(item.Size)
			}
			m.statusBar.SetInfo(info)
		}
	}
}

// =============================================================================
// 公共方法 (Public Methods)
// =============================================================================

// SetClient 设置 API 客户端
func (m *Model) SetClient(client *api.Client) {
	m.client = client
}

// IsLoading 返回是否正在加载
func (m *Model) IsLoading() bool {
	return m.loading
}

// Error 返回错误消息
func (m *Model) Error() string {
	return m.err
}

// Refresh 刷新回收站列表
func (m *Model) Refresh() tea.Cmd {
	m.loading = true
	return m.loadTrash()
}

// GetSelectedItems 返回选中的回收站项目
func (m *Model) GetSelectedItems() []models.TrashItem {
	return m.trashList.SelectedItems()
}

// GetCurrentItem 返回当前聚焦的项目
func (m *Model) GetCurrentItem() *models.TrashItem {
	return m.trashList.SelectedItem()
}

// IsEmpty 返回回收站是否为空
func (m *Model) IsEmpty() bool {
	return m.trashList.IsEmpty()
}

// IsConfirmMode 返回是否处于确认模式
func (m *Model) IsConfirmMode() bool {
	return m.confirmMode
}
