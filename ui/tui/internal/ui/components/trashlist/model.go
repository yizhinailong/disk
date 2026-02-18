// Package trashlist 回收站列表组件
//
// 提供回收站项目的展示、导航和选择功能。
// 支持多选、单选、键盘导航和空状态提示。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package trashlist

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

// Model 回收站列表模型
//
// 管理回收站列表的展示、导航和选择状态。
type Model struct {
	list     list.Model         // bubbles 列表组件
	items    []models.TrashItem // 回收站项目列表
	selected map[uint64]bool    // 选中状态（按回收站项目 ID）
	width    int                // 组件宽度
	height   int                // 组件高度
}

// TrashItemWrapper 列表项包装
//
// 实现 list.Item 接口，包装回收站项目信息。
type TrashItemWrapper struct {
	item models.TrashItem // 回收站项目信息
}

// FilterValue 实现 list.Item 接口
func (i TrashItemWrapper) FilterValue() string {
	return i.item.Name
}

// GetItem 获取回收站项目信息
func (i TrashItemWrapper) GetItem() models.TrashItem {
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
	wrapped, ok := wrappedList.(TrashItemWrapper)
	if !ok {
		return
	}

	item := wrapped.GetItem()

	// 判断是否为文件夹
	isFolder := item.Type == models.FileTypeFolder

	// 选中状态样式
	var style lipgloss.Style
	if index == m.Index() {
		style = styles.SelectedStyle
	} else if isFolder {
		style = styles.FolderStyle
	} else {
		style = styles.FileStyle
	}

	// 图标
	icon := styles.GetFileIcon(item.Name, isFolder)

	// 选中指示器
	prefix := "  "
	if index == m.Index() {
		prefix = styles.IconSelected + " "
	}

	// 大小
	sizeStr := ""
	if !isFolder {
		sizeStr = styles.FormatSizeShort(item.Size)
	}

	// 计算可用宽度
	availableWidth := m.Width() - 4 // 减去 padding
	if availableWidth < 20 {
		availableWidth = 20
	}

	// 名称长度
	nameMaxLen := availableWidth - 20 // 预留大小和时间位置
	if nameMaxLen < 10 {
		nameMaxLen = 10
	}
	name := item.Name
	if len(name) > nameMaxLen {
		name = name[:nameMaxLen-3] + "..."
	}

	// 渲染行
	line := fmt.Sprintf("%s%s %s", prefix, icon, name)
	if sizeStr != "" {
		// 计算需要的填充
		lineLen := lipgloss.Width(line)
		fillLen := availableWidth - lineLen - len(sizeStr)
		if fillLen > 0 {
			line += strings.Repeat(".", fillLen)
		}
		line += " " + sizeStr
	}

	fmt.Fprint(w, style.Render(line))
}

// formatRelativeTime 格式化相对时间
//
// 参数:
//   - t: 时间点
//   - isFuture: true 表示未来时间（过期时间），false 表示过去时间（删除时间）
//
// 返回:
//   - string: 相对时间字符串（如 "3天前"、"27天后过期"）
func formatRelativeTime(t time.Time, isFuture bool) string {
	now := time.Now()
	var diff time.Duration
	var suffix string

	if isFuture {
		diff = t.Sub(now)
		suffix = "后过期"
	} else {
		diff = now.Sub(t)
		suffix = "前"
	}

	if diff < 0 {
		if isFuture {
			return "已过期"
		}
		return "刚刚"
	}

	// 计算各时间单位
	seconds := int(diff.Seconds())
	minutes := seconds / 60
	hours := minutes / 60
	days := hours / 24

	switch {
	case days > 0:
		return fmt.Sprintf("%d天%s", days, suffix)
	case hours > 0:
		return fmt.Sprintf("%d小时%s", hours, suffix)
	case minutes > 0:
		return fmt.Sprintf("%d分钟%s", minutes, suffix)
	default:
		if isFuture {
			return "即将过期"
		}
		return "刚刚"
	}
}

// New 创建回收站列表
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

// SetItems 设置回收站项目列表
func (m *Model) SetItems(items []models.TrashItem) {
	m.items = items
	wrapped := make([]list.Item, len(items))
	for i, item := range items {
		wrapped[i] = TrashItemWrapper{item: item}
	}
	m.list.SetItems(wrapped)
}

// Items 获取所有回收站项目
func (m *Model) Items() []models.TrashItem {
	return m.items
}

// SelectedItem 获取当前选中的回收站项目
func (m *Model) SelectedItem() *models.TrashItem {
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

// Count 获取回收站项目数量
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

// SelectedItems 获取所有选中的回收站项目
func (m *Model) SelectedItems() []models.TrashItem {
	var result []models.TrashItem
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
		styles.IconEmpty + " 回收站为空",
		"",
		"按 [r] 刷新",
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
