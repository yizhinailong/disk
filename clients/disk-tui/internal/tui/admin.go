package tui

import (
	"context"
	"fmt"
	"strconv"
	"strings"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/disk-tui/internal/client"
	"github.com/disk-tui/internal/util"
)

type adminTab int

const (
	adminUsers adminTab = iota
	adminShares
	adminStats
	adminLogs
)

type adminState struct {
	tab      adminTab
	users    []client.AdminUserDetail
	shares   []client.AdminShareDetail
	logs     []client.AdminLogDetail
	stats    *client.AdminStorageStats
	system   *client.AdminSystemStatus
	overview map[string]any
	cursor   int

	// dialogs
	loading      bool
	dialogOpen   bool
	dialogKind   string // "status" | "role" | "space" | "delete-user" | "force-cancel"
	dialogUserID uint64
	dialogInput1 textinput.Model
	dialogInput2 textinput.Model
	dialogFocus  int
}

type adminMsg struct {
	tab      adminTab
	users    []client.AdminUserDetail
	shares   []client.AdminShareDetail
	logs     []client.AdminLogDetail
	stats    *client.AdminStorageStats
	system   *client.AdminSystemStatus
	overview map[string]any
	summary  string
	err      error
}

func (m *model) initAdmin() {
	if m.admin.dialogInput1.Value() != "" {
		return
	}
	m.admin.dialogInput1 = newInput("value 1", "")
	m.admin.dialogInput2 = newInput("value 2", "")
}

func (m model) adminReload() tea.Cmd {
	m.admin.loading = true
	c := m.client
	return func() tea.Msg {
		users, err := c.AdminListUsers(context.Background(), client.AdminListUsersParams{Page: 1, PageSize: 100})
		if err != nil {
			return adminMsg{err: err}
		}
		shares, serr := c.AdminListShares(context.Background(), client.AdminListSharesParams{Page: 1, PageSize: 100})
		stats, sterr := c.AdminStorageStats(context.Background())
		sys, syerr := c.AdminSystemStatus(context.Background())
		ov, _ := c.AdminOverviewStats(context.Background())
		logs, lerr := c.AdminLogs(context.Background(), client.AdminLogsParams{Page: 1, PageSize: 100})
		out := adminMsg{
			users:    users.Items,
			overview: ov,
		}
		if serr == nil {
			out.shares = shares.Items
		}
		if sterr == nil {
			s := stats
			out.stats = &s
		}
		if syerr == nil {
			sy := sys
			out.system = &sy
		}
		if lerr == nil {
			out.logs = logs.Items
		}
		return out
	}
}

func (m model) adminUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case adminMsg:
		m.admin.loading = false
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		if msg.users != nil {
			m.admin.users = msg.users
		}
		if msg.shares != nil {
			m.admin.shares = msg.shares
		}
		if msg.logs != nil {
			m.admin.logs = msg.logs
		}
		if msg.stats != nil {
			m.admin.stats = msg.stats
		}
		if msg.system != nil {
			m.admin.system = msg.system
		}
		if msg.overview != nil {
			m.admin.overview = msg.overview
		}
		if msg.summary != "" {
			m.SetStatus(msg.summary)
		} else {
			m.SetStatus("admin data loaded")
		}
		return m, nil
	case tea.KeyMsg:
		if m.admin.dialogOpen {
			return m.adminDialogUpdate(msg)
		}
		switch msg.String() {
		case "ctrl+x", "esc":
			m.active = screenMenu
		case "1":
			m.admin.tab = adminUsers
			m.admin.cursor = 0
		case "2":
			m.admin.tab = adminShares
			m.admin.cursor = 0
		case "3":
			m.admin.tab = adminStats
		case "4":
			m.admin.tab = adminLogs
		case "tab":
			m.admin.tab = (m.admin.tab + 1) % 4
			m.admin.cursor = 0
		case "r":
			return m, m.adminReload()
		case "up", "k":
			if m.admin.cursor > 0 {
				m.admin.cursor--
			}
		case "down", "j":
			maxIdx := m.adminMaxCursor()
			if m.admin.cursor < maxIdx {
				m.admin.cursor++
			}
		case "enter":
			return m.adminActionPrimary()
		case "s":
			return m.adminAction("status")
		case "e":
			return m.adminAction("role")
		case "q":
			return m.adminAction("space")
		case "D":
			return m.adminAction("delete-user")
		case "X":
			return m.adminAction("force-cancel")
		}
	}
	return m, nil
}

func (m model) adminMaxCursor() int {
	switch m.admin.tab {
	case adminUsers:
		return len(m.admin.users) - 1
	case adminShares:
		return len(m.admin.shares) - 1
	case adminLogs:
		return len(m.admin.logs) - 1
	}
	return 0
}

func (m model) adminActionPrimary() (model, tea.Cmd) {
	// On users tab enter opens status dialog; on shares tab enter cancels.
	switch m.admin.tab {
	case adminUsers:
		return m.adminAction("status")
	case adminShares:
		return m.adminAction("force-cancel")
	}
	return m, nil
}

func (m model) adminAction(kind string) (model, tea.Cmd) {
	c := m.client
	switch kind {
	case "delete-user":
		if len(m.admin.users) == 0 {
			return m, nil
		}
		u := m.admin.users[m.admin.cursor]
		return m, func() tea.Msg {
			err := c.AdminDeleteUser(context.Background(), u.ID)
			if err != nil {
				return adminMsg{err: err}
			}
			return adminMsg{summary: fmt.Sprintf("soft-deleted user %s", u.Username)}
		}
	case "force-cancel":
		if len(m.admin.shares) == 0 {
			return m, nil
		}
		s := m.admin.shares[m.admin.cursor]
		return m, func() tea.Msg {
			err := c.AdminForceCancelShare(context.Background(), s.ShareID)
			if err != nil {
				return adminMsg{err: err}
			}
			return adminMsg{summary: fmt.Sprintf("force-cancelled share %s", s.ShareID)}
		}
	case "status", "role", "space":
		if len(m.admin.users) == 0 {
			return m, nil
		}
		u := m.admin.users[m.admin.cursor]
		m.admin.dialogOpen = true
		m.admin.dialogKind = kind
		m.admin.dialogUserID = u.ID
		m.admin.dialogFocus = 0
		m.admin.dialogInput1.SetValue("")
		switch kind {
		case "status":
			m.admin.dialogInput1.SetValue(strconv.Itoa(u.Status))
		case "role":
			m.admin.dialogInput1.SetValue(strconv.Itoa(u.Role))
		case "space":
			m.admin.dialogInput1.SetValue(strconv.FormatUint(u.StorageQuota/(1024*1024*1024), 10))
		}
		return m, m.admin.dialogInput1.Focus()
	}
	return m, nil
}

func (m model) adminDialogUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.admin.dialogOpen = false
			return m, nil
		case "enter":
			c := m.client
			id := m.admin.dialogUserID
			kind := m.admin.dialogKind
			m.admin.dialogOpen = false
			return m, func() tea.Msg {
				switch kind {
				case "status":
					v, err := strconv.Atoi(strings.TrimSpace(m.admin.dialogInput1.Value()))
					if err != nil {
						return adminMsg{err: fmt.Errorf("invalid status")}
					}
					if _, err := c.AdminChangeUserStatus(context.Background(), id, v); err != nil {
						return adminMsg{err: err}
					}
					return adminMsg{summary: "user status updated"}
				case "role":
					v, err := strconv.Atoi(strings.TrimSpace(m.admin.dialogInput1.Value()))
					if err != nil {
						return adminMsg{err: fmt.Errorf("invalid role")}
					}
					if _, err := c.AdminChangeUserRole(context.Background(), id, v); err != nil {
						return adminMsg{err: err}
					}
					return adminMsg{summary: "user role updated"}
				case "space":
					v, err := strconv.ParseUint(strings.TrimSpace(m.admin.dialogInput1.Value()), 10, 64)
					if err != nil {
						return adminMsg{err: fmt.Errorf("invalid space")}
					}
					if _, err := c.AdminChangeUserAvailableSpace(context.Background(), id, v); err != nil {
						return adminMsg{err: err}
					}
					return adminMsg{summary: "user available space updated"}
				}
				return adminMsg{}
			}
		}
		var cmd tea.Cmd
		m.admin.dialogInput1, cmd = m.admin.dialogInput1.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) adminPassResize(msg tea.WindowSizeMsg) {
	w := msg.Width - 6
	if w < 20 {
		w = 20
	}
	m.admin.dialogInput1.Width = w
	m.admin.dialogInput2.Width = w
}

func (m model) adminView() string {
	t := m.theme
	if m.admin.dialogOpen {
		title := "Dialog"
		hint := ""
		switch m.admin.dialogKind {
		case "status":
			title = "Change User Status"
			hint = "0=disabled  1=active  2=locked"
		case "role":
			title = "Change User Role"
			hint = "0=user  1=admin"
		case "space":
			title = "Change Available Space (G)"
			hint = "non-negative integer in GB"
		}
		var rows []string
		rows = append(rows, t.Banner.Render("◼ "+title))
		rows = append(rows, "")
		rows = append(rows, t.Label.Render("User ID")+t.Value.Render(fmt.Sprintf("%d", m.admin.dialogUserID)))
		rows = append(rows, t.Label.Render("Value")+m.admin.dialogInput1.View())
		rows = append(rows, "")
		rows = append(rows, t.Muted.Render(hint))
		rows = append(rows, t.Muted.Render("Enter: submit  Esc: cancel"))
		dialog := t.Dialog.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))
		return lipgloss.Place(m.width, m.height-lipgloss.Height(m.renderHeader())-lipgloss.Height(m.renderFooter()),
			lipgloss.Center, lipgloss.Center, dialog)
	}

	var rows []string
	rows = append(rows, t.Banner.Render("◼ Admin Console"))
	rows = append(rows, t.Muted.Render("[1] Users  [2] Shares  [3] Stats  [4] Logs   Tab: cycle"))
	rows = append(rows, "")
	switch m.admin.tab {
	case adminUsers:
		rows = append(rows, m.adminUsersView())
	case adminShares:
		rows = append(rows, m.adminSharesView())
	case adminStats:
		rows = append(rows, m.adminStatsView())
	case adminLogs:
		rows = append(rows, m.adminLogsView())
	}
	rows = append(rows, "")
	rows = append(rows, t.JoinHelp([][2]string{
		{"s", "user status"}, {"e", "user role"}, {"q", "user space"},
		{"D", "delete user"}, {"X", "force-cancel share"},
		{"r", "refresh"}, {"Ctrl+X", "menu"},
	}))
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}

func (m model) adminUsersView() string {
	t := m.theme
	if len(m.admin.users) == 0 {
		return t.Muted.Render("(no users)")
	}
	header := strings.Join([]string{
		padRight("ID", 6),
		padRight("Username", 16),
		padRight("Email", 24),
		padRight("Role", 7),
		padRight("Status", 9),
		padRight("Used", 12),
		padRight("Quota", 12),
		padRight("Created", 20),
	}, "  ")
	rows := []string{t.ListHeader.Render(header)}
	for i, u := range m.admin.users {
		line := strings.Join([]string{
			padRight(strconv.FormatUint(u.ID, 10), 6),
			padRight(util.Truncate(u.Username, 16), 16),
			padRight(util.Truncate(u.Email, 24), 24),
			padRight(util.RoleName(u.Role), 7),
			padRight(util.UserStatusName(u.Status), 9),
			padRight(util.FormatBytes(u.StorageUsed), 12),
			padRight(util.FormatBytes(u.StorageQuota), 12),
			padRight(util.FormatTime(u.CreatedAt), 20),
		}, "  ")
		if i == m.admin.cursor {
			line = lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#0B1220")).Background(lipgloss.Color("#FBBF24")).Render(line)
		}
		rows = append(rows, line)
	}
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}

func (m model) adminSharesView() string {
	t := m.theme
	if len(m.admin.shares) == 0 {
		return t.Muted.Render("(no shares)")
	}
	header := strings.Join([]string{
		padRight("ID", 6),
		padRight("User", 16),
		padRight("File", 24),
		padRight("Status", 7),
		padRight("Views", 7),
		padRight("Pwd", 5),
		padRight("Expires", 22),
	}, "  ")
	rows := []string{t.ListHeader.Render(header)}
	for i, s := range m.admin.shares {
		pwd := "no"
		if s.PasswordSet {
			pwd = "yes"
		}
		line := strings.Join([]string{
			padRight(s.ShareID, 6),
			padRight(util.Truncate(s.Username, 16), 16),
			padRight(util.Truncate(s.FileName, 24), 24),
			padRight(strconv.Itoa(s.Status), 7),
			padRight(strconv.Itoa(s.AccessCount), 7),
			padRight(pwd, 5),
			padRight(util.FormatTime(s.ExpiresAt), 22),
		}, "  ")
		if i == m.admin.cursor {
			line = lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#0B1220")).Background(lipgloss.Color("#FBBF24")).Render(line)
		}
		rows = append(rows, line)
	}
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}

func (m model) adminStatsView() string {
	t := m.theme
	var rows []string
	if m.admin.overview != nil {
		rows = append(rows, t.Accent.Render("Overview:"))
		for k, v := range m.admin.overview {
			rows = append(rows, t.Label.Render(k)+t.Value.Render(fmt.Sprintf("%v", v)))
		}
		rows = append(rows, "")
	}
	if m.admin.stats != nil {
		s := m.admin.stats
		rows = append(rows, t.Accent.Render("Storage Stats:"))
		rows = append(rows, t.Label.Render("Users")+t.Value.Render(fmt.Sprintf("%d", s.TotalUsers)))
		rows = append(rows, t.Label.Render("Files")+t.Value.Render(fmt.Sprintf("%d", s.TotalFiles)))
		rows = append(rows, t.Label.Render("Used")+t.Value.Render(util.FormatBytes(s.TotalStorageUsed)))
		rows = append(rows, t.Label.Render("Quota")+t.Value.Render(util.FormatBytes(s.TotalStorageQuota)))
		rows = append(rows, t.Label.Render("Active Shares")+t.Value.Render(fmt.Sprintf("%d", s.ActiveShares)))
	}
	if m.admin.system != nil {
		rows = append(rows, "")
		rows = append(rows, t.Accent.Render("System Status:"))
		sy := m.admin.system
		rows = append(rows, t.Label.Render("DB")+t.Value.Render(boolStr(sy.DBConnected)))
		rows = append(rows, t.Label.Render("Redis")+t.Value.Render(boolStr(sy.RedisConnected)))
		rows = append(rows, t.Label.Render("Disk Total")+t.Value.Render(util.FormatBytes(sy.DiskTotal)))
		rows = append(rows, t.Label.Render("Disk Used")+t.Value.Render(util.FormatBytes(sy.DiskUsed)))
		rows = append(rows, t.Label.Render("Disk Free")+t.Value.Render(util.FormatBytes(sy.DiskFree)))
		rows = append(rows, t.Label.Render("Uptime")+t.Value.Render(util.FormatDuration(sy.UptimeSeconds)))
	}
	if len(rows) == 0 {
		return t.Muted.Render("(no data)")
	}
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}

func (m model) adminLogsView() string {
	t := m.theme
	if len(m.admin.logs) == 0 {
		return t.Muted.Render("(no admin log entries)")
	}
	header := strings.Join([]string{
		padRight("ID", 6),
		padRight("User", 8),
		padRight("Action", 14),
		padRight("Target Type", 12),
		padRight("Target ID", 12),
		padRight("IP", 18),
		padRight("When", 20),
	}, "  ")
	rows := []string{t.ListHeader.Render(header)}
	for i, l := range m.admin.logs {
		tgtID := "-"
		if l.TargetID != nil {
			tgtID = fmt.Sprintf("%d", *l.TargetID)
		}
		line := strings.Join([]string{
			padRight(strconv.FormatUint(l.ID, 10), 6),
			padRight(strconv.FormatUint(l.UserID, 10), 8),
			padRight(util.Truncate(l.Action, 14), 14),
			padRight(util.Truncate(l.TargetType, 12), 12),
			padRight(tgtID, 12),
			padRight(l.IPAddress, 18),
			padRight(util.FormatTime(l.CreatedAt), 20),
		}, "  ")
		if i == m.admin.cursor {
			line = lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#0B1220")).Background(lipgloss.Color("#FBBF24")).Render(line)
		}
		rows = append(rows, line)
	}
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}

func boolStr(b bool) string {
	if b {
		return "connected"
	}
	return "disconnected"
}
