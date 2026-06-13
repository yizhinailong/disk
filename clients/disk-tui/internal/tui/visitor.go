package tui

import (
	"context"
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/disk-tui/internal/client"
	"github.com/disk-tui/internal/util"
)

type visitorMode int

const (
	visitorForm visitorMode = iota
	visitorBrowse
	visitorDialogDownload
	visitorDialogSave
)

type visitorState struct {
	mode         visitorMode
	shareID      textinput.Model
	password     textinput.Model
	folderID     uint64 // current browse folder within share
	breadcrumb   []client.BrowseBreadcrumb
	items        []client.BrowseItem
	cursor       int
	access       *client.AccessShareResponse
	downloadPath textinput.Model
	saveTarget   textinput.Model
	saveFileIDs  textinput.Model
}

type visitorMsg struct {
	access  *client.AccessShareResponse
	items   []client.BrowseItem
	bc      []client.BrowseBreadcrumb
	summary string
	err     error
}

func (m *model) initVisitor() {
	if m.visitor.shareID.Value() != "" {
		return
	}
	m.visitor.shareID = newInput("share id", "")
	m.visitor.password = newInput("password (optional)", "")
	m.visitor.password.EchoMode = textinput.EchoPassword
	m.visitor.downloadPath = newInput("local save path", "")
	m.visitor.saveTarget = newInput("target folder id (0 = root)", "0")
	m.visitor.saveFileIDs = newInput("file ids to save (comma sep)", "")
}

func (m model) visitorReload() tea.Cmd {
	// nothing async on enter; user submits a form
	return nil
}

func (m model) visitorUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case visitorMsg:
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		if msg.access != nil {
			m.visitor.access = msg.access
		}
		if msg.items != nil {
			m.visitor.items = msg.items
			m.visitor.breadcrumb = msg.bc
		}
		if msg.summary != "" {
			m.SetStatus(msg.summary)
		}
		return m, nil
	case tea.KeyMsg:
		switch m.visitor.mode {
		case visitorForm:
			return m.visitorFormUpdate(msg)
		case visitorBrowse:
			return m.visitorBrowseUpdate(msg)
		case visitorDialogDownload:
			return m.visitorDownloadDialog(msg)
		case visitorDialogSave:
			return m.visitorSaveDialog(msg)
		}
	}
	return m, nil
}

func (m model) visitorFormUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "tab":
			if m.visitor.shareID.Focused() {
				m.visitor.shareID.Blur()
				return m, m.visitor.password.Focus()
			}
			m.visitor.password.Blur()
			return m, m.visitor.shareID.Focus()
		case "esc":
			m.active = screenMenu
		case "enter":
			shareID := strings.TrimSpace(m.visitor.shareID.Value())
			if shareID == "" {
				m.SetError("share id required")
				return m, nil
			}
			pwd := m.visitor.password.Value()
			c := m.client
			return m, func() tea.Msg {
				access, err := c.AccessShare(context.Background(), shareID, pwd)
				if err != nil {
					return visitorMsg{err: err}
				}
				// immediately browse root
				browse, berr := c.BrowseShare(context.Background(), shareID, 0)
				if berr != nil {
					return visitorMsg{err: berr}
				}
				acopy := access
				return visitorMsg{access: &acopy, items: browse.Items, bc: browse.Breadcrumb, summary: "share accessed"}
			}
		}
		var cmd tea.Cmd
		if m.visitor.shareID.Focused() {
			m.visitor.shareID, cmd = m.visitor.shareID.Update(msg)
		} else {
			m.visitor.password, cmd = m.visitor.password.Update(msg)
		}
		return m, cmd
	}
	return m, nil
}

func (m model) visitorBrowseUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "ctrl+x", "esc":
			m.client.ClearShareToken()
			m.visitor.items = nil
			m.visitor.folderID = 0
			m.visitor.mode = visitorForm
			m.SetStatus("left share")
			return m, m.visitor.shareID.Focus()
		case "up", "k":
			if m.visitor.cursor > 0 {
				m.visitor.cursor--
			}
		case "down", "j":
			if m.visitor.cursor < len(m.visitor.items)-1 {
				m.visitor.cursor++
			}
		case "enter", "l", "right":
			if len(m.visitor.items) == 0 {
				return m, nil
			}
			it := m.visitor.items[m.visitor.cursor]
			if it.Type == "folder" {
				// descend
				shareID := strings.TrimSpace(m.visitor.shareID.Value())
				folderID := it.ID
				c := m.client
				return m, func() tea.Msg {
					r, err := c.BrowseShare(context.Background(), shareID, folderID)
					if err != nil {
						return visitorMsg{err: err}
					}
					return visitorMsg{items: r.Items, bc: r.Breadcrumb}
				}
			}
			// download
			m.visitor.downloadPath.SetValue(it.Name)
			m.visitor.mode = visitorDialogDownload
			return m, m.visitor.downloadPath.Focus()
		case "h", "left", "backspace":
			// go up via breadcrumb
			if len(m.visitor.breadcrumb) >= 2 {
				shareID := strings.TrimSpace(m.visitor.shareID.Value())
				parent := m.visitor.breadcrumb[len(m.visitor.breadcrumb)-2]
				c := m.client
				return m, func() tea.Msg {
					r, err := c.BrowseShare(context.Background(), shareID, parent.ID)
					if err != nil {
						return visitorMsg{err: err}
					}
					return visitorMsg{items: r.Items, bc: r.Breadcrumb}
				}
			}
		case "s":
			// save share items (requires both JWT + share token)
			if !m.client.IsAuthenticated() {
				m.SetError("save requires login")
				return m, nil
			}
			m.visitor.saveFileIDs.SetValue("")
			m.visitor.saveTarget.SetValue("0")
			m.visitor.mode = visitorDialogSave
			return m, m.visitor.saveFileIDs.Focus()
		}
	}
	return m, nil
}

func (m model) visitorDownloadDialog(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.visitor.mode = visitorBrowse
			return m, nil
		case "enter":
			dst := strings.TrimSpace(m.visitor.downloadPath.Value())
			if dst == "" {
				m.SetError("destination required")
				return m, nil
			}
			if len(m.visitor.items) == 0 {
				return m, nil
			}
			it := m.visitor.items[m.visitor.cursor]
			if it.Type != "file" {
				m.SetError("can only download files")
				return m, nil
			}
			shareID := strings.TrimSpace(m.visitor.shareID.Value())
			c := m.client
			m.visitor.mode = visitorBrowse
			return m, func() tea.Msg {
				f, err := os.Create(dst)
				if err != nil {
					return visitorMsg{err: err}
				}
				defer f.Close()
				if _, err := c.DownloadShareFile(context.Background(), shareID, it.ID, f); err != nil {
					return visitorMsg{err: err}
				}
				return visitorMsg{summary: "downloaded to " + dst}
			}
		}
		var cmd tea.Cmd
		m.visitor.downloadPath, cmd = m.visitor.downloadPath.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) visitorSaveDialog(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "tab":
			if m.visitor.saveFileIDs.Focused() {
				m.visitor.saveFileIDs.Blur()
				return m, m.visitor.saveTarget.Focus()
			}
			m.visitor.saveTarget.Blur()
			return m, m.visitor.saveFileIDs.Focus()
		case "esc":
			m.visitor.mode = visitorBrowse
			return m, nil
		case "enter":
			shareID := strings.TrimSpace(m.visitor.shareID.Value())
			fileIDs := parseIDList(m.visitor.saveFileIDs.Value())
			target, _ := strconv.ParseUint(strings.TrimSpace(m.visitor.saveTarget.Value()), 10, 64)
			c := m.client
			m.visitor.mode = visitorBrowse
			return m, func() tea.Msg {
				r, err := c.SaveShareItems(context.Background(), shareID, client.SaveShareItemsParams{
					FileIDs: fileIDs, TargetFolderID: target,
				})
				if err != nil {
					return visitorMsg{err: err}
				}
				return visitorMsg{summary: fmt.Sprintf("saved %d files into my drive", r.SavedCount)}
			}
		}
		var cmd tea.Cmd
		if m.visitor.saveFileIDs.Focused() {
			m.visitor.saveFileIDs, cmd = m.visitor.saveFileIDs.Update(msg)
		} else {
			m.visitor.saveTarget, cmd = m.visitor.saveTarget.Update(msg)
		}
		return m, cmd
	}
	return m, nil
}

func (m model) visitorPassResize(msg tea.WindowSizeMsg) {
	w := msg.Width - 6
	if w < 20 {
		w = 20
	}
	for _, in := range []*textinput.Model{
		&m.visitor.shareID, &m.visitor.password,
		&m.visitor.downloadPath, &m.visitor.saveTarget, &m.visitor.saveFileIDs,
	} {
		in.Width = w
	}
}

func (m model) visitorView() string {
	t := m.theme
	switch m.visitor.mode {
	case visitorForm:
		var rows []string
		rows = append(rows, t.Banner.Render("◼ Open Share Link"))
		rows = append(rows, "")
		rows = append(rows, t.Label.Render("Share ID")+m.visitor.shareID.View())
		rows = append(rows, t.Label.Render("Password")+m.visitor.password.View())
		rows = append(rows, "")
		rows = append(rows, t.Muted.Render("Tab: switch  Enter: access  Esc: back"))
		dialog := t.Dialog.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))
		return lipgloss.Place(m.width, m.height-lipgloss.Height(m.renderHeader())-lipgloss.Height(m.renderFooter()),
			lipgloss.Center, lipgloss.Center, dialog)
	case visitorDialogDownload:
		return m.renderInputDialog("Download Shared File", &m.visitor.downloadPath, "Enter: download  |  Esc: cancel")
	case visitorDialogSave:
		var rows []string
		rows = append(rows, t.Banner.Render("◼ Save Shared Items to My Drive"))
		rows = append(rows, "")
		rows = append(rows, t.Label.Render("File IDs")+m.visitor.saveFileIDs.View())
		rows = append(rows, t.Label.Render("Target Folder")+m.visitor.saveTarget.View())
		rows = append(rows, "")
		rows = append(rows, t.Muted.Render("Tab: switch  Enter: save  Esc: cancel"))
		dialog := t.Dialog.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))
		return lipgloss.Place(m.width, m.height-lipgloss.Height(m.renderHeader())-lipgloss.Height(m.renderFooter()),
			lipgloss.Center, lipgloss.Center, dialog)
	}

	var rows []string
	rows = append(rows, t.Banner.Render("◼ Shared Content")+" "+t.Muted.Render("("+strings.TrimSpace(m.visitor.shareID.Value())+")"))
	// breadcrumb
	bcParts := []string{t.Folder.Render("/")}
	for _, c := range m.visitor.breadcrumb {
		bcParts = append(bcParts, t.Folder.Render(c.Name))
	}
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Left, bcParts...))
	rows = append(rows, "")
	if len(m.visitor.items) == 0 {
		rows = append(rows, t.Muted.Render("(no items)"))
	} else {
		header := strings.Join([]string{
			padRight("Type", 7),
			padRight("Name", 40),
			padRight("Size/Items", 14),
		}, "  ")
		rows = append(rows, t.ListHeader.Render(header))
		for i, it := range m.visitor.items {
			mark := "📄"
			sizeStr := util.FormatBytes(it.Size)
			if it.Type == "folder" {
				mark = "📁"
				sizeStr = fmt.Sprintf("%d items", it.ItemCount)
			}
			line := strings.Join([]string{
				padRight(mark, 7),
				padRight(util.Truncate(it.Name, 40), 40),
				padRight(sizeStr, 14),
			}, "  ")
			if i == m.visitor.cursor {
				line = lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#0B1220")).Background(lipgloss.Color("#A5B4FC")).Render(line)
			}
			rows = append(rows, line)
		}
	}
	rows = append(rows, "")
	rows = append(rows, t.JoinHelp([][2]string{
		{"↑↓/jk", "nav"}, {"→/l", "open/down"}, {"←/h", "up"},
		{"s", "save to my drive"}, {"Esc", "exit share"},
	}))
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}
