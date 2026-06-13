package tui

import (
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

type menuItem struct {
	key   string
	label string
	desc  string
	open  screen
	admin bool
}

func (m model) menuItems() []menuItem {
	return []menuItem{
		{key: "F", label: "Files", desc: "browse, upload, download, rename, move, copy, delete, search", open: screenFiles},
		{key: "S", label: "Shares", desc: "create/list/update/cancel shares; access visitor shares", open: screenShares},
		{key: "T", label: "Trash", desc: "list / restore / delete / empty trash", open: screenTrash},
		{key: "V", label: "Share Visitor", desc: "browse and download a shared link via access token", open: screenShareVisitor},
		{key: "P", label: "Profile", desc: "show profile, update nickname/avatar, change password, storage", open: screenProfile},
		{key: "L", label: "Operation Logs", desc: "list my operation history", open: screenLogs},
		{key: "Y", label: "System", desc: "system info, health check", open: screenSystem},
		{key: "A", label: "Admin Console", desc: "users / shares / stats / logs (requires admin role)", open: screenAdmin, admin: true},
	}
}

func (m model) menuUpdate(msg tea.Msg) (model, tea.Cmd) {
	items := m.menuItems()
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "up", "k":
			if m.menuIndex > 0 {
				m.menuIndex--
			}
		case "down", "j":
			if m.menuIndex < len(items)-1 {
				m.menuIndex++
			}
		case "home", "g":
			m.menuIndex = 0
		case "end", "G":
			m.menuIndex = len(items) - 1
		case "enter":
			return m.openMenu(items[m.menuIndex])
		case "q", "esc":
			return m, tea.Quit
		}
		// hotkey: first letter matches
		if len(msg.String()) == 1 {
			ch := strings.ToUpper(msg.String())
			for i, it := range items {
				if it.key == ch {
					m.menuIndex = i
					return m.openMenu(it)
				}
			}
		}
	}
	return m, nil
}

func (m model) openMenu(it menuItem) (model, tea.Cmd) {
	m.active = it.open
	m.SetStatus("opened " + it.label)
	var cmd tea.Cmd
	switch it.open {
	case screenFiles:
		cmd = m.filesReload()
	case screenShares:
		cmd = m.sharesReload()
	case screenTrash:
		cmd = m.trashReload()
	case screenProfile:
		cmd = m.profileReload()
	case screenLogs:
		cmd = m.logsReload()
	case screenSystem:
		cmd = m.systemReload()
	case screenAdmin:
		cmd = m.adminReload()
	case screenShareVisitor:
		cmd = m.visitorReload()
	}
	return m, cmd
}

func (m model) menuView() string {
	t := m.theme
	items := m.menuItems()
	var rows []string
	rows = append(rows, t.Banner.Render("◼ Main Menu"))
	rows = append(rows, t.Muted.Render("Pick a screen to interact with the disk backend."))
	rows = append(rows, "")
	for i, it := range items {
		var prefix string
		if i == m.menuIndex {
			prefix = t.MenuItemSel.Render("▶ " + it.key + "  " + it.label)
			rows = append(rows, prefix)
			rows = append(rows, t.MenuItemDesc.Padding(0, 0, 0, 5).Render(it.desc))
		} else {
			prefix = t.MenuItem.Render("  " + it.key + "  " + it.label)
			rows = append(rows, prefix)
		}
	}
	rows = append(rows, "")
	rows = append(rows, t.Muted.Render("↑↓/jk: navigate   Enter: open   letter: quick open   q/Esc: quit"))
	rows = append(rows, "")
	rows = append(rows, t.JoinHelp([][2]string{
		{"Ctrl+O", "logout"},
	}))
	content := lipgloss.JoinVertical(lipgloss.Left, rows...)
	return content
}

// handleGlobalKeys returns true if the key was consumed.
func (m *model) handleGlobalKeys(s string) bool {
	switch s {
	case "ctrl+o":
		go func() { _ = m.client.Logout(m.ctx) }()
		m.client.ClearTokens()
		if m.store != nil {
			m.store.ClearTokens()
			_ = m.store.Save()
		}
		m.SetStatus("logged out")
		m.active = screenLogin
		m.initLogin()
		return true
	case "ctrl+x":
		m.active = screenMenu
		m.menuIndex = 0
		return true
	}
	return false
}
