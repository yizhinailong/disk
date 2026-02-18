// Package sharelist 分享列表组件
//
// 提供分享项目的展示、导航和选择功能。
// 支持多选、单选、键盘导航和空状态提示。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package sharelist

import (
	"fmt"
	"io"
	"strings"
	"time"

	"github.com/charmbracelet/bubbles/list"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/yizhinailong/disk/ui/tui/internal/models"
	"github.com/yizhinailong/disk/ui/tui/internal/ui/styles"
)

// Model 分享列表模型
//
// 管理分享列表的展示、导航和选择状态。
type Model struct {
	list     list.Model      // bubbles 列表组件
	items    []models.Share  // 分享项目列表
	selected map[uint64]bool // 选中状态（按分享 ID）
	width    int             // 组件宽度
	height   int             // 组件高度
}

// ShareItemWrapper 列表项包装
//
// 实现 list.Item 接口，包装分享项目信息。
type ShareItemWrapper struct {
	item models.Share // 分享项目信息
}

// FilterValue 实现 list.Item 接口
func (i ShareItemWrapper) FilterValue() string {
	// 返回第一个文件名用于过滤
	if len(i.item.Files) > 0 {
		return i.item.Files[0].Name
	}
	return i.item.ShareCode
}

// GetItem 获取分享项目信息
func (i ShareItemWrapper) GetItem() models.Share {
	return i.item
}

// itemDelegate 列表项渲染代理
type itemDelegate struct{}

// Height 实现 list.Delegate 接口
func (d itemDelegate) Height() int {
	return 1
}

// Spacing 实现 list.Delegate 接口
func (d itemDelegate) Spacing() int {
	return 0
}

// Update 实现 list.Delegate 接口
func (d itemDelegate) Update(_ tea.Msg, _ *list.Model) tea.Cmd {
	return nil
}

// Render 实现 list.Delegate 接口
func (d itemDelegate) Render(w io.Writer, m list.Model, index int, wrappedList list.Item) {
	wrapped, ok := wrappedList.(ShareItemWrapper)
	if !ok {
		return
	}

	item := wrapped.GetItem()

	// 选中状态样式
	var style lipgloss.Style
	if index == m.Index() {
		style = styles.SelectedStyle
	} else {
		style = styles.ListItemStyle
	}

	// 图标
	icon := styles.IconLink
	if item.HasPassword {
		icon = styles.IconLocked
	}

	// 选中指示器
	prefix := "  "
	if index == m.Index() {
		prefix = styles.IconSelected + " "
	}

	// 获取分享的文件名
	name := getShareName(item)

	// 状态标识
	statusIcon := ""
	if item.Status == "expired" {
		statusIcon = styles.ErrorStyle.Render("[已过期]")
	} else if item.Status == "cancelled" {
		statusIcon = styles.MutedStyle.Render("[已取消]")
	}

	// 计算可用宽度
	availableWidth := m.Width() - 4 // 减去 padding
	if availableWidth < 20 {
		availableWidth = 20
	}

	// 名称长度
	nameMaxLen := availableWidth - 25 // 预留状态和统计位置
	if nameMaxLen < 10 {
		nameMaxLen = 10
	}
	if len(name) > nameMaxLen {
		name = name[:nameMaxLen-3] + "..."
	}

	// 统计信息
	stats := fmt.Sprintf("%d阅/%d载", item.ViewCount, item.DownloadCount)

	// 渲染行
	line := fmt.Sprintf("%s%s %s", prefix, icon, name)

	// 计算需要的填充
	lineLen := lipgloss.Width(line)
	fillLen := availableWidth - lineLen - len(stats) - lipgloss.Width(statusIcon)
	if fillLen > 0 {
		line += strings.Repeat(".", fillLen)
	}
	line += " " + stats
	if statusIcon != "" {
		line += " " + statusIcon
	}

	fmt.Fprint(w, style.Render(line))
}

// getShareName 获取分享显示名称
func getShareName(share models.Share) string {
	if len(share.Files) == 0 {
		return share.ShareCode
	}
	if len(share.Files) == 1 {
		return share.Files[0].Name
	}
	// 多个文件时显示第一个文件名 + 数量
	return fmt.Sprintf("%s 等%d项", share.Files[0].Name, len(share.Files))
}

// formatExpiryTime 格式化过期时间
//
// 参数:
//   - t: 过期时间（nil 表示永久有效）
//
// 返回:
//   - string: 过期时间字符串
func formatExpiryTime(t *time.Time) string {
	if t == nil {
		return "永久"
	}

	now := time.Now()
	diff := t.Sub(now)

	if diff < 0 {
		return "已过期"
	}

	days := int(diff.Hours() / 24)
	hours := int(diff.Hours()) % 24

	switch {
	case days > 0:
		return fmt.Sprintf("%d天后", days)
	case hours > 0:
		return fmt.Sprintf("%d小时后", hours)
	default:
		return "即将过期"
	}
}

// New 创建分享列表
func New(width, height int) Model {
	l := list.New([]list.Item{}, itemDelegate{}, width, height)
	l.SetShowStatusBar(false)
	l.SetFilteringEnabled(false)
	l.SetShowHelp(false)
	l.SetShowTitle(false)

	return Model{
		list:     l,
		selected: make(map[uint64]bool),
		width:    width,
		height:   height,
	}
}

// SetItems 设置分享项目列表
func (m *Model) SetItems(items []models.Share) {
	m.items = items
	wrapped := make([]list.Item, len(items))
	for i, item := range items {
		wrapped[i] = ShareItemWrapper{item: item}
	}
	m.list.SetItems(wrapped)
}

// Items 获取所有分享项目
func (m *Model) Items() []models.Share {
	return m.items
}

// SelectedItem 获取当前选中的分享项目
func (m *Model) SelectedItem() *models.Share {
	if len(m.items) == 0 {
		return nil
	}
	i := m.list.Index()
	if i >= 0 && i < len(m.items) {
		return &m.items[i]
	}
	return nil
}

// SelectedIndex 获取当前选中索引
func (m *Model) SelectedIndex() int {
	return m.list.Index()
}

// Count 获取分享项目数量
func (m *Model) Count() int {
	return len(m.items)
}

// IsEmpty 是否为空
func (m *Model) IsEmpty() bool {
	return len(m.items) == 0
}

// ToggleSelected 切换选中状态
func (m *Model) ToggleSelected() {
	if item := m.SelectedItem(); item != nil {
		m.selected[item.ID] = !m.selected[item.ID]
	}
}

// IsSelected 检查是否选中
func (m *Model) IsSelected(id uint64) bool {
	return m.selected[id]
}

// SelectedItems 获取所有选中的分享项目
func (m *Model) SelectedItems() []models.Share {
	var result []models.Share
	for _, item := range m.items {
		if m.selected[item.ID] {
			result = append(result, item)
		}
	}
	return result
}

// HasSelection 是否有选中项
func (m *Model) HasSelection() bool {
	return len(m.selected) > 0
}

// ClearSelection 清除选择
func (m *Model) ClearSelection() {
	m.selected = make(map[uint64]bool)
}

// SelectAll 全选
func (m *Model) SelectAll() {
	for _, item := range m.items {
		m.selected[item.ID] = true
	}
}

// Init 初始化
func (m Model) Init() tea.Cmd {
	return nil
}

// Update 更新
func (m Model) Update(msg tea.Msg) (Model, tea.Cmd) {
	var cmd tea.Cmd
	m.list, cmd = m.list.Update(msg)
	return m, cmd
}

// View 渲染
func (m Model) View() string {
	if m.IsEmpty() {
		return m.renderEmpty()
	}
	return m.list.View()
}

// renderEmpty 渲染空状态
func (m *Model) renderEmpty() string {
	emptyStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("#6c6c6c")).
		Align(lipgloss.Center)

	lines := []string{
		"",
		styles.IconEmpty + " 暂无分享",
		"",
		"在文件列表中按 [y] 创建分享",
		"按 [Esc] 返回文件列表",
		"",
	}

	var result string
	for _, line := range lines {
		result += emptyStyle.Width(m.width).Render(line) + "\n"
	}
	return result
}

// SetSize 设置大小
func (m *Model) SetSize(width, height int) {
	m.width = width
	m.height = height
	m.list.SetSize(width, height)
}

// Width 获取宽度
func (m *Model) Width() int {
	return m.width
}

// Height 获取高度
func (m *Model) Height() int {
	return m.height
}

// Index 获取当前索引
func (m *Model) Index() int {
	return m.list.Index()
}

// CursorUp 上移
func (m *Model) CursorUp() {
	m.list.CursorUp()
}

// CursorDown 下移
func (m *Model) CursorDown() {
	m.list.CursorDown()
}

// CursorHome 跳到顶部
func (m *Model) CursorHome() {
	if len(m.items) > 0 {
		m.list.Select(0)
	}
}

// CursorEnd 跳到底部
func (m *Model) CursorEnd() {
	if len(m.items) > 0 {
		m.list.Select(len(m.items) - 1)
	}
}

// Select 选择指定索引
func (m *Model) Select(index int) {
	if index >= 0 && index < len(m.items) {
		m.list.Select(index)
	}
}

// FormatExpiryTime 导出格式化函数
func FormatExpiryTime(t *time.Time) string {
	return formatExpiryTime(t)
}
