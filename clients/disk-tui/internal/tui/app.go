// Package tui implements the disk-tui terminal user interface using
// bubbletea + lipgloss. It exposes every backend endpoint through a
// navigable screen-based interface.
package tui

import (
	"context"
	"fmt"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/disk-tui/internal/client"
	"github.com/disk-tui/internal/config"
)

// screen identifies the active top-level view.
type screen int

const (
	screenLogin screen = iota
	screenMenu
	screenFiles
	screenShares
	screenTrash
	screenProfile
	screenLogs
	screenSystem
	screenAdmin
	screenShareVisitor
)

func (s screen) Title() string {
	switch s {
	case screenLogin:
		return "Login"
	case screenMenu:
		return "Main Menu"
	case screenFiles:
		return "Files"
	case screenShares:
		return "Shares"
	case screenTrash:
		return "Trash"
	case screenProfile:
		return "Profile"
	case screenLogs:
		return "Operation Logs"
	case screenSystem:
		return "System"
	case screenAdmin:
		return "Admin"
	case screenShareVisitor:
		return "Share Visitor"
	}
	return "Unknown"
}

// model is the root bubbletea model.
type model struct {
	client *client.Client
	store  *config.Store
	theme  Theme
	width  int
	height int

	active    screen
	returnTo  screen // screen to return to after dialog/prompt
	status    string
	statusErr bool

	// async runner
	ctx context.Context

	// === login screen state ===
	login loginState

	// === main menu state ===
	menuIndex int

	// === files browser state ===
	files filesState

	// === shares state ===
	shares sharesState

	// === trash state ===
	trash trashState

	// === profile state ===
	profile profileState

	// === logs state ===
	logs logsState

	// === system state ===
	system systemState

	// === admin state ===
	admin adminState

	// === share visitor state ===
	visitor visitorState
}

// New constructs the root model.
func New(c *client.Client, store *config.Store) tea.Model {
	m := model{
		client: c,
		store:  store,
		theme:  DefaultTheme(),
		ctx:    context.Background(),
	}
	if c.IsAuthenticated() {
		m.active = screenMenu
	} else {
		m.active = screenLogin
	}
	m.initLogin()
	m.initFiles()
	m.initShares()
	m.initTrash()
	m.initProfile()
	m.initLogs()
	m.initSystem()
	m.initAdmin()
	m.initVisitor()
	return m
}

// Init starts the program; kicks off initial fetches if logged in.
func (m model) Init() tea.Cmd {
	if m.active == screenLogin {
		return m.loginFocus()
	}
	return nil
}

// SetStatus stores a status message rendered in the footer.
func (m *model) SetStatus(msg string) {
	m.status = msg
	m.statusErr = false
}

// SetError stores an error message rendered in the footer.
func (m *model) SetError(msg string) {
	m.status = msg
	m.statusErr = true
}

// Update is the top-level dispatcher.
func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	var cmds []tea.Cmd
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width, m.height = msg.Width, msg.Height
		m.passWindowSize(msg)
		return m, nil
	case tea.KeyMsg:
		switch msg.String() {
		case "ctrl+c":
			return m, tea.Quit
		}
	}

	// dispatch to active screen
	var cmd tea.Cmd
	switch m.active {
	case screenLogin:
		m, cmd = m.loginUpdate(msg)
	case screenMenu:
		m, cmd = m.menuUpdate(msg)
	case screenFiles:
		m, cmd = m.filesUpdate(msg)
	case screenShares:
		m, cmd = m.sharesUpdate(msg)
	case screenTrash:
		m, cmd = m.trashUpdate(msg)
	case screenProfile:
		m, cmd = m.profileUpdate(msg)
	case screenLogs:
		m, cmd = m.logsUpdate(msg)
	case screenSystem:
		m, cmd = m.systemUpdate(msg)
	case screenAdmin:
		m, cmd = m.adminUpdate(msg)
	case screenShareVisitor:
		m, cmd = m.visitorUpdate(msg)
	}
	cmds = append(cmds, cmd)
	return m, tea.Batch(cmds...)
}

// View is the top-level renderer.
func (m model) View() string {
	if m.width == 0 {
		return "Loading…"
	}
	var body string
	switch m.active {
	case screenLogin:
		body = m.loginView()
	case screenMenu:
		body = m.menuView()
	case screenFiles:
		body = m.filesView()
	case screenShares:
		body = m.sharesView()
	case screenTrash:
		body = m.trashView()
	case screenProfile:
		body = m.profileView()
	case screenLogs:
		body = m.logsView()
	case screenSystem:
		body = m.systemView()
	case screenAdmin:
		body = m.adminView()
	case screenShareVisitor:
		body = m.visitorView()
	}
	header := m.renderHeader()
	footer := m.renderFooter()
	available := m.height - lipgloss.Height(header) - lipgloss.Height(footer)
	if available < 1 {
		available = 1
	}
	body = trimLinesToHeight(body, available)
	return lipgloss.JoinVertical(lipgloss.Left, header, body, footer)
}

// renderHeader builds the title bar.
func (m model) renderHeader() string {
	t := m.theme
	left := t.Title.Render("◼ disk-tui") + "  " +
		t.Subtitle.Render(m.active.Title())
	right := m.theme.Muted.Render(m.client.BaseURL)
	authState := t.Success.Render("● auth")
	if !m.client.IsAuthenticated() {
		authState = t.Muted.Render("○ guest")
	}
	right = lipgloss.JoinHorizontal(lipgloss.Left, authState, "  ", right)
	gap := max(1, m.width-lipgloss.Width(left)-lipgloss.Width(right))
	fill := strings.Repeat(" ", gap)
	bar := lipgloss.JoinHorizontal(lipgloss.Left, left, fill, right)
	return t.Header.MaxWidth(m.width).Render(bar)
}

// renderFooter builds the status bar.
func (m model) renderFooter() string {
	t := m.theme
	status := m.status
	if status == "" {
		status = t.Muted.Render("ready")
	} else if m.statusErr {
		status = t.Error.Render("⚠ " + status)
	} else {
		status = t.Success.Render("✓ " + status)
	}
	return t.Footer.MaxWidth(m.width).Render(status)
}

func (m *model) passWindowSize(msg tea.WindowSizeMsg) {
	m.loginPassResize(msg)
	m.filesPassResize(msg)
	m.sharesPassResize(msg)
	m.trashPassResize(msg)
	m.profilePassResize(msg)
	m.logsPassResize(msg)
	m.systemPassResize(msg)
	m.adminPassResize(msg)
	m.visitorPassResize(msg)
}

func trimLinesToHeight(s string, h int) string {
	if h <= 0 {
		return ""
	}
	lines := strings.Split(s, "\n")
	if len(lines) <= h {
		return s
	}
	return strings.Join(lines[len(lines)-h:], "\n")
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

// safeFmt formats v and returns the result, falling back to "<nil>" for nil.
func safeFmt(v any) string {
	if v == nil {
		return ""
	}
	return fmt.Sprintf("%v", v)
}
