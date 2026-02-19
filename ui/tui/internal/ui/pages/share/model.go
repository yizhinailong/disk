// Package share 分享管理页面
//
// 提供分享列表查看、取消分享和复制分享链接功能。
// 支持 vim 风格键盘导航、多选操作和确认对话框。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package share

import (
	"context"
	"fmt"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/yizhinailong/disk/ui/tui/internal/api"
	"github.com/yizhinailong/disk/ui/tui/internal/models"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/sharelist"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/statusbar"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/components/topbar"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/styles"
)

// =============================================================================
// 消息类型 (Message Types)
// =============================================================================

// SharesLoadedMsg 分享加载成功消息
type SharesLoadedMsg struct {
	Items      []models.Share    // 分享项目列表
	Pagination models.Pagination // 分页信息
}

// SharesLoadErrorMsg 分享加载失败消息
type SharesLoadErrorMsg struct {
	Error error // 错误信息
}

// ShareCancelledMsg 取消分享成功消息
type ShareCancelledMsg struct {
	Count int // 取消数量
}

// SwitchToFilesMsg 切换到文件页面消息
type SwitchToFilesMsg struct{}

// CopyLinkMsg 复制链接消息
type CopyLinkMsg struct {
	Link     string // 分享链接
	Password string // 访问密码（如有）
}

// =============================================================================
// 模型定义 (Model Definition)
// =============================================================================

// Model 分享管理页面模型
type Model struct {
	client    *api.Client     // API 客户端
	shareList sharelist.Model // 分享列表组件
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
	confirmMode  bool // 是否在确认模式
	confirmCount int  // 待确认的项目数量
}

// =============================================================================
// 构造函数 (Constructors)
// =============================================================================

// New 创建分享管理页面
func New(client *api.Client) Model {
	return Model{
		client:    client,
		page:      1,
		pageSize:  100,
		statusBar: statusbar.New(),
		shareList: sharelist.New(0, 0),
		topBar:    topbar.New(),
	}
}

// NewWithSize 创建指定尺寸的分享管理页面
func NewWithSize(client *api.Client, width, height int) Model {
	m := New(client)
	m.width = width
	m.height = height
	m.shareList = sharelist.New(width, height-4)
	return m
}

// =============================================================================
// 生命周期方法 (Lifecycle Methods)
// =============================================================================

// Init 初始化页面
func (m Model) Init() tea.Cmd {
	return m.loadShares()
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

	case SharesLoadedMsg:
		m.loading = false
		m.page = msg.Pagination.Page
		m.total = msg.Pagination.Total
		m.shareList.SetItems(msg.Items)
		m.updateStatusBar()
		return m, nil

	case SharesLoadErrorMsg:
		m.loading = false
		m.err = msg.Error.Error()
		m.statusBar.SetError(m.err)
		return m, nil

	case ShareCancelledMsg:
		m.confirmMode = false
		m.shareList.ClearSelection()
		m.statusBar.SetSuccess(fmt.Sprintf("已取消 %d 个分享", msg.Count))
		return m, m.loadShares()

	case CopyLinkMsg:
		linkInfo := msg.Link
		if msg.Password != "" {
			linkInfo += fmt.Sprintf(" (密码: %s)", msg.Password)
		}
		m.statusBar.SetInfo("分享链接: " + linkInfo)
		return m, nil
	}

	// 更新子组件
	var cmd tea.Cmd
	m.shareList, cmd = m.shareList.Update(msg)
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
		sections = append(sections, m.renderConfirmDialog())
	} else {
		sections = append(sections, m.shareList.View())
	}

	// 状态栏
	sections = append(sections, m.statusBar.RenderForShare())

	return lipgloss.JoinVertical(lipgloss.Left, sections...)
}

// =============================================================================
// 核心方法 (Core Methods)
// =============================================================================

// loadShares 加载分享列表
func (m Model) loadShares() tea.Cmd {
	return func() tea.Msg {
		ctx := context.Background()
		result, err := m.client.Share.List(ctx, m.page, m.pageSize)

		if err != nil {
			return SharesLoadErrorMsg{Error: err}
		}

		return SharesLoadedMsg{
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
		m.shareList.CursorDown()
		m.updateSelectionStatus()
		return m, nil

	case "k", "up":
		m.shareList.CursorUp()
		m.updateSelectionStatus()
		return m, nil

	case "g":
		m.shareList.CursorHome()
		m.updateSelectionStatus()
		return m, nil

	case "G":
		m.shareList.CursorEnd()
		m.updateSelectionStatus()
		return m, nil

	case " ":
		m.shareList.ToggleSelected()
		m.updateSelectionStatus()
		return m, nil

	case "a":
		if m.shareList.HasSelection() {
			m.shareList.ClearSelection()
		} else {
			m.shareList.SelectAll()
		}
		m.updateSelectionStatus()
		return m, nil

	case "enter", "y":
		return m.handleCopyLink()

	case "c":
		return m.handleCancel()

	case "R", "f5":
		m.loading = true
		m.statusBar.SetInfo("刷新中...")
		return m, m.loadShares()

	case "esc", "h", "left", "backspace", "q":
		return m, func() tea.Msg { return SwitchToFilesMsg{} }

	case "?":
		return m.handleHelp()
	}

	return m, nil
}

// handleConfirmKeyPress 处理确认模式下的按键
func (m Model) handleConfirmKeyPress(key string) (Model, tea.Cmd) {
	switch key {
	case "y", "Y":
		return m.executeCancel()

	case "n", "N", "esc":
		m.confirmMode = false
		m.confirmCount = 0
		m.statusBar.SetInfo("已取消")
		return m, nil
	}

	return m, nil
}

// handleCopyLink 处理复制链接操作
func (m Model) handleCopyLink() (Model, tea.Cmd) {
	item := m.shareList.SelectedItem()
	if item == nil {
		m.statusBar.SetError("请先选择一个分享")
		return m, nil
	}

	// 构建分享链接（这里使用 share_code，实际链接格式可能需要根据服务端配置调整）
	link := fmt.Sprintf("https://disk.example.com/s/%s", item.ShareCode)

	var password string
	if item.HasPassword {
		password = "******" // 密码不返回，需要用户自己记住
	}

	return m, func() tea.Msg {
		return CopyLinkMsg{
			Link:     link,
			Password: password,
		}
	}
}

// handleCancel 处理取消分享操作
func (m Model) handleCancel() (Model, tea.Cmd) {
	count := 0

	if m.shareList.HasSelection() {
		count = len(m.shareList.SelectedItems())
	} else if m.shareList.SelectedItem() != nil {
		count = 1
	}

	if count == 0 {
		m.statusBar.SetError("请先选择要取消的分享")
		return m, nil
	}

	// 进入确认模式
	m.confirmMode = true
	m.confirmCount = count
	return m, nil
}

// executeCancel 执行取消分享
func (m Model) executeCancel() (Model, tea.Cmd) {
	var shareIDs []string

	if m.shareList.HasSelection() {
		items := m.shareList.SelectedItems()
		for _, item := range items {
			shareIDs = append(shareIDs, item.ShareCode)
		}
	} else if item := m.shareList.SelectedItem(); item != nil {
		shareIDs = append(shareIDs, item.ShareCode)
	}

	m.statusBar.SetInfo(fmt.Sprintf("正在取消 %d 个分享...", len(shareIDs)))
	return m, m.doCancel(shareIDs)
}

// doCancel 执行取消分享
func (m Model) doCancel(shareIDs []string) tea.Cmd {
	return func() tea.Msg {
		ctx := context.Background()
		if err := m.client.Share.Cancel(ctx, shareIDs); err != nil {
			return SharesLoadErrorMsg{Error: err}
		}
		return ShareCancelledMsg{Count: len(shareIDs)}
	}
}

// handleHelp 显示帮助信息
func (m Model) handleHelp() (Model, tea.Cmd) {
	m.statusBar.SetInfo(styles.IconHelp + " j/k导航, Enter复制链接, c取消分享, a全选, R刷新, Esc返回")
	return m, nil
}

// =============================================================================
// 渲染方法 (Render Methods)
// =============================================================================

// renderConfirmDialog 渲染确认对话框
func (m *Model) renderConfirmDialog() string {
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

	content := fmt.Sprintf("%s 取消 %d 个分享？\n\n%s\n%s",
		styles.IconWarning,
		m.confirmCount,
		hintStyle.Render("取消后分享链接将立即失效"),
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

	m.shareList.SetSize(width, listHeight)
	m.statusBar.SetWidth(width)
	m.topBar.SetWidth(width)
}

// updateStatusBar 更新状态栏
func (m *Model) updateStatusBar() {
	count := m.shareList.Count()
	selected := len(m.shareList.SelectedItems())

	var msg string
	if selected > 0 {
		msg = fmt.Sprintf("已选中 %d / 共 %d 项", selected, count)
	} else if count > 0 {
		msg = fmt.Sprintf("共 %d 个分享", count)
	} else {
		msg = "暂无分享"
	}

	if m.total > m.pageSize {
		msg += fmt.Sprintf(" | 第 %d 页", m.page)
	}

	m.statusBar.SetInfo(msg)
}

// updateSelectionStatus 更新选择状态
func (m *Model) updateSelectionStatus() {
	selected := len(m.shareList.SelectedItems())
	count := m.shareList.Count()

	if selected > 0 {
		msg := fmt.Sprintf("已选中 %d / %d 项", selected, count)
		m.statusBar.SetInfo(msg)
	} else {
		if item := m.shareList.SelectedItem(); item != nil {
			name := getShareDisplayName(*item)
			expiry := sharelist.FormatExpiryTime(item.ExpiresAt)
			info := fmt.Sprintf("%s | 过期: %s", name, expiry)
			m.statusBar.SetInfo(info)
		}
	}
}

// getShareDisplayName 获取分享显示名称
func getShareDisplayName(share models.Share) string {
	if len(share.Files) == 0 {
		return share.ShareCode
	}
	if len(share.Files) == 1 {
		return share.Files[0].Name
	}
	return fmt.Sprintf("%s 等%d项", share.Files[0].Name, len(share.Files))
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

// Refresh 刷新分享列表
func (m *Model) Refresh() tea.Cmd {
	m.loading = true
	return m.loadShares()
}

// GetSelectedItems 返回选中的分享项目
func (m *Model) GetSelectedItems() []models.Share {
	return m.shareList.SelectedItems()
}

// GetCurrentItem 返回当前聚焦的项目
func (m *Model) GetCurrentItem() *models.Share {
	return m.shareList.SelectedItem()
}

// IsEmpty 返回分享列表是否为空
func (m *Model) IsEmpty() bool {
	return m.shareList.IsEmpty()
}

// IsConfirmMode 返回是否处于确认模式
func (m *Model) IsConfirmMode() bool {
	return m.confirmMode
}
