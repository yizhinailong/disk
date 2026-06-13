package tui

import (
	"context"
	"fmt"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/disk-tui/internal/client"
	"github.com/disk-tui/internal/util"
)

type systemState struct {
	info   *client.SystemInfo
	health *client.HealthResponse
}

type systemMsg struct {
	info   *client.SystemInfo
	health *client.HealthResponse
	err    error
}

func (m *model) initSystem() {}

func (m model) systemReload() tea.Cmd {
	c := m.client
	return func() tea.Msg {
		info, err := c.GetSystemInfo(context.Background())
		if err != nil {
			return systemMsg{err: err}
		}
		health, _ := c.Health(context.Background())
		icopy := info
		hcopy := health
		return systemMsg{info: &icopy, health: &hcopy}
	}
}

func (m model) systemUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case systemMsg:
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		m.system.info = msg.info
		m.system.health = msg.health
		m.SetStatus("system info loaded")
	case tea.KeyMsg:
		switch msg.String() {
		case "ctrl+x", "esc":
			m.active = screenMenu
		case "r":
			return m, m.systemReload()
		}
	}
	return m, nil
}

func (m model) systemPassResize(_ tea.WindowSizeMsg) {}

func (m model) systemView() string {
	t := m.theme
	var rows []string
	rows = append(rows, t.Banner.Render("◼ System"))
	rows = append(rows, "")
	if m.system.health != nil {
		h := m.system.health
		rows = append(rows, t.Accent.Render("Health:"))
		rows = append(rows, t.Label.Render("Status")+t.Value.Render(h.Status))
		if h.Version != "" {
			rows = append(rows, t.Label.Render("Version")+t.Value.Render(h.Version))
		}
		rows = append(rows, "")
	}
	if m.system.info != nil {
		si := m.system.info
		rows = append(rows, t.Accent.Render("System Info:"))
		rows = append(rows, t.Label.Render("Version")+t.Value.Render(si.Version))
		rows = append(rows, t.Label.Render("Drogon")+t.Value.Render(si.DrogonVersion))
		rows = append(rows, t.Label.Render("Build Time")+t.Value.Render(si.BuildTime))
		rows = append(rows, t.Label.Render("Uptime")+t.Value.Render(util.FormatDuration(uint64(si.Uptime))))
		rows = append(rows, "")
		rows = append(rows, t.Accent.Render("Connections:"))
		rows = append(rows, t.Label.Render("Current")+t.Value.Render(fmt.Sprintf("%d", si.Connections.Current)))
		rows = append(rows, t.Label.Render("Peak")+t.Value.Render(fmt.Sprintf("%d", si.Connections.Peak)))
		rows = append(rows, t.Label.Render("DB Pool")+t.Value.Render(fmt.Sprintf("%d", si.Connections.DBPoolSize)))
		rows = append(rows, t.Label.Render("Redis Pool")+t.Value.Render(fmt.Sprintf("%d", si.Connections.RedisPoolSize)))
		rows = append(rows, "")
		rows = append(rows, t.Accent.Render("Storage:"))
		rows = append(rows, t.Label.Render("Users")+t.Value.Render(fmt.Sprintf("%d", si.Storage.TotalUsers)))
		rows = append(rows, t.Label.Render("Files")+t.Value.Render(fmt.Sprintf("%d", si.Storage.TotalFiles)))
		rows = append(rows, t.Label.Render("Folders")+t.Value.Render(fmt.Sprintf("%d", si.Storage.TotalFolders)))
		rows = append(rows, t.Label.Render("Total Size")+t.Value.Render(util.FormatBytesI64(si.Storage.TotalSize)))
	} else {
		rows = append(rows, t.Muted.Render("(loading…)"))
	}
	rows = append(rows, "")
	rows = append(rows, t.JoinHelp([][2]string{
		{"r", "refresh"}, {"Ctrl+X", "menu"},
	}))
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}
