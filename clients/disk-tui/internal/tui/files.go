package tui

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/disk-tui/internal/client"
	"github.com/disk-tui/internal/util"
)

// filesMode controls which sub-view is shown on the files screen.
type filesMode int

const (
	filesBrowse filesMode = iota
	filesSearch
	filesDialogRename
	filesDialogMoveCopy
	filesDialogMkdir
	filesDialogUpload
	filesDialogDownload
	filesDialogDetail
)

// filesDialogSub selects between copy/move during the move-copy dialog.
type filesDialogSub int

const (
	dialogSubMove filesDialogSub = iota
	dialogSubCopy
)

type filesState struct {
	mode           filesMode
	dialogSub      filesDialogSub
	parentID       uint64
	history        []uint64 // navigation stack for "back"
	breadcrumb     []client.BreadcrumbItem
	items          []client.FileListItem
	selected       map[uint64]bool
	cursor         int
	loading        bool
	searchInput    textinput.Model
	renameInput    textinput.Model
	mkdirInput     textinput.Model
	uploadInput    textinput.Model
	downloadInput  textinput.Model
	moveCopyTarget textinput.Model
	detail         *client.FileDetail
	sortBy         string
	sortOrder      string
	filterType     string
}

func (m *model) initFiles() {
	if m.files.searchInput.Value() != "" {
		return
	}
	m.files.searchInput = newInput("search keyword", "")
	m.files.renameInput = newInput("new name", "")
	m.files.mkdirInput = newInput("folder name", "")
	m.files.uploadInput = newInput("local file path to upload", "")
	m.files.downloadInput = newInput("local file path to save", "")
	m.files.moveCopyTarget = newInput("target folder id (0 = root)", "0")
	m.files.selected = map[uint64]bool{}
	m.files.sortBy = "name"
	m.files.sortOrder = "asc"
	m.files.filterType = "all"
}

// filesListMsg carries the result of a file list fetch.
type filesListMsg struct {
	items      []client.FileListItem
	breadcrumb []client.BreadcrumbItem
	err        error
}

func (m model) filesReload() tea.Cmd {
	st := m.files
	m.files = st
	m.files.loading = true
	m.SetStatus("loading files…")
	c := m.client
	parentID := m.files.parentID
	return func() tea.Msg {
		list, lerr := c.ListFiles(context.Background(), client.FileListParams{
			ParentID:  parentID,
			Page:      1,
			PageSize:  100,
			SortBy:    m.files.sortBy,
			SortOrder: m.files.sortOrder,
			Type:      m.files.filterType,
		})
		if lerr != nil {
			return filesListMsg{err: lerr}
		}
		var bc client.BreadcrumbResponse
		if parentID > 0 {
			bc, _ = c.GetBreadcrumb(context.Background(), parentID)
		}
		return filesListMsg{items: list.Items, breadcrumb: bc.Path}
	}
}

func (m model) filesUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case filesListMsg:
		m.files.loading = false
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		m.files.items = sortFiles(msg.items, m.files.sortBy, m.files.sortOrder)
		m.files.breadcrumb = msg.breadcrumb
		if m.files.cursor >= len(m.files.items) {
			m.files.cursor = len(m.files.items) - 1
		}
		if m.files.cursor < 0 {
			m.files.cursor = 0
		}
		m.SetStatus(fmt.Sprintf("%d items", len(m.files.items)))
		return m, nil
	case fileOpMsg:
		m.SetStatus(msg.summary)
		return m, m.filesReload()
	case searchResultMsg:
		m.files.mode = filesBrowse
		converted := make([]client.FileListItem, 0, len(msg.items))
		for _, r := range msg.items {
			converted = append(converted, r.FileListItem)
		}
		m.files.items = sortFiles(converted, m.files.sortBy, m.files.sortOrder)
		m.files.breadcrumb = []client.BreadcrumbItem{{ID: 0, Name: "Search Results"}}
		m.SetStatus(fmt.Sprintf("%d search results", len(msg.items)))
		return m, nil
	case uploadProgressMsg:
		if msg.err != nil {
			m.SetError("upload failed: " + msg.err.Error())
			return m, nil
		}
		if msg.done {
			m.SetStatus("upload complete")
			return m, m.filesReload()
		}
		m.SetStatus(fmt.Sprintf("uploading %s (%s / %s)",
			msg.name, util.FormatBytes(msg.uploaded), util.FormatBytes(msg.total)))
		return m, nil
	case tea.KeyMsg:
		// global keys
		if m.active == screenFiles {
			switch msg.String() {
			case "ctrl+x":
				m.active = screenMenu
				return m, nil
			}
		}
		// delegate to dialog/inputs
		switch m.files.mode {
		case filesSearch:
			return m.filesUpdateSearch(msg)
		case filesDialogRename:
			return m.filesUpdateRename(msg)
		case filesDialogMkdir:
			return m.filesUpdateMkdir(msg)
		case filesDialogUpload:
			return m.filesUpdateUpload(msg)
		case filesDialogDownload:
			return m.filesUpdateDownload(msg)
		case filesDialogMoveCopy:
			return m.filesUpdateMoveCopy(msg)
		case filesDialogDetail:
			if msg.String() == "esc" || msg.String() == "enter" {
				m.files.mode = filesBrowse
				m.files.detail = nil
			}
			return m, nil
		}
		// browse-mode keys
		switch msg.String() {
		case "up", "k":
			if m.files.cursor > 0 {
				m.files.cursor--
			}
		case "down", "j":
			if m.files.cursor < len(m.files.items)-1 {
				m.files.cursor++
			}
		case "g", "home":
			m.files.cursor = 0
		case "G", "end":
			m.files.cursor = len(m.files.items) - 1
		case "enter", "l", "right":
			return m.filesOpenCursor()
		case "h", "left", "backspace":
			return m.filesGoUp()
		case " ":
			if len(m.files.items) > 0 {
				id := m.files.items[m.files.cursor].ID
				m.files.selected[id] = !m.files.selected[id]
			}
		case "a":
			// select all visible
			for _, it := range m.files.items {
				m.files.selected[it.ID] = true
			}
		case "A":
			// clear selection
			m.files.selected = map[uint64]bool{}
		case "r":
			if len(m.files.items) > 0 {
				it := m.files.items[m.files.cursor]
				if it.Type == "folder" {
					m.SetError("use R to rename folder")
					return m, nil
				}
				m.files.renameInput.SetValue(it.Name)
				m.files.mode = filesDialogRename
				return m, m.files.renameInput.Focus()
			}
		case "R":
			if len(m.files.items) > 0 {
				it := m.files.items[m.files.cursor]
				if it.Type == "file" {
					m.SetError("use r to rename file")
					return m, nil
				}
				m.files.renameInput.SetValue(it.Name)
				m.files.mode = filesDialogRename
				return m, m.files.renameInput.Focus()
			}
		case "n", "N":
			m.files.mkdirInput.SetValue("")
			m.files.mode = filesDialogMkdir
			return m, m.files.mkdirInput.Focus()
		case "u":
			m.files.uploadInput.SetValue("")
			m.files.mode = filesDialogUpload
			return m, m.files.uploadInput.Focus()
		case "d":
			if len(m.files.items) > 0 {
				it := m.files.items[m.files.cursor]
				if it.Type == "file" {
					m.files.downloadInput.SetValue(it.Name)
					m.files.mode = filesDialogDownload
					return m, m.files.downloadInput.Focus()
				}
				m.SetError("download only available for files")
			}
		case "m":
			m.files.dialogSub = dialogSubMove
			m.files.moveCopyTarget.SetValue("0")
			m.files.mode = filesDialogMoveCopy
			return m, m.files.moveCopyTarget.Focus()
		case "c":
			m.files.dialogSub = dialogSubCopy
			m.files.moveCopyTarget.SetValue("0")
			m.files.mode = filesDialogMoveCopy
			return m, m.files.moveCopyTarget.Focus()
		case "D":
			return m.filesDelete()
		case "i":
			return m.filesShowDetail()
		case "/":
			m.files.searchInput.SetValue("")
			m.files.mode = filesSearch
			return m, m.files.searchInput.Focus()
		case "t":
			// cycle type filter
			m.files.filterType = nextType(m.files.filterType)
			m.SetStatus("filter: " + m.files.filterType)
			return m, m.filesReload()
		case "s":
			// cycle sort by
			m.files.sortBy = nextSort(m.files.sortBy)
			m.SetStatus(fmt.Sprintf("sort: %s %s", m.files.sortBy, m.files.sortOrder))
			return m, m.filesReload()
		case "o":
			// toggle order
			if m.files.sortOrder == "asc" {
				m.files.sortOrder = "desc"
			} else {
				m.files.sortOrder = "asc"
			}
			m.SetStatus(fmt.Sprintf("sort: %s %s", m.files.sortBy, m.files.sortOrder))
			return m, m.filesReload()
		case "?":
			// help is always shown in footer; for now just refresh
			return m, m.filesReload()
		case "esc":
			m.active = screenMenu
		}
	}
	return m, nil
}

func nextType(t string) string {
	switch t {
	case "all":
		return "file"
	case "file":
		return "folder"
	default:
		return "all"
	}
}

func nextSort(s string) string {
	switch s {
	case "name":
		return "size"
	case "size":
		return "created_at"
	case "created_at":
		return "updated_at"
	default:
		return "name"
	}
}

func (m model) filesOpenCursor() (model, tea.Cmd) {
	if len(m.files.items) == 0 {
		return m, nil
	}
	it := m.files.items[m.files.cursor]
	if it.Type != "folder" {
		// for files, show detail
		return m.filesShowDetail()
	}
	// descend
	m.files.history = append(m.files.history, m.files.parentID)
	m.files.parentID = it.ID
	m.files.selected = map[uint64]bool{}
	return m, m.filesReload()
}

func (m model) filesGoUp() (model, tea.Cmd) {
	if len(m.files.history) == 0 {
		return m, nil
	}
	m.files.parentID = m.files.history[len(m.files.history)-1]
	m.files.history = m.files.history[:len(m.files.history)-1]
	m.files.selected = map[uint64]bool{}
	return m, m.filesReload()
}

func (m model) filesShowDetail() (model, tea.Cmd) {
	if len(m.files.items) == 0 {
		return m, nil
	}
	it := m.files.items[m.files.cursor]
	c := m.client
	id := it.ID
	return m, func() tea.Msg {
		if it.Type == "file" {
			d, err := c.GetFileDetail(context.Background(), id)
			if err != nil {
				return fileOpMsg{summary: "", err: err}
			}
			return detailMsg{detail: &d}
		}
		// for folders, synthesize a detail from list item
		d := client.FileDetail{
			ID: it.ID, Name: it.Name, Type: it.Type, CreatedAt: it.CreatedAt, UpdatedAt: it.UpdatedAt,
		}
		return detailMsg{detail: &d}
	}
}

type detailMsg struct{ detail *client.FileDetail }

type fileOpMsg struct {
	summary string
	err     error
}

type searchResultMsg struct {
	items []client.SearchResultItem
	err   error
}

type uploadProgressMsg struct {
	name     string
	uploaded uint64
	total    uint64
	done     bool
	err      error
}

func (m model) filesUpdateSearch(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.files.mode = filesBrowse
			return m, m.filesReload()
		case "enter":
			kw := strings.TrimSpace(m.files.searchInput.Value())
			if kw == "" {
				m.SetError("keyword required")
				return m, nil
			}
			c := m.client
			return m, func() tea.Msg {
				res, err := c.SearchFiles(context.Background(), kw, m.files.filterType, 0, 1, 100)
				if err != nil {
					return searchResultMsg{err: err}
				}
				return searchResultMsg{items: res.Items}
			}
		}
		var cmd tea.Cmd
		m.files.searchInput, cmd = m.files.searchInput.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) filesUpdateRename(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.files.mode = filesBrowse
			return m, nil
		case "enter":
			name := strings.TrimSpace(m.files.renameInput.Value())
			if name == "" {
				m.SetError("name required")
				return m, nil
			}
			it := m.files.items[m.files.cursor]
			c := m.client
			folderID := it.ID
			isFolder := it.Type == "folder"
			return m, func() tea.Msg {
				if isFolder {
					_, err := c.RenameFolder(context.Background(), folderID, name)
					if err != nil {
						return fileOpMsg{err: err}
					}
					return fileOpMsg{summary: "folder renamed"}
				}
				_, err := c.RenameFile(context.Background(), folderID, name)
				if err != nil {
					return fileOpMsg{err: err}
				}
				return fileOpMsg{summary: "file renamed"}
			}
		}
		var cmd tea.Cmd
		m.files.renameInput, cmd = m.files.renameInput.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) filesUpdateMkdir(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.files.mode = filesBrowse
			return m, nil
		case "enter":
			name := strings.TrimSpace(m.files.mkdirInput.Value())
			if name == "" {
				m.SetError("name required")
				return m, nil
			}
			c := m.client
			parentID := m.files.parentID
			return m, func() tea.Msg {
				_, err := c.CreateFolder(context.Background(), name, parentID)
				if err != nil {
					return fileOpMsg{err: err}
				}
				return fileOpMsg{summary: "folder created: " + name}
			}
		}
		var cmd tea.Cmd
		m.files.mkdirInput, cmd = m.files.mkdirInput.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) filesUpdateUpload(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.files.mode = filesBrowse
			return m, nil
		case "enter":
			path := strings.TrimSpace(m.files.uploadInput.Value())
			if path == "" {
				m.SetError("path required")
				return m, nil
			}
			if _, err := os.Stat(path); err != nil {
				m.SetError("invalid path: " + err.Error())
				return m, nil
			}
			c := m.client
			parentID := m.files.parentID
			name := filepath.Base(path)
			m.SetStatus("uploading…")
			m.files.mode = filesBrowse
			return m, func() tea.Msg {
				_, err := c.UploadFile(context.Background(), path, parentID, func(u, t uint64) {})
				if err != nil {
					return uploadProgressMsg{name: name, err: err}
				}
				return uploadProgressMsg{name: name, uploaded: 0, total: 0, done: true}
			}
		}
		var cmd tea.Cmd
		m.files.uploadInput, cmd = m.files.uploadInput.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) filesUpdateDownload(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.files.mode = filesBrowse
			return m, nil
		case "enter":
			dst := strings.TrimSpace(m.files.downloadInput.Value())
			if dst == "" {
				m.SetError("destination path required")
				return m, nil
			}
			it := m.files.items[m.files.cursor]
			if it.Type != "file" {
				m.SetError("can only download files")
				return m, nil
			}
			c := m.client
			id := it.ID
			m.SetStatus("downloading…")
			m.files.mode = filesBrowse
			return m, func() tea.Msg {
				f, err := os.Create(dst)
				if err != nil {
					return fileOpMsg{err: err}
				}
				defer f.Close()
				if _, err := c.DownloadFile(context.Background(), id, f); err != nil {
					return fileOpMsg{err: err}
				}
				return fileOpMsg{summary: "downloaded to " + dst}
			}
		}
		var cmd tea.Cmd
		m.files.downloadInput, cmd = m.files.downloadInput.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) filesUpdateMoveCopy(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.files.mode = filesBrowse
			return m, nil
		case "enter":
			targetStr := strings.TrimSpace(m.files.moveCopyTarget.Value())
			targetID := uint64(0)
			if _, err := fmt.Sscanf(targetStr, "%d", &targetID); err != nil {
				m.SetError("invalid folder id")
				return m, nil
			}
			var fileIDs, folderIDs []uint64
			for _, it := range m.files.items {
				if m.files.selected[it.ID] {
					if it.Type == "file" {
						fileIDs = append(fileIDs, it.ID)
					} else {
						folderIDs = append(folderIDs, it.ID)
					}
				}
			}
			if len(fileIDs) == 0 && len(folderIDs) == 0 && len(m.files.items) > 0 {
				it := m.files.items[m.files.cursor]
				if it.Type == "file" {
					fileIDs = append(fileIDs, it.ID)
				} else {
					folderIDs = append(folderIDs, it.ID)
				}
			}
			if len(fileIDs) == 0 && len(folderIDs) == 0 {
				m.SetError("nothing selected")
				return m, nil
			}
			c := m.client
			mode := m.files.dialogSub
			m.files.mode = filesBrowse
			return m, func() tea.Msg {
				if mode == dialogSubMove {
					res, err := c.MoveItems(context.Background(), fileIDs, folderIDs, targetID)
					if err != nil {
						return fileOpMsg{err: err}
					}
					return fileOpMsg{summary: fmt.Sprintf("moved %d items", res.MovedCount)}
				}
				res, err := c.CopyItems(context.Background(), fileIDs, folderIDs, targetID)
				if err != nil {
					return fileOpMsg{err: err}
				}
				return fileOpMsg{summary: fmt.Sprintf("copied %d items", res.CopiedCount)}
			}
		}
		var cmd tea.Cmd
		m.files.moveCopyTarget, cmd = m.files.moveCopyTarget.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) filesDelete() (model, tea.Cmd) {
	var fileIDs, folderIDs []uint64
	for _, it := range m.files.items {
		if m.files.selected[it.ID] {
			if it.Type == "file" {
				fileIDs = append(fileIDs, it.ID)
			} else {
				folderIDs = append(folderIDs, it.ID)
			}
		}
	}
	if len(fileIDs) == 0 && len(folderIDs) == 0 && len(m.files.items) > 0 {
		it := m.files.items[m.files.cursor]
		if it.Type == "file" {
			fileIDs = append(fileIDs, it.ID)
		} else {
			folderIDs = append(folderIDs, it.ID)
		}
	}
	if len(fileIDs) == 0 && len(folderIDs) == 0 {
		m.SetError("nothing selected")
		return m, nil
	}
	c := m.client
	return m, func() tea.Msg {
		res, err := c.DeleteItems(context.Background(), fileIDs, folderIDs)
		if err != nil {
			return fileOpMsg{err: err}
		}
		return fileOpMsg{summary: fmt.Sprintf("deleted %d items (moved to trash)", res.DeletedCount)}
	}
}

func (m model) filesPassResize(msg tea.WindowSizeMsg) {
	w := msg.Width - 4
	if w < 20 {
		w = 20
	}
	m.files.searchInput.Width = w
	m.files.renameInput.Width = w
	m.files.mkdirInput.Width = w
	m.files.uploadInput.Width = w
	m.files.downloadInput.Width = w
	m.files.moveCopyTarget.Width = w
}

func (m model) filesView() string {
	t := m.theme
	st := m.files

	switch st.mode {
	case filesSearch:
		return m.renderInputDialog("Search", &st.searchInput, "Enter: search  |  Esc: cancel")
	case filesDialogRename:
		title := "Rename File"
		if len(st.items) > 0 && st.items[st.cursor].Type == "folder" {
			title = "Rename Folder"
		}
		return m.renderInputDialog(title, &st.renameInput, "Enter: rename  |  Esc: cancel")
	case filesDialogMkdir:
		return m.renderInputDialog("New Folder", &st.mkdirInput, "Enter: create  |  Esc: cancel")
	case filesDialogUpload:
		return m.renderInputDialog("Upload Local File", &st.uploadInput, "Enter: upload  |  Esc: cancel")
	case filesDialogDownload:
		return m.renderInputDialog("Download To Local File", &st.downloadInput, "Enter: download  |  Esc: cancel")
	case filesDialogMoveCopy:
		title := "Move Items to Folder"
		if st.dialogSub == dialogSubCopy {
			title = "Copy Items to Folder"
		}
		return m.renderInputDialog(title, &st.moveCopyTarget, "Enter: confirm  |  Esc: cancel")
	case filesDialogDetail:
		return m.renderFileDetail()
	}

	// Browse mode
	var rows []string
	rows = append(rows, t.Banner.Render("◼ Files"))
	bc := renderBreadcrumb(st.breadcrumb, t)
	rows = append(rows, bc)
	rows = append(rows, "")
	rows = append(rows, m.renderFileTable())

	rows = append(rows, "")
	rows = append(rows, t.JoinHelp([][2]string{
		{"↑↓/jk", "navigate"},
		{"→/l", "open"},
		{"←/h", "up"},
		{"␣", "select"},
		{"a/A", "sel all/none"},
		{"n", "mkdir"},
		{"u", "upload"},
		{"d", "download"},
		{"r/R", "rename"},
		{"m", "move"},
		{"c", "copy"},
		{"D", "delete"},
		{"i", "detail"},
		{"/", "search"},
		{"s/o", "sort/order"},
		{"t", "type filter: " + st.filterType},
		{"Ctrl+X", "menu"},
	}))
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}

func (m model) renderFileDetail() string {
	t := m.theme
	d := m.files.detail
	if d == nil {
		return ""
	}
	var rows []string
	rows = append(rows, t.Banner.Render("◼ File Detail"))
	rows = append(rows, "")
	rows = append(rows, t.Label.Render("ID")+t.Value.Render(fmt.Sprintf("%d", d.ID)))
	rows = append(rows, t.Label.Render("Name")+t.Value.Render(d.Name))
	rows = append(rows, t.Label.Render("Type")+t.Value.Render(d.Type))
	if d.Type == "file" {
		rows = append(rows, t.Label.Render("Size")+t.Value.Render(util.FormatBytes(uint64(d.Size))))
		rows = append(rows, t.Label.Render("Hash")+t.Value.Render(d.Hash))
		rows = append(rows, t.Label.Render("MIME")+t.Value.Render(d.MimeType))
	}
	rows = append(rows, t.Label.Render("Parent")+t.Value.Render(fmt.Sprintf("%d", d.ParentID)))
	rows = append(rows, t.Label.Render("Path")+t.Value.Render(d.Path))
	rows = append(rows, t.Label.Render("Created")+t.Value.Render(util.FormatTime(d.CreatedAt)))
	rows = append(rows, t.Label.Render("Updated")+t.Value.Render(util.FormatTime(d.UpdatedAt)))
	rows = append(rows, "")
	rows = append(rows, t.Muted.Render("Esc/Enter: back"))
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}

func renderBreadcrumb(bc []client.BreadcrumbItem, t Theme) string {
	if len(bc) == 0 {
		return t.Muted.Render("/ (root)")
	}
	parts := []string{t.Folder.Render("/")}
	for _, c := range bc {
		parts = append(parts, t.Folder.Render(c.Name))
	}
	return lipgloss.JoinHorizontal(lipgloss.Left, parts...)
}

func (m model) renderFileTable() string {
	t := m.theme
	if len(m.files.items) == 0 {
		return t.Muted.Render("(no items)")
	}
	header := strings.Join([]string{
		padRight("Sel", 4),
		padRight("Type", 7),
		padRight("Name", 40),
		padRight("Size/Items", 14),
		padRight("Modified", 20),
	}, "  ")
	rows := []string{t.ListHeader.Render(header)}
	for i, it := range m.files.items {
		sel := " "
		if m.files.selected[it.ID] {
			sel = "✓"
		}
		typeMark := "📄"
		sizeStr := util.FormatBytes(it.Size)
		if it.Type == "folder" {
			typeMark = "📁"
			sizeStr = fmt.Sprintf("%d items", it.ItemCount)
		}
		line := strings.Join([]string{
			padRight(sel, 4),
			padRight(typeMark, 7),
			padRight(util.Truncate(it.Name, 40), 40),
			padRight(sizeStr, 14),
			padRight(util.FormatTime(it.UpdatedAt), 20),
		}, "  ")
		if i == m.files.cursor {
			line = lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#0B1220")).Background(lipgloss.Color("#7DD3FC")).Render(line)
		}
		rows = append(rows, line)
	}
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}

func (m model) renderInputDialog(title string, input interface{}, help string) string {
	t := m.theme
	ti, _ := input.(interface{ View() string })
	rows := []string{
		t.Banner.Render("◼ " + title),
		"",
		ti.View(),
		"",
		t.Muted.Render(help),
	}
	dialog := t.Dialog.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))
	return lipgloss.Place(m.width, m.height-lipgloss.Height(m.renderHeader())-lipgloss.Height(m.renderFooter()),
		lipgloss.Center, lipgloss.Center, dialog)
}

func sortFiles(items []client.FileListItem, by, order string) []client.FileListItem {
	out := make([]client.FileListItem, len(items))
	copy(out, items)
	mul := 1
	if order == "desc" {
		mul = -1
	}
	sort.SliceStable(out, func(i, j int) bool {
		a, b := out[i], out[j]
		if a.Type != b.Type {
			// folders first
			return a.Type == "folder"
		}
		switch by {
		case "size":
			return (a.Size < b.Size) == (mul > 0)
		case "created_at":
			return (a.CreatedAt < b.CreatedAt) == (mul > 0)
		case "updated_at":
			return (a.UpdatedAt < b.UpdatedAt) == (mul > 0)
		default:
			return (a.Name < b.Name) == (mul > 0)
		}
	})
	return out
}

func padRight(s string, n int) string {
	r := []rune(s)
	if len(r) >= n {
		return string(r[:n])
	}
	return s + strings.Repeat(" ", n-len(r))
}
