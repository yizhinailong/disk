package tui

import (
	"context"
	"fmt"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/disk-tui/internal/client"
	"github.com/disk-tui/internal/util"
)

type logsState struct {
	items []client.OperationLogItem
	page  int
}

type logsMsg struct {
	items []client.OperationLogItem
	total int
	err   error
}

func (m *model) initLogs() {
	if m.logs.page == 0 {
		m.logs.page = 1
	}
}

func (m model) logsReload() tea.Cmd {
	c := m.client
	page := m.logs.page
	return func() tea.Msg {
		r, err := c.ListOperationLogs(context.Background(), page, 100)
		if err != nil {
			return logsMsg{err: err}
		}
		return logsMsg{items: r.Items, total: r.Total}
	}
}

func (m model) logsUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case logsMsg:
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		m.logs.items = msg.items
		m.SetStatus(fmt.Sprintf("%d log entries (total=%d)", len(msg.items), msg.total))
	case tea.KeyMsg:
		switch msg.String() {
		case "ctrl+x", "esc":
			m.active = screenMenu
		case "r":
			return m, m.logsReload()
		case "left", "h":
			if m.logs.page > 1 {
				m.logs.page--
				return m, m.logsReload()
			}
		case "right", "l":
			m.logs.page++
			return m, m.logsReload()
		}
	}
	return m, nil
}

func (m model) logsPassResize(_ tea.WindowSizeMsg) {}

func (m model) logsView() string {
	t := m.theme
	var rows []string
	rows = append(rows, t.Banner.Render("◼ Operation Logs")+" "+t.Muted.Render(fmt.Sprintf("(page %d)", m.logs.page)))
	rows = append(rows, "")
	if len(m.logs.items) == 0 {
		rows = append(rows, t.Muted.Render("(no log entries)"))
	} else {
		header := strings.Join([]string{
			padRight("ID", 8),
			padRight("Action", 12),
			padRight("Target Type", 12),
			padRight("Target ID", 12),
			padRight("Target Name", 30),
			padRight("IP", 18),
			padRight("When", 20),
		}, "  ")
		rows = append(rows, t.ListHeader.Render(header))
		for _, it := range m.logs.items {
			line := strings.Join([]string{
				padRight(fmt.Sprintf("%d", it.ID), 8),
				padRight(it.Action, 12),
				padRight(it.TargetType, 12),
				padRight(fmt.Sprintf("%d", it.TargetID), 12),
				padRight(util.Truncate(it.TargetName, 30), 30),
				padRight(it.IPAddress, 18),
				padRight(util.FormatTime(it.CreatedAt), 20),
			}, "  ")
			rows = append(rows, line)
		}
	}
	rows = append(rows, "")
	rows = append(rows, t.JoinHelp([][2]string{
		{"r", "refresh"}, {"←/h", "prev page"}, {"→/l", "next page"},
		{"Ctrl+X", "menu"},
	}))
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}
