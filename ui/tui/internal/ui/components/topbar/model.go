package topbar

import (
	"fmt"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/yizhinailong/disk/ui/tui/internal/ui/styles"
)

const version = "v1.0"

// Model 顶部状态栏模型
type Model struct {
	username   string
	usedSpace  uint64
	totalSpace uint64
	width      int
}

// New 创建顶部状态栏
func New() Model {
	return Model{}
}

// SetUserInfo 设置用户信息
func (m *Model) SetUserInfo(username string) {
	m.username = username
}

// SetStorageInfo 设置存储空间信息
func (m *Model) SetStorageInfo(used, total uint64) {
	m.usedSpace = used
	m.totalSpace = total
}

// SetWidth 设置宽度
func (m *Model) SetWidth(width int) {
	m.width = width
}

// Init 初始化
func (m Model) Init() tea.Cmd {
	return nil
}

// Update 更新
func (m Model) Update(msg tea.Msg) (Model, tea.Cmd) {
	return m, nil
}

// View 渲染
func (m Model) View() string {
	if m.width <= 0 {
		return ""
	}

	var parts []string

	// 应用名称
	appName := fmt.Sprintf("Disk TUI %s", version)
	parts = append(parts, appName)

	// 用户信息
	if m.username != "" {
		parts = append(parts, fmt.Sprintf("用户: %s", m.username))
	}

	// 存储空间
	if m.totalSpace > 0 {
		storageStr := styles.FormatStorage(m.usedSpace, m.totalSpace)
		parts = append(parts, storageStr)
	}

	// 日期时间
	now := time.Now()
	dateTime := now.Format("2006-01-02 15:04")
	parts = append(parts, dateTime)

	// 使用分隔符连接
	content := ""
	for i, part := range parts {
		if i > 0 {
			content += " │ "
		}
		content += part
	}

	// 应用顶部状态栏样式 - 主色背景，白色文字
	style := lipgloss.NewStyle().
		Background(styles.ColorPrimary).
		Foreground(styles.ColorSelectedFg).
		Padding(0, 1).
		Width(m.width)

	return style.Render(content)
}

// Height 返回高度
func (m Model) Height() int {
	return 1
}
