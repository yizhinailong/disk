// Package input 输入框组件
package input

import (
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/liufeng/disk/ui/tui/internal/ui/styles"
)

// InputState 输入框状态
type InputState int

const (
	StateNormal InputState = iota
	StateFocused
	StateError
	StateDisabled
)

// Model 输入框模型
type Model struct {
	placeholder string
	value       string
	cursor      int
	focused     bool
	width       int
	echoMode    bool   // 是否显示输入内容（密码模式）
	errMsg      string // 错误消息
	label       string // 标签
	state       InputState
	maxLength   int // 最大长度限制
}

// New 创建输入框
func New(placeholder string) Model {
	return Model{
		placeholder: placeholder,
		echoMode:    true,
		state:       StateNormal,
		maxLength:   0, // 0 表示无限制
	}
}

// NewWithLabel 创建带标签的输入框
func NewWithLabel(label, placeholder string) Model {
	return Model{
		placeholder: placeholder,
		echoMode:    true,
		label:       label,
		state:       StateNormal,
		maxLength:   0,
	}
}

// SetPassword 设置为密码模式
func (m *Model) SetPassword() {
	m.echoMode = false
}

// SetEchoMode 设置回显模式
func (m *Model) SetEchoMode(echo bool) {
	m.echoMode = echo
}

// SetValue 设置值
func (m *Model) SetValue(value string) {
	m.value = value
	m.cursor = len(value)
	m.errMsg = ""
	m.state = StateNormal
}

// Value 获取值
func (m *Model) Value() string {
	return m.value
}

// SetPlaceholder 设置占位符
func (m *Model) SetPlaceholder(placeholder string) {
	m.placeholder = placeholder
}

// SetLabel 设置标签
func (m *Model) SetLabel(label string) {
	m.label = label
}

// Label 获取标签
func (m *Model) Label() string {
	return m.label
}

// SetError 设置错误
func (m *Model) SetError(err string) {
	m.errMsg = err
	m.state = StateError
}

// ClearError 清除错误
func (m *Model) ClearError() {
	m.errMsg = ""
	if m.focused {
		m.state = StateFocused
	} else {
		m.state = StateNormal
	}
}

// Error 获取错误消息
func (m *Model) Error() string {
	return m.errMsg
}

// HasError 是否有错误
func (m *Model) HasError() bool {
	return m.errMsg != ""
}

// Focus 获取焦点
func (m *Model) Focus() tea.Cmd {
	m.focused = true
	if m.state != StateError {
		m.state = StateFocused
	}
	return nil
}

// Blur 失去焦点
func (m *Model) Blur() {
	m.focused = false
	if m.state != StateError {
		m.state = StateNormal
	}
}

// Focused 是否有焦点
func (m *Model) Focused() bool {
	return m.focused
}

// SetWidth 设置宽度
func (m *Model) SetWidth(width int) {
	m.width = width
}

// Width 获取宽度
func (m *Model) Width() int {
	return m.width
}

// SetMaxLength 设置最大长度
func (m *Model) SetMaxLength(max int) {
	m.maxLength = max
}

// MaxLength 获取最大长度
func (m *Model) MaxLength() int {
	return m.maxLength
}

// IsEmpty 是否为空
func (m *Model) IsEmpty() bool {
	return m.value == ""
}

// Clear 清空内容
func (m *Model) Clear() {
	m.value = ""
	m.cursor = 0
	m.errMsg = ""
	m.state = StateFocused
}

// SetDisabled 设置禁用状态
func (m *Model) SetDisabled(disabled bool) {
	if disabled {
		m.state = StateDisabled
		m.focused = false
	} else {
		m.state = StateNormal
	}
}

// IsDisabled 是否禁用
func (m *Model) IsDisabled() bool {
	return m.state == StateDisabled
}

// Init 初始化
func (m Model) Init() tea.Cmd {
	return nil
}

// Update 更新
func (m Model) Update(msg tea.Msg) (Model, tea.Cmd) {
	if m.state == StateDisabled {
		return m, nil
	}

	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.Type {
		case tea.KeyBackspace:
			if m.cursor > 0 {
				m.value = m.value[:m.cursor-1] + m.value[m.cursor:]
				m.cursor--
				m.errMsg = ""
				if m.focused {
					m.state = StateFocused
				}
			}
		case tea.KeyDelete:
			if m.cursor < len(m.value) {
				m.value = m.value[:m.cursor] + m.value[m.cursor+1:]
				m.errMsg = ""
				if m.focused {
					m.state = StateFocused
				}
			}
		case tea.KeyLeft:
			if m.cursor > 0 {
				m.cursor--
			}
		case tea.KeyRight:
			if m.cursor < len(m.value) {
				m.cursor++
			}
		case tea.KeyHome:
			m.cursor = 0
		case tea.KeyEnd:
			m.cursor = len(m.value)
		case tea.KeyRunes:
			// 检查最大长度
			if m.maxLength > 0 && len(m.value) >= m.maxLength {
				return m, nil
			}
			runes := string(msg.Runes)
			m.value = m.value[:m.cursor] + runes + m.value[m.cursor:]
			m.cursor += len(runes)
			m.errMsg = ""
			if m.focused {
				m.state = StateFocused
			}
		}
	}

	return m, nil
}

// View 渲染
func (m Model) View() string {
	// 选择样式
	var style lipgloss.Style
	switch m.state {
	case StateFocused:
		style = styles.InputFocusStyle
	case StateError:
		style = styles.InputErrorStyle
	case StateDisabled:
		style = styles.InputStyle.Foreground(lipgloss.Color("#6c6c6c"))
	default:
		style = styles.InputStyle
	}

	// 构建显示内容
	var display string
	if m.value == "" {
		display = styles.PlaceholderStyle.Render(m.placeholder)
	} else if m.echoMode {
		display = m.value
	} else {
		// 密码模式：显示星号
		display = strings.Repeat("•", len(m.value))
	}

	// 添加光标
	if m.focused && m.state != StateDisabled {
		cursor := "│"
		if m.cursor >= len(display) {
			display += styles.CursorStyle.Render(cursor)
		} else {
			// 在光标位置插入
			before := display[:m.cursor]
			after := display[m.cursor:]
			char := string([]rune(after)[0])
			after = after[len(char):]
			display = before + styles.CursorStyle.Render(char) + after
		}
	}

	// 设置宽度
	if m.width > 0 {
		style = style.Width(m.width)
	}

	result := style.Render(display)

	// 添加标签
	if m.label != "" {
		labelStyle := styles.LabelStyle
		result = labelStyle.Render(m.label) + "\n" + result
	}

	// 添加错误消息
	if m.errMsg != "" {
		result += "\n" + styles.ErrorStyle.Render("  "+m.errMsg)
	}

	return result
}

// RenderSimple 渲染简单版本（无标签无错误）
func (m Model) RenderSimple() string {
	// 选择样式
	var style lipgloss.Style
	switch m.state {
	case StateFocused:
		style = styles.InputFocusStyle
	case StateError:
		style = styles.InputErrorStyle
	case StateDisabled:
		style = styles.InputStyle.Foreground(lipgloss.Color("#6c6c6c"))
	default:
		style = styles.InputStyle
	}

	// 构建显示内容
	var display string
	if m.value == "" {
		display = styles.PlaceholderStyle.Render(m.placeholder)
	} else if m.echoMode {
		display = m.value
	} else {
		display = strings.Repeat("•", len(m.value))
	}

	// 添加光标
	if m.focused && m.state != StateDisabled {
		display += styles.CursorStyle.Render("│")
	}

	return style.Render(display)
}

// CursorPos 获取光标位置
func (m *Model) CursorPos() int {
	return m.cursor
}

// SetCursor 设置光标位置
func (m *Model) SetCursor(pos int) {
	if pos < 0 {
		pos = 0
	}
	if pos > len(m.value) {
		pos = len(m.value)
	}
	m.cursor = pos
}

// Len 获取内容长度
func (m *Model) Len() int {
	return len(m.value)
}

// CursorStart 移动光标到开始
func (m *Model) CursorStart() {
	m.cursor = 0
}

// CursorEnd 移动光标到结尾
func (m *Model) CursorEnd() {
	m.cursor = len(m.value)
}

// WordLeft 光标左移一个单词
func (m *Model) WordLeft() {
	if m.cursor == 0 {
		return
	}
	// 跳过空格
	for m.cursor > 0 && m.value[m.cursor-1] == ' ' {
		m.cursor--
	}
	// 跳过单词
	for m.cursor > 0 && m.value[m.cursor-1] != ' ' {
		m.cursor--
	}
}

// WordRight 光标右移一个单词
func (m *Model) WordRight() {
	if m.cursor >= len(m.value) {
		return
	}
	// 跳过单词
	for m.cursor < len(m.value) && m.value[m.cursor] != ' ' {
		m.cursor++
	}
	// 跳过空格
	for m.cursor < len(m.value) && m.value[m.cursor] == ' ' {
		m.cursor++
	}
}
