// Package statusbar 状态栏组件
package statusbar

import (
	"fmt"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/liufeng/disk/ui/tui/internal/ui/styles"
)

// KeyHint 快捷键提示
type KeyHint struct {
	Key  string
	Desc string
}

// Model 状态栏模型
type Model struct {
	keyHints []KeyHint
	message  string
	msgType  string // info, success, error, warning
	width    int
}

// New 创建状态栏
func New() Model {
	return Model{
		keyHints: []KeyHint{
			{"u", "上传"},
			{"d", "下载"},
			{"r", "重命名"},
			{"m", "移动"},
			{"c", "复制"},
			{"x", "删除"},
			{"n", "新建"},
			{"?", "帮助"},
		},
	}
}

// SetWidth 设置宽度
func (m *Model) SetWidth(width int) {
	m.width = width
}

// Width 获取宽度
func (m *Model) Width() int {
	return m.width
}

// SetMessage 设置消息
func (m *Model) SetMessage(msg, msgType string) {
	m.message = msg
	m.msgType = msgType
}

// SetInfo 设置信息消息
func (m *Model) SetInfo(msg string) {
	m.SetMessage(msg, "info")
}

// SetSuccess 设置成功消息
func (m *Model) SetSuccess(msg string) {
	m.SetMessage(msg, "success")
}

// SetError 设置错误消息
func (m *Model) SetError(msg string) {
	m.SetMessage(msg, "error")
}

// SetWarning 设置警告消息
func (m *Model) SetWarning(msg string) {
	m.SetMessage(msg, "warning")
}

// ClearMessage 清除消息
func (m *Model) ClearMessage() {
	m.message = ""
	m.msgType = ""
}

// Message 获取当前消息
func (m *Model) Message() (string, string) {
	return m.message, m.msgType
}

// SetKeyHints 设置快捷键提示
func (m *Model) SetKeyHints(hints []KeyHint) {
	m.keyHints = hints
}

// KeyHints 获取快捷键提示
func (m *Model) KeyHints() []KeyHint {
	return m.keyHints
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
	var parts []string

	// 消息优先显示
	if m.message != "" {
		var msgStyle lipgloss.Style
		switch m.msgType {
		case "success":
			msgStyle = styles.SuccessStyle
		case "error":
			msgStyle = styles.ErrorStyle
		case "warning":
			msgStyle = styles.WarningStyle
		default:
			msgStyle = styles.InfoStyle
		}
		parts = append(parts, msgStyle.Render(m.message))
	}

	// 快捷键提示
	for _, hint := range m.keyHints {
		keyPart := fmt.Sprintf("[%s]%s", styles.KeyStyle.Render(hint.Key), hint.Desc)
		parts = append(parts, keyPart)
	}

	line := strings.Join(parts, " ")

	// 应用状态栏样式
	return styles.StatusBarStyle.Width(m.width).Render(line)
}

// RenderCompact 渲染紧凑版本（用于窄屏）
func (m *Model) RenderCompact() string {
	// 只显示最重要的快捷键
	compactHints := []KeyHint{
		{"u", "上传"},
		{"d", "下载"},
		{"x", "删除"},
		{"?", "帮助"},
	}

	var parts []string

	if m.message != "" {
		var msgStyle lipgloss.Style
		switch m.msgType {
		case "success":
			msgStyle = styles.SuccessStyle
		case "error":
			msgStyle = styles.ErrorStyle
		case "warning":
			msgStyle = styles.WarningStyle
		default:
			msgStyle = styles.InfoStyle
		}
		parts = append(parts, msgStyle.Render(m.message))
	}

	for _, hint := range compactHints {
		keyPart := fmt.Sprintf("[%s]%s", styles.KeyStyle.Render(hint.Key), hint.Desc)
		parts = append(parts, keyPart)
	}

	line := strings.Join(parts, " ")
	return styles.StatusBarStyle.Width(m.width).Render(line)
}

// RenderForTrash 渲染回收站专用状态栏
func (m *Model) RenderForTrash() string {
	hints := []KeyHint{
		{"r", "恢复"},
		{"x", "彻底删除"},
		{"E", "清空"},
		{"?", "帮助"},
	}

	var parts []string

	if m.message != "" {
		var msgStyle lipgloss.Style
		switch m.msgType {
		case "success":
			msgStyle = styles.SuccessStyle
		case "error":
			msgStyle = styles.ErrorStyle
		case "warning":
			msgStyle = styles.WarningStyle
		default:
			msgStyle = styles.InfoStyle
		}
		parts = append(parts, msgStyle.Render(m.message))
	}

	for _, hint := range hints {
		keyPart := fmt.Sprintf("[%s]%s", styles.KeyStyle.Render(hint.Key), hint.Desc)
		parts = append(parts, keyPart)
	}

	line := strings.Join(parts, " ")
	return styles.StatusBarStyle.Width(m.width).Render(line)
}

// RenderForShare 渲染分享管理专用状态栏
func (m *Model) RenderForShare() string {
	hints := []KeyHint{
		{"x", "取消分享"},
		{"c", "复制链接"},
		{"e", "编辑"},
		{"?", "帮助"},
	}

	var parts []string

	if m.message != "" {
		var msgStyle lipgloss.Style
		switch m.msgType {
		case "success":
			msgStyle = styles.SuccessStyle
		case "error":
			msgStyle = styles.ErrorStyle
		case "warning":
			msgStyle = styles.WarningStyle
		default:
			msgStyle = styles.InfoStyle
		}
		parts = append(parts, msgStyle.Render(m.message))
	}

	for _, hint := range hints {
		keyPart := fmt.Sprintf("[%s]%s", styles.KeyStyle.Render(hint.Key), hint.Desc)
		parts = append(parts, keyPart)
	}

	line := strings.Join(parts, " ")
	return styles.StatusBarStyle.Width(m.width).Render(line)
}

// RenderForHelp 渲染帮助页面状态栏
func (m *Model) RenderForHelp() string {
	hints := []KeyHint{
		{"q/Esc", "返回"},
		{"/", "搜索"},
		{"j/k", "滚动"},
	}

	var parts []string

	for _, hint := range hints {
		keyPart := fmt.Sprintf("[%s]%s", styles.KeyStyle.Render(hint.Key), hint.Desc)
		parts = append(parts, keyPart)
	}

	line := strings.Join(parts, " ")
	return styles.StatusBarStyle.Width(m.width).Render(line)
}
