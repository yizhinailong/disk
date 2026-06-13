package tui

import (
	"context"
	"fmt"
	"strconv"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/disk-tui/internal/client"
	"github.com/disk-tui/internal/util"
)

type trashState struct {
	items    []client.TrashItemResponse
	selected map[uint64]bool
	cursor   int
	loading  bool
	page     int
}

type trashMsg struct {
	items []client.TrashItemResponse
	err   error
}
type trashOpMsg struct {
	summary string
	err     error
}

func (m *model) initTrash() {
	if m.trash.selected == nil {
		m.trash.selected = map[uint64]bool{}
		m.trash.page = 1
	}
}

func (m model) trashReload() tea.Cmd {
	c := m.client
	page := m.trash.page
	return func() tea.Msg {
		r, err := c.ListTrash(context.Background(), page, 100)
		if err != nil {
			return trashMsg{err: err}
		}
		return trashMsg{items: r.Items}
	}
}

func (m model) trashUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case trashMsg:
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		m.trash.items = msg.items
		m.SetStatus(fmt.Sprintf("%d trash items", len(msg.items)))
	case trashOpMsg:
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		m.SetStatus(msg.summary)
		return m, m.trashReload()
	case tea.KeyMsg:
		switch msg.String() {
		case "ctrl+x", "esc":
			m.active = screenMenu
		case "up", "k":
			if m.trash.cursor > 0 {
				m.trash.cursor--
			}
		case "down", "j":
			if m.trash.cursor < len(m.trash.items)-1 {
				m.trash.cursor++
			}
		case " ":
			if len(m.trash.items) > 0 {
				id := m.trash.items[m.trash.cursor].ID
				m.trash.selected[id] = !m.trash.selected[id]
			}
		case "a":
			for _, it := range m.trash.items {
				m.trash.selected[it.ID] = true
			}
		case "A":
			m.trash.selected = map[uint64]bool{}
		case "r":
			return m.trashBatch("restore")
		case "d":
			return m.trashBatch("delete")
		case "x":
			return m.trashEmpty()
		}
	}
	return m, nil
}

func (m model) trashBatch(kind string) (model, tea.Cmd) {
	var ids []uint64
	for _, it := range m.trash.items {
		if m.trash.selected[it.ID] {
			ids = append(ids, it.ID)
		}
	}
	if len(ids) == 0 && len(m.trash.items) > 0 {
		ids = append(ids, m.trash.items[m.trash.cursor].ID)
	}
	if len(ids) == 0 {
		m.SetError("nothing selected")
		return m, nil
	}
	c := m.client
	return m, func() tea.Msg {
		if kind == "restore" {
			r, err := c.RestoreTrash(context.Background(), ids)
			if err != nil {
				return trashOpMsg{err: err}
			}
			return trashOpMsg{summary: fmt.Sprintf("restored %d/%d", r.Summary.SuccessCount, r.Summary.Total)}
		}
		r, err := c.DeleteTrash(context.Background(), ids)
		if err != nil {
			return trashOpMsg{err: err}
		}
		return trashOpMsg{summary: fmt.Sprintf("deleted %d/%d (permanent)", r.Summary.SuccessCount, r.Summary.Total)}
	}
}

func (m model) trashEmpty() (model, tea.Cmd) {
	c := m.client
	return m, func() tea.Msg {
		r, err := c.EmptyTrash(context.Background())
		if err != nil {
			return trashOpMsg{err: err}
		}
		return trashOpMsg{summary: fmt.Sprintf("emptied trash: %d items, freed %s",
			r.DeletedCount, util.FormatBytes(r.FreedSpace))}
	}
}

func (m model) trashPassResize(msg tea.WindowSizeMsg) {}

func (m model) trashView() string {
	t := m.theme
	var rows []string
	rows = append(rows, t.Banner.Render("◼ Trash"))
	rows = append(rows, "")
	if len(m.trash.items) == 0 {
		rows = append(rows, t.Muted.Render("(trash is empty)"))
	} else {
		header := strings.Join([]string{
			padRight("Sel", 4),
			padRight("ID", 8),
			padRight("Type", 7),
			padRight("Name", 32),
			padRight("Size", 12),
			padRight("Original Path", 30),
			padRight("Deleted", 20),
			padRight("Expires", 20),
		}, "  ")
		rows = append(rows, t.ListHeader.Render(header))
		for i, it := range m.trash.items {
			sel := " "
			if m.trash.selected[it.ID] {
				sel = "✓"
			}
			sizeStr := util.FormatBytes(it.Size)
			if it.Type == "folder" {
				sizeStr = "-"
			}
			line := strings.Join([]string{
				padRight(sel, 4),
				padRight(strconv.FormatUint(it.ID, 10), 8),
				padRight(it.Type, 7),
				padRight(util.Truncate(it.Name, 32), 32),
				padRight(sizeStr, 12),
				padRight(util.Truncate(it.OriginalPath, 30), 30),
				padRight(util.FormatTime(it.DeletedAt), 20),
				padRight(util.FormatTime(it.ExpiresAt), 20),
			}, "  ")
			if i == m.trash.cursor {
				line = lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#0B1220")).Background(lipgloss.Color("#7DD3FC")).Render(line)
			}
			rows = append(rows, line)
		}
	}
	rows = append(rows, "")
	rows = append(rows, t.JoinHelp([][2]string{
		{"r", "restore"}, {"d", "delete perm"}, {"x", "empty all"},
		{"␣", "select"}, {"a/A", "sel all/none"}, {"Ctrl+X", "menu"},
	}))
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}
