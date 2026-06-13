package tui

import (
	"context"
	"strings"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

type loginMode int

const (
	loginModeLogin loginMode = iota
	loginModeRegister
)

type loginState struct {
	mode     loginMode
	focus    int // 0..n-1
	account  textinput.Model
	username textinput.Model
	email    textinput.Model
	password textinput.Model
	baseURL  textinput.Model
	loading  bool
}

// initLogin initializes login inputs.
func (m *model) initLogin() {
	if m.login.account.Value() != "" {
		return
	}
	m.login.account = newInput("account (username or email)", "")
	m.login.username = newInput("username", "")
	m.login.email = newInput("email", "")
	m.login.password = newInput("password", "")
	m.login.password.EchoMode = textinput.EchoPassword
	m.login.baseURL = newInput("server base url", m.client.BaseURL)
	m.login.mode = loginModeLogin
	m.login.focus = 0
	m.login.account.Focus()
}

func newInput(placeholder, value string) textinput.Model {
	ti := textinput.New()
	ti.Placeholder = placeholder
	ti.SetValue(value)
	ti.CharLimit = 256
	ti.Width = 40
	return ti
}

func (m model) loginFocus() tea.Cmd {
	if m.login.mode == loginModeLogin {
		return m.login.account.Focus()
	}
	return m.login.username.Focus()
}

// loginResultMsg carries the result of an async auth call.
type loginResultMsg struct {
	err error
}

func (m model) loginSubmit() tea.Cmd {
	c := m.client
	store := m.store
	if m.login.mode == loginModeLogin {
		account := m.login.account.Value()
		password := m.login.password.Value()
		return func() tea.Msg {
			_, err := c.Login(context.Background(), account, password)
			if err == nil && store != nil {
				store.SetTokens(c.AccessToken(), c.RefreshToken())
				_ = store.Save()
			}
			return loginResultMsg{err: err}
		}
	}
	username := m.login.username.Value()
	email := m.login.email.Value()
	password := m.login.password.Value()
	return func() tea.Msg {
		_, err := c.Register(context.Background(), username, email, password)
		if err == nil {
			// try to log in immediately
			_, lerr := c.Login(context.Background(), username, password)
			if lerr == nil && store != nil {
				store.SetTokens(c.AccessToken(), c.RefreshToken())
				_ = store.Save()
			}
			if lerr != nil {
				return loginResultMsg{err: lerr}
			}
		}
		return loginResultMsg{err: err}
	}
}

func (m model) loginUpdate(msg tea.Msg) (model, tea.Cmd) {
	st := &m.login
	switch msg := msg.(type) {
	case loginResultMsg:
		st.loading = false
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, m.loginFocus()
		}
		m.SetStatus("logged in")
		m.active = screenMenu
		return m, nil
	case tea.KeyMsg:
		if st.loading {
			return m, nil
		}
		switch msg.String() {
		case "tab", "shift+tab":
			// cycle focus
			n := m.loginFieldCount()
			st.focus = (st.focus + 1) % n
			return m, m.loginApplyFocus()
		case "up":
			n := m.loginFieldCount()
			st.focus = (st.focus - 1 + n) % n
			return m, m.loginApplyFocus()
		case "down":
			n := m.loginFieldCount()
			st.focus = (st.focus + 1) % n
			return m, m.loginApplyFocus()
		case "ctrl+l":
			st.mode = loginModeLogin
			st.focus = 0
			return m, m.loginFocus()
		case "ctrl+r":
			st.mode = loginModeRegister
			st.focus = 0
			return m, m.loginFocus()
		case "enter":
			if err := m.loginValidate(); err != "" {
				m.SetError(err)
				return m, nil
			}
			if m.login.baseURL.Value() != "" && m.login.baseURL.Value() != m.client.BaseURL {
				// rebuild client base url
				m.client.BaseURL = m.login.baseURL.Value()
				if m.store != nil {
					m.store.SetBaseURL(m.client.BaseURL)
					_ = m.store.Save()
				}
			}
			st.loading = true
			m.SetStatus("authenticating…")
			return m, m.loginSubmit()
		}
	}
	// pass to current input
	cur := m.loginCurrentInput()
	if cur != nil {
		var cmd tea.Cmd
		*cur, cmd = cur.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) loginCurrentInput() *textinput.Model {
	st := &m.login
	if st.mode == loginModeLogin {
		switch st.focus {
		case 0:
			return &st.account
		case 1:
			return &st.password
		case 2:
			return &st.baseURL
		}
	} else {
		switch st.focus {
		case 0:
			return &st.username
		case 1:
			return &st.email
		case 2:
			return &st.password
		case 3:
			return &st.baseURL
		}
	}
	return nil
}

func (m model) loginFieldCount() int {
	if m.login.mode == loginModeLogin {
		return 3
	}
	return 4
}

func (m model) loginApplyFocus() tea.Cmd {
	// reset all, then focus current
	st := &m.login
	st.account.Blur()
	st.username.Blur()
	st.email.Blur()
	st.password.Blur()
	st.baseURL.Blur()
	if cur := m.loginCurrentInput(); cur != nil {
		return cur.Focus()
	}
	return nil
}

func (m model) loginValidate() string {
	st := &m.login
	if st.mode == loginModeLogin {
		if strings.TrimSpace(st.account.Value()) == "" {
			return "account is required"
		}
		if st.password.Value() == "" {
			return "password is required"
		}
		return ""
	}
	if strings.TrimSpace(st.username.Value()) == "" {
		return "username is required"
	}
	if strings.TrimSpace(st.email.Value()) == "" {
		return "email is required"
	}
	if st.password.Value() == "" {
		return "password is required"
	}
	return ""
}

func (m model) loginPassResize(msg tea.WindowSizeMsg) {
	w := msg.Width - 4
	if w < 20 {
		w = 20
	}
	m.login.account.Width = w
	m.login.username.Width = w
	m.login.email.Width = w
	m.login.password.Width = w
	m.login.baseURL.Width = w
}

func (m model) loginView() string {
	t := m.theme
	st := m.login
	var rows []string
	rows = append(rows, t.Banner.Render("◼ Welcome to disk-tui"))
	rows = append(rows, "")
	rows = append(rows, t.Muted.Render("Mode:  Ctrl+L login  |  Ctrl+R register"))
	rows = append(rows, "")

	renderField := func(label string, active bool, input textinput.Model) string {
		l := t.Label.Render(label)
		if active {
			l = lipgloss.NewStyle().Foreground(lipgloss.Color("#FBBF24")).Bold(true).Width(14).Render(label)
		}
		return l + input.View()
	}

	if st.mode == loginModeLogin {
		rows = append(rows, renderField("Account", st.focus == 0, st.account))
		rows = append(rows, renderField("Password", st.focus == 1, st.password))
		rows = append(rows, renderField("Server URL", st.focus == 2, st.baseURL))
	} else {
		rows = append(rows, renderField("Username", st.focus == 0, st.username))
		rows = append(rows, renderField("Email", st.focus == 1, st.email))
		rows = append(rows, renderField("Password", st.focus == 2, st.password))
		rows = append(rows, renderField("Server URL", st.focus == 3, st.baseURL))
	}
	rows = append(rows, "")
	rows = append(rows, t.Muted.Render("Enter: submit  |  Tab/↑↓: next field  |  Ctrl+C: quit"))

	if st.loading {
		rows = append(rows, t.Accent.Render("⏳ Authenticating…"))
	}

	content := lipgloss.JoinVertical(lipgloss.Left, rows...)
	dialog := t.Dialog.MaxWidth(80).Render(content)
	return lipgloss.Place(m.width, m.height-lipgloss.Height(m.renderHeader())-lipgloss.Height(m.renderFooter()),
		lipgloss.Center, lipgloss.Center, dialog)
}
