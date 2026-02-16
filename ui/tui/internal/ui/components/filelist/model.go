// Package filelist 文件列表组件
package filelist

import (
	"fmt"
	"io"
	"strings"

	"github.com/charmbracelet/bubbles/list"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/liufeng/disk/ui/tui/internal/models"
	"github.com/liufeng/disk/ui/tui/internal/ui/styles"
)

// Model 文件列表模型
type Model struct {
	list     list.Model
	files    []models.File
	selected map[uint64]bool
	width    int
	height   int
}

// FileItem 列表项包装
type FileItem struct {
	file models.File
}

// FilterValue 实现 list.Item 接口
func (i FileItem) FilterValue() string {
	return i.file.Name
}

// GetFile 获取文件信息
func (i FileItem) GetFile() models.File {
	return i.file
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
	item, ok := wrappedList.(FileItem)
	if !ok {
		return
	}

	file := item.GetFile()

	// 选中状态样式
	var style lipgloss.Style
	if index == m.Index() {
		style = styles.SelectedStyle
	} else if file.IsFolder() {
		style = styles.FolderStyle
	} else {
		style = styles.FileStyle
	}

	// 图标
	icon := styles.GetFileIcon(file.Name, file.IsFolder())

	// 选中指示器
	prefix := "  "
	if index == m.Index() {
		prefix = styles.IconSelected + " "
	}

	// 大小
	sizeStr := ""
	if !file.IsFolder() {
		sizeStr = styles.FormatSize(file.Size)
	} else if file.ItemCount > 0 {
		sizeStr = fmt.Sprintf("%d 项", file.ItemCount)
	}

	// 计算可用宽度
	availableWidth := m.Width() - 4 // 减去 padding
	if availableWidth < 20 {
		availableWidth = 20
	}

	// 名称长度
	nameMaxLen := availableWidth - 15 // 预留大小和图标位置
	if nameMaxLen < 10 {
		nameMaxLen = 10
	}
	name := file.Name
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

// New 创建文件列表
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

// SetFiles 设置文件列表
func (m *Model) SetFiles(files []models.File) {
	m.files = files
	items := make([]list.Item, len(files))
	for i, f := range files {
		items[i] = FileItem{file: f}
	}
	m.list.SetItems(items)
}

// SelectedFile 获取当前选中的文件
func (m *Model) SelectedFile() *models.File {
	if len(m.files) == 0 {
		return nil
	}
	i := m.list.Index()
	if i >= 0 && i < len(m.files) {
		return &m.files[i]
	}
	return nil
}

// SelectedIndex 获取当前选中索引
func (m *Model) SelectedIndex() int {
	return m.list.Index()
}

// Files 获取所有文件
func (m *Model) Files() []models.File {
	return m.files
}

// IsEmpty 是否为空
func (m *Model) IsEmpty() bool {
	return len(m.files) == 0
}

// Count 获取文件数量
func (m *Model) Count() int {
	return len(m.files)
}

// ToggleSelected 切换选中状态
func (m *Model) ToggleSelected() {
	if file := m.SelectedFile(); file != nil {
		m.selected[file.ID] = !m.selected[file.ID]
	}
}

// IsSelected 检查是否选中
func (m *Model) IsSelected(id uint64) bool {
	return m.selected[id]
}

// SelectedFiles 获取所有选中的文件
func (m *Model) SelectedFiles() []models.File {
	var result []models.File
	for _, f := range m.files {
		if m.selected[f.ID] {
			result = append(result, f)
		}
	}
	return result
}

// ClearSelection 清除选择
func (m *Model) ClearSelection() {
	m.selected = make(map[uint64]bool)
}

// SelectAll 全选
func (m *Model) SelectAll() {
	for _, f := range m.files {
		m.selected[f.ID] = true
	}
}

// HasSelection 是否有选中项
func (m *Model) HasSelection() bool {
	return len(m.selected) > 0
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
		// 空目录提示
		return m.renderEmpty()
	}
	return m.list.View()
}

// renderEmpty 渲染空目录
func (m *Model) renderEmpty() string {
	emptyStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("#6c6c6c")).
		Align(lipgloss.Center)

	lines := []string{
		"",
		styles.IconEmpty + " 此文件夹为空",
		"",
		"按 [u] 上传文件",
		"按 [n] 创建文件夹",
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
	if len(m.files) > 0 {
		m.list.Select(0)
	}
}

// CursorEnd 跳到底部
func (m *Model) CursorEnd() {
	if len(m.files) > 0 {
		m.list.Select(len(m.files) - 1)
	}
}

// PageUp 上翻页
func (m *Model) PageUp() {
	m.list.Paginator.PrevPage()
}

// PageDown 下翻页
func (m *Model) PageDown() {
	m.list.Paginator.NextPage()
}

// Select 选择指定索引
func (m *Model) Select(index int) {
	if index >= 0 && index < len(m.files) {
		m.list.Select(index)
	}
}
