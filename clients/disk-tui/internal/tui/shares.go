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

type sharesMode int

const (
	sharesList sharesMode = iota
	sharesDetail
	sharesCreate
	sharesUpdate
	sharesCancel
)

type sharesState struct {
	mode         sharesMode
	items        []client.ShareItem
	selected     map[string]bool
	cursor       int
	statusFilter string
	detail       *client.ShareDetailResponse

	// create dialog
	createFiles    textinput.Model
	createFolders  textinput.Model
	createExpire   textinput.Model
	createPassword textinput.Model
	createPerm     textinput.Model
	createFocus    int

	// update dialog
	updateShareID  textinput.Model
	updateExpire   textinput.Model
	updatePassword textinput.Model
	updatePerm     textinput.Model
	updateFocus    int

	// cancel dialog
	cancelIDs textinput.Model
}

func (m *model) initShares() {
	if m.shares.createFiles.Value() != "" {
		return
	}
	m.shares.createFiles = newInput("file ids (comma sep)", "")
	m.shares.createFolders = newInput("folder ids (comma sep)", "")
	m.shares.createExpire = newInput("expire days (0=forever)", "7")
	m.shares.createPassword = newInput("password (optional, 4-8 chars)", "")
	m.shares.createPerm = newInput("permission (view/download)", "download")
	m.shares.updateShareID = newInput("share id", "")
	m.shares.updateExpire = newInput("expire days (blank=skip)", "")
	m.shares.updatePassword = newInput("password (blank=skip)", "")
	m.shares.updatePerm = newInput("permission (blank=skip)", "")
	m.shares.cancelIDs = newInput("share ids (comma sep)", "")
	m.shares.selected = map[string]bool{}
	m.shares.statusFilter = "all"
}

type sharesListMsg struct {
	items []client.ShareItem
	err   error
}
type shareOpMsg struct {
	summary string
	err     error
}
type shareDetailMsg struct {
	detail *client.ShareDetailResponse
	err    error
}

func (m model) sharesReload() tea.Cmd {
	m.shares.selected = map[string]bool{}
	c := m.client
	status := m.shares.statusFilter
	return func() tea.Msg {
		res, err := c.ListShares(context.Background(), status, 1, 100)
		if err != nil {
			return sharesListMsg{err: err}
		}
		return sharesListMsg{items: res.Items}
	}
}

func (m model) sharesUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case sharesListMsg:
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		m.shares.items = msg.items
		m.SetStatus(fmt.Sprintf("%d shares", len(msg.items)))
	case shareOpMsg:
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		m.SetStatus(msg.summary)
		return m, m.sharesReload()
	case shareDetailMsg:
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		m.shares.detail = msg.detail
		m.shares.mode = sharesDetail
	case tea.KeyMsg:
		if m.shares.mode != sharesList {
			return m.sharesDialogUpdate(msg)
		}
		switch msg.String() {
		case "ctrl+x", "esc":
			m.active = screenMenu
		case "up", "k":
			if m.shares.cursor > 0 {
				m.shares.cursor--
			}
		case "down", "j":
			if m.shares.cursor < len(m.shares.items)-1 {
				m.shares.cursor++
			}
		case " ":
			if len(m.shares.items) > 0 {
				id := m.shares.items[m.shares.cursor].ShareID
				m.shares.selected[id] = !m.shares.selected[id]
			}
		case "i", "enter":
			if len(m.shares.items) > 0 {
				id := m.shares.items[m.shares.cursor].ShareID
				c := m.client
				return m, func() tea.Msg {
					d, err := c.GetShareDetail(context.Background(), id)
					if err != nil {
						return shareDetailMsg{err: err}
					}
					return shareDetailMsg{detail: &d}
				}
			}
		case "c":
			m.shares.createFocus = 0
			m.shares.mode = sharesCreate
			return m, m.shares.createFiles.Focus()
		case "u":
			m.shares.updateFocus = 0
			if len(m.shares.items) > 0 {
				m.shares.updateShareID.SetValue(m.shares.items[m.shares.cursor].ShareID)
			}
			m.shares.mode = sharesUpdate
			return m, m.shares.updateShareID.Focus()
		case "x":
			m.shares.mode = sharesCancel
			if len(m.shares.items) > 0 {
				var ids []string
				for _, it := range m.shares.items {
					if m.shares.selected[it.ShareID] {
						ids = append(ids, it.ShareID)
					}
				}
				if len(ids) == 0 && len(m.shares.items) > 0 {
					ids = append(ids, m.shares.items[m.shares.cursor].ShareID)
				}
				m.shares.cancelIDs.SetValue(strings.Join(ids, ","))
			}
			return m, m.shares.cancelIDs.Focus()
		case "f":
			// cycle status filter
			m.shares.statusFilter = nextShareStatus(m.shares.statusFilter)
			m.SetStatus("filter: " + m.shares.statusFilter)
			return m, m.sharesReload()
		}
	}
	return m, nil
}

func nextShareStatus(s string) string {
	switch s {
	case "all":
		return "active"
	case "active":
		return "expired"
	case "expired":
		return "cancelled"
	default:
		return "all"
	}
}

func (m model) sharesDialogUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch m.shares.mode {
	case sharesCreate:
		return m.sharesCreateUpdate(msg)
	case sharesUpdate:
		return m.sharesUpdateUpdate(msg)
	case sharesCancel:
		return m.sharesCancelUpdate(msg)
	case sharesDetail:
		if km, ok := msg.(tea.KeyMsg); ok {
			if km.String() == "esc" || km.String() == "enter" {
				m.shares.mode = sharesList
				m.shares.detail = nil
			}
		}
		return m, nil
	}
	return m, nil
}

func parseIDList(s string) []uint64 {
	parts := strings.Split(s, ",")
	out := []uint64{}
	for _, p := range parts {
		p = strings.TrimSpace(p)
		if p == "" {
			continue
		}
		v, err := strconv.ParseUint(p, 10, 64)
		if err == nil {
			out = append(out, v)
		}
	}
	return out
}

func (m model) sharesCreateUpdate(msg tea.Msg) (model, tea.Cmd) {
	inputs := []*textinput.Model{
		&m.shares.createFiles, &m.shares.createFolders, &m.shares.createExpire,
		&m.shares.createPassword, &m.shares.createPerm,
	}
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "tab", "down":
			inputs[m.shares.createFocus].Blur()
			m.shares.createFocus = (m.shares.createFocus + 1) % len(inputs)
			return m, inputs[m.shares.createFocus].Focus()
		case "shift+tab", "up":
			inputs[m.shares.createFocus].Blur()
			m.shares.createFocus = (m.shares.createFocus - 1 + len(inputs)) % len(inputs)
			return m, inputs[m.shares.createFocus].Focus()
		case "esc":
			m.shares.mode = sharesList
			return m, nil
		case "enter":
			if m.shares.createFocus < len(inputs)-1 {
				inputs[m.shares.createFocus].Blur()
				m.shares.createFocus++
				return m, inputs[m.shares.createFocus].Focus()
			}
			files := parseIDList(m.shares.createFiles.Value())
			folders := parseIDList(m.shares.createFolders.Value())
			if len(files) == 0 && len(folders) == 0 {
				m.SetError("at least one file/folder id required")
				return m, nil
			}
			params := client.CreateShareParams{FileIDs: files, FolderIDs: folders, Permission: m.shares.createPerm.Value()}
			if exp, err := strconv.Atoi(strings.TrimSpace(m.shares.createExpire.Value())); err == nil {
				params.ExpireDays = &exp
			}
			if pwd := m.shares.createPassword.Value(); pwd != "" {
				params.Password = &pwd
			}
			c := m.client
			m.shares.mode = sharesList
			return m, func() tea.Msg {
				r, err := c.CreateShare(context.Background(), params)
				if err != nil {
					return shareOpMsg{err: err}
				}
				return shareOpMsg{summary: fmt.Sprintf("share created: %s", r.ShareID)}
			}
		}
		var cmd tea.Cmd
		*inputs[m.shares.createFocus], cmd = inputs[m.shares.createFocus].Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) sharesUpdateUpdate(msg tea.Msg) (model, tea.Cmd) {
	inputs := []*textinput.Model{
		&m.shares.updateShareID, &m.shares.updateExpire, &m.shares.updatePassword, &m.shares.updatePerm,
	}
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "tab", "down":
			inputs[m.shares.updateFocus].Blur()
			m.shares.updateFocus = (m.shares.updateFocus + 1) % len(inputs)
			return m, inputs[m.shares.updateFocus].Focus()
		case "shift+tab", "up":
			inputs[m.shares.updateFocus].Blur()
			m.shares.updateFocus = (m.shares.updateFocus - 1 + len(inputs)) % len(inputs)
			return m, inputs[m.shares.updateFocus].Focus()
		case "esc":
			m.shares.mode = sharesList
			return m, nil
		case "enter":
			if m.shares.updateFocus < len(inputs)-1 {
				inputs[m.shares.updateFocus].Blur()
				m.shares.updateFocus++
				return m, inputs[m.shares.updateFocus].Focus()
			}
			shareID := strings.TrimSpace(m.shares.updateShareID.Value())
			if shareID == "" {
				m.SetError("share id required")
				return m, nil
			}
			params := client.UpdateShareParams{}
			if s := strings.TrimSpace(m.shares.updateExpire.Value()); s != "" {
				if v, err := strconv.Atoi(s); err == nil {
					params.ExpireDays = &v
				}
			}
			if pwd := m.shares.updatePassword.Value(); pwd != "" {
				params.Password = &pwd
			}
			if perm := m.shares.updatePerm.Value(); perm != "" {
				params.Permission = &perm
			}
			c := m.client
			m.shares.mode = sharesList
			return m, func() tea.Msg {
				_, err := c.UpdateShare(context.Background(), shareID, params)
				if err != nil {
					return shareOpMsg{err: err}
				}
				return shareOpMsg{summary: "share updated"}
			}
		}
		var cmd tea.Cmd
		*inputs[m.shares.updateFocus], cmd = inputs[m.shares.updateFocus].Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) sharesCancelUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.shares.mode = sharesList
			return m, nil
		case "enter":
			ids := strings.Split(m.shares.cancelIDs.Value(), ",")
			for i := range ids {
				ids[i] = strings.TrimSpace(ids[i])
			}
			c := m.client
			m.shares.mode = sharesList
			return m, func() tea.Msg {
				r, err := c.CancelShares(context.Background(), ids)
				if err != nil {
					return shareOpMsg{err: err}
				}
				return shareOpMsg{summary: fmt.Sprintf("cancelled %d/%d shares", r.Summary.Succeeded, r.Summary.Total)}
			}
		}
		var cmd tea.Cmd
		m.shares.cancelIDs, cmd = m.shares.cancelIDs.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) sharesPassResize(msg tea.WindowSizeMsg) {
	w := msg.Width - 6
	if w < 20 {
		w = 20
	}
	for _, in := range []*textinput.Model{
		&m.shares.createFiles, &m.shares.createFolders, &m.shares.createExpire,
		&m.shares.createPassword, &m.shares.createPerm,
		&m.shares.updateShareID, &m.shares.updateExpire, &m.shares.updatePassword,
		&m.shares.updatePerm, &m.shares.cancelIDs,
	} {
		in.Width = w
	}
}

func (m model) sharesView() string {
	t := m.theme
	st := m.shares
	switch st.mode {
	case sharesCreate:
		return m.renderDialogRows("Create Share", [][2]string{
			{"File IDs", st.createFiles.Value() + st.createFiles.View()[len(st.createFiles.Value()):]},
		}, [][2]string{
			{"Folder IDs", st.createFolders.View()},
			{"Expire Days", st.createExpire.View()},
			{"Password", st.createPassword.View()},
			{"Permission", st.createPerm.View()},
		}, "Tab: next  Enter: submit  Esc: cancel", st.createFocus)
	case sharesUpdate:
		return m.renderDialogRows("Update Share", nil, [][2]string{
			{"Share ID", st.updateShareID.View()},
			{"Expire Days", st.updateExpire.View()},
			{"Password", st.updatePassword.View()},
			{"Permission", st.updatePerm.View()},
		}, "Tab: next  Enter: submit  Esc: cancel", st.updateFocus)
	case sharesCancel:
		return m.renderInputDialog("Cancel Shares (batch)", &st.cancelIDs, "Enter: cancel  |  Esc: back")
	case sharesDetail:
		return m.renderShareDetail()
	}
	var rows []string
	rows = append(rows, t.Banner.Render("◼ Shares")+" "+t.Muted.Render("["+st.statusFilter+"]"))
	rows = append(rows, "")
	if len(st.items) == 0 {
		rows = append(rows, t.Muted.Render("(no shares; press c to create one)"))
	} else {
		header := strings.Join([]string{
			padRight("Sel", 4),
			padRight("ID", 18),
			padRight("Name", 30),
			padRight("Status", 10),
			padRight("Views", 7),
			padRight("DLs", 7),
			padRight("Expires", 22),
		}, "  ")
		rows = append(rows, t.ListHeader.Render(header))
		for i, it := range st.items {
			sel := " "
			if st.selected[it.ShareID] {
				sel = "✓"
			}
			line := strings.Join([]string{
				padRight(sel, 4),
				padRight(util.Truncate(it.ShareID, 18), 18),
				padRight(util.Truncate(it.FileName, 30), 30),
				padRight(it.Status, 10),
				padRight(strconv.Itoa(it.ViewCount), 7),
				padRight(strconv.Itoa(it.DownloadCount), 7),
				padRight(util.FormatTime(it.ExpiresAt), 22),
			}, "  ")
			if i == st.cursor {
				line = lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#0B1220")).Background(lipgloss.Color("#7DD3FC")).Render(line)
			}
			rows = append(rows, line)
		}
	}
	rows = append(rows, "")
	rows = append(rows, t.JoinHelp([][2]string{
		{"c", "create"}, {"u", "update"}, {"x", "cancel"},
		{"i/Enter", "detail"}, {"␣", "select"}, {"f", "filter"},
		{"Ctrl+X", "menu"},
	}))
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}

func (m model) renderShareDetail() string {
	t := m.theme
	d := m.shares.detail
	if d == nil {
		return ""
	}
	var rows []string
	rows = append(rows, t.Banner.Render("◼ Share Detail"))
	rows = append(rows, "")
	rows = append(rows, t.Label.Render("Share ID")+t.Value.Render(d.ShareID))
	rows = append(rows, t.Label.Render("Link")+t.Value.Render(d.ShareLink))
	rows = append(rows, t.Label.Render("Permission")+t.Value.Render(d.Permission))
	rows = append(rows, t.Label.Render("Has Password")+t.Value.Render(fmt.Sprintf("%v", d.HasPassword)))
	rows = append(rows, t.Label.Render("Views")+t.Value.Render(strconv.Itoa(d.ViewCount)))
	rows = append(rows, t.Label.Render("Downloads")+t.Value.Render(strconv.Itoa(d.DownloadCount)))
	rows = append(rows, t.Label.Render("Created")+t.Value.Render(util.FormatTime(d.CreatedAt)))
	rows = append(rows, t.Label.Render("Expires")+t.Value.Render(util.FormatTime(d.ExpiresAt)))
	rows = append(rows, t.Label.Render("Status")+t.Value.Render(d.Status))
	rows = append(rows, "")
	if len(d.Files) > 0 {
		rows = append(rows, t.Accent.Render("Shared Files:"))
		for _, f := range d.Files {
			line := fmt.Sprintf("  %d  %s  %s",
				f.ID, f.Type, f.Name)
			if f.Type == "file" {
				line += "  (" + util.FormatBytes(f.Size) + ")"
			} else {
				line += fmt.Sprintf("  (%d items)", f.ItemCount)
			}
			rows = append(rows, line)
		}
	}
	rows = append(rows, "")
	rows = append(rows, t.Muted.Render("Esc/Enter: back"))
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}

// renderDialogRows is a small helper that renders a multi-line dialog
// with labels + inputs (passed via their already-rendered View()).
// activeIdx is the field currently focused (for highlight).
func (m model) renderDialogRows(title string, top [][2]string, fields [][2]string, help string, activeIdx int) string {
	t := m.theme
	rows := []string{t.Banner.Render("◼ " + title), ""}
	for _, f := range top {
		rows = append(rows, t.Label.Render(f[0])+f[1])
	}
	for i, f := range fields {
		var l string
		if i == activeIdx {
			l = lipgloss.NewStyle().Foreground(lipgloss.Color("#FBBF24")).Bold(true).Width(14).Render(f[0])
		} else {
			l = t.Label.Render(f[0])
		}
		rows = append(rows, l+f[1])
	}
	rows = append(rows, "")
	rows = append(rows, t.Muted.Render(help))
	dialog := t.Dialog.MaxWidth(100).Render(lipgloss.JoinVertical(lipgloss.Left, rows...))
	return lipgloss.Place(m.width, m.height-lipgloss.Height(m.renderHeader())-lipgloss.Height(m.renderFooter()),
		lipgloss.Center, lipgloss.Center, dialog)
}
