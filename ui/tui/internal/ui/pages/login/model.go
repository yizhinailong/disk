// Package login implements the login page for the TUI application.
// It provides user authentication with username/password input,
// server address display, and error feedback.
package login

import (
	"context"
	"fmt"
	"strings"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/liufeng/disk/ui/tui/internal/api"
	"github.com/liufeng/disk/ui/tui/internal/config"
	"github.com/liufeng/disk/ui/tui/internal/ui/styles"
)

// =============================================================================
// 登录状态 (Login State)
// =============================================================================

// loginState 登录页面状态
type loginState int

const (
	stateInput   loginState = iota // 输入状态
	stateLogging                   // 登录中
	stateError                     // 错误状态
	stateSuccess                   // 成功状态
)

// =============================================================================
// 消息定义 (Message Definitions)
// =============================================================================

// LoginSuccessMsg 登录成功消息
type LoginSuccessMsg struct {
	Username string
}

// LoginErrorMsg 登录失败消息
type LoginErrorMsg struct {
	Error string
}

// =============================================================================
// Model 定义 (Model Definition)
// =============================================================================

// Model 登录页面模型
type Model struct {
	client *api.Client
	config *config.Config

	// 输入框
	usernameInput textinput.Model
	passwordInput textinput.Model
	focusIndex    int // 0: 用户名, 1: 密码, 2: 登录按钮

	// 状态
	state    loginState
	errorMsg string
	width    int
	height   int
}

// New 创建登录页面
func New(cfg *config.Config, client *api.Client) Model {
	// 用户名输入框
	ui := textinput.New()
	ui.Placeholder = "用户名或邮箱"
	ui.Focus()
	ui.Width = 30

	// 密码输入框
	pi := textinput.New()
	pi.Placeholder = "密码"
	pi.EchoMode = textinput.EchoPassword
	pi.EchoCharacter = '•'
	pi.Width = 30

	return Model{
		client:        client,
		config:        cfg,
		usernameInput: ui,
		passwordInput: pi,
		state:         stateInput,
	}
}

// Init 初始化
func (m Model) Init() tea.Cmd {
	return textinput.Blink
}

// Update 更新
func (m Model) Update(msg tea.Msg) (Model, tea.Cmd) {
	var cmds []tea.Cmd

	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.Type {
		case tea.KeyTab, tea.KeyShiftTab:
			// 切换焦点
			if msg.Type == tea.KeyTab {
				m.focusIndex = (m.focusIndex + 1) % 3
			} else {
				m.focusIndex = (m.focusIndex - 1 + 3) % 3
			}
			m.updateFocus()
			return m, nil

		case tea.KeyEnter:
			if m.state == stateInput {
				if m.focusIndex == 2 {
					// 点击登录按钮
					return m, m.doLogin()
				}
				// 切换到下一个输入框
				m.focusIndex = (m.focusIndex + 1) % 3
				m.updateFocus()
			}
			return m, nil

		case tea.KeyCtrlC, tea.KeyEsc:
			return m, tea.Quit
		}

	case LoginSuccessMsg:
		m.state = stateSuccess
		return m, func() tea.Msg { return msg }

	case LoginErrorMsg:
		m.state = stateError
		m.errorMsg = msg.Error
		return m, nil
	}

	// 更新输入框
	var cmd tea.Cmd
	m.usernameInput, cmd = m.usernameInput.Update(msg)
	cmds = append(cmds, cmd)

	m.passwordInput, cmd = m.passwordInput.Update(msg)
	cmds = append(cmds, cmd)

	return m, tea.Batch(cmds...)
}

// updateFocus 更新焦点
func (m *Model) updateFocus() {
	m.usernameInput.Blur()
	m.passwordInput.Blur()

	switch m.focusIndex {
	case 0:
		m.usernameInput.Focus()
	case 1:
		m.passwordInput.Focus()
	}
}

// doLogin 执行登录
func (m Model) doLogin() tea.Cmd {
	return func() tea.Msg {
		ctx := context.Background()
		_, err := m.client.Auth.Login(ctx, m.usernameInput.Value(), m.passwordInput.Value())

		if err != nil {
			return LoginErrorMsg{Error: err.Error()}
		}

		return LoginSuccessMsg{Username: m.usernameInput.Value()}
	}
}

// View 渲染
func (m Model) View() string {
	var b strings.Builder

	// 标题
	title := styles.TitleStyle.Render("Disk TUI 客户端")
	b.WriteString(lipgloss.NewStyle().Margin(2, 0, 1, 0).Render(title))
	b.WriteString("\n")

	// 服务器地址
	server := styles.MutedStyle.Render(fmt.Sprintf("服务器: %s", m.config.Server.URL))
	b.WriteString(server)
	b.WriteString("\n\n")

	// 用户名输入框
	b.WriteString(m.renderInput("用户名", &m.usernameInput, m.focusIndex == 0))
	b.WriteString("\n")

	// 密码输入框
	b.WriteString(m.renderInput("密码", &m.passwordInput, m.focusIndex == 1))
	b.WriteString("\n\n")

	// 登录按钮
	b.WriteString(m.renderButton("登录", m.focusIndex == 2))
	b.WriteString("\n\n")

	// 状态信息
	switch m.state {
	case stateLogging:
		b.WriteString(styles.InfoStyle.Render("正在登录..."))
	case stateError:
		b.WriteString(styles.ErrorStyle.Render("✗ " + m.errorMsg))
	}

	b.WriteString("\n\n")

	// 帮助提示
	help := styles.MutedStyle.Render("[Tab] 切换  [Enter] 确认  [Esc] 退出")
	b.WriteString(help)

	// 居中渲染
	return lipgloss.NewStyle().
		Width(m.width).
		Height(m.height).
		Align(lipgloss.Center, lipgloss.Center).
		Render(b.String())
}

// renderInput 渲染输入框
func (m Model) renderInput(label string, input *textinput.Model, focused bool) string {
	labelStyle := styles.TextStyle
	if focused {
		labelStyle = styles.TitleStyle
	}

	var inputStyle lipgloss.Style
	if focused {
		inputStyle = styles.InputFocusStyle
	} else {
		inputStyle = styles.InputStyle
	}

	return fmt.Sprintf("%s\n%s",
		labelStyle.Render(label),
		inputStyle.Render(input.View()),
	)
}

// renderButton 渲染按钮
func (m Model) renderButton(text string, focused bool) string {
	style := lipgloss.NewStyle().
		Padding(0, 4).
		Margin(0, 2)

	if focused {
		style = style.
			Background(styles.ColorPrimary).
			Foreground(lipgloss.Color("#FFFFFF")).
			Bold(true)
	} else {
		style = style.
			Background(styles.ColorBgSecondary).
			Foreground(styles.ColorText)
	}

	return style.Render(text)
}

// SetSize 设置大小
func (m *Model) SetSize(width, height int) {
	m.width = width
	m.height = height
}
