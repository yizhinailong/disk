package tui

import (
	"context"
	"fmt"
	"strings"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/disk-tui/internal/client"
	"github.com/disk-tui/internal/util"
)

type profileMode int

const (
	profileShow profileMode = iota
	profileEditNickname
	profileEditAvatar
	profileChangePassword
)

type profileState struct {
	mode          profileMode
	user          *client.User
	storage       *client.StorageResponse
	nicknameInput textinput.Model
	avatarInput   textinput.Model
	oldPassword   textinput.Model
	newPassword   textinput.Model
}

type profileMsg struct {
	user    *client.User
	storage *client.StorageResponse
	summary string
	err     error
}

func (m *model) initProfile() {
	if m.profile.nicknameInput.Value() != "" {
		return
	}
	m.profile.nicknameInput = newInput("nickname", "")
	m.profile.avatarInput = newInput("avatar (URL or text)", "")
	m.profile.oldPassword = newInput("old password", "")
	m.profile.newPassword = newInput("new password", "")
	m.profile.oldPassword.EchoMode = textinput.EchoPassword
	m.profile.newPassword.EchoMode = textinput.EchoPassword
}

func (m model) profileReload() tea.Cmd {
	c := m.client
	return func() tea.Msg {
		u, err := c.GetProfile(context.Background())
		if err != nil {
			return profileMsg{err: err}
		}
		s, serr := c.GetStorage(context.Background())
		var sptr *client.StorageResponse
		if serr == nil {
			sptr = &s
		}
		ucopy := u
		return profileMsg{user: &ucopy, storage: sptr}
	}
}

func (m model) profileUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case profileMsg:
		if msg.err != nil {
			m.SetError(msg.err.Error())
			return m, nil
		}
		if msg.user != nil {
			m.profile.user = msg.user
		}
		if msg.storage != nil {
			m.profile.storage = msg.storage
		}
		if msg.summary != "" {
			m.SetStatus(msg.summary)
		} else {
			m.SetStatus("profile loaded")
		}
		return m, nil
	case tea.KeyMsg:
		switch m.profile.mode {
		case profileEditNickname:
			return m.profileEditNicknameUpdate(msg)
		case profileEditAvatar:
			return m.profileEditAvatarUpdate(msg)
		case profileChangePassword:
			return m.profilePasswordUpdate(msg)
		}
		switch msg.String() {
		case "ctrl+x", "esc":
			m.active = screenMenu
		case "n":
			m.profile.nicknameInput.SetValue("")
			if m.profile.user != nil {
				m.profile.nicknameInput.SetValue(m.profile.user.Nickname)
			}
			m.profile.mode = profileEditNickname
			return m, m.profile.nicknameInput.Focus()
		case "a":
			m.profile.avatarInput.SetValue("")
			if m.profile.user != nil {
				m.profile.avatarInput.SetValue(m.profile.user.Avatar)
			}
			m.profile.mode = profileEditAvatar
			return m, m.profile.avatarInput.Focus()
		case "p":
			m.profile.oldPassword.SetValue("")
			m.profile.newPassword.SetValue("")
			m.profile.mode = profileChangePassword
			return m, m.profile.oldPassword.Focus()
		case "r":
			return m, m.profileReload()
		}
	}
	return m, nil
}

func (m model) profileEditNicknameUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.profile.mode = profileShow
			return m, nil
		case "enter":
			val := strings.TrimSpace(m.profile.nicknameInput.Value())
			if val == "" {
				m.SetError("nickname required")
				return m, nil
			}
			c := m.client
			m.profile.mode = profileShow
			return m, func() tea.Msg {
				u, err := c.UpdateProfile(context.Background(), val, "")
				if err != nil {
					return profileMsg{err: err}
				}
				ucopy := u
				return profileMsg{user: &ucopy, summary: "nickname updated"}
			}
		}
		var cmd tea.Cmd
		m.profile.nicknameInput, cmd = m.profile.nicknameInput.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) profileEditAvatarUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.profile.mode = profileShow
			return m, nil
		case "enter":
			val := strings.TrimSpace(m.profile.avatarInput.Value())
			if val == "" {
				m.SetError("avatar required")
				return m, nil
			}
			c := m.client
			m.profile.mode = profileShow
			return m, func() tea.Msg {
				u, err := c.UpdateProfile(context.Background(), "", val)
				if err != nil {
					return profileMsg{err: err}
				}
				ucopy := u
				return profileMsg{user: &ucopy, summary: "avatar updated"}
			}
		}
		var cmd tea.Cmd
		m.profile.avatarInput, cmd = m.profile.avatarInput.Update(msg)
		return m, cmd
	}
	return m, nil
}

func (m model) profilePasswordUpdate(msg tea.Msg) (model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "tab":
			if m.profile.oldPassword.Focused() {
				m.profile.oldPassword.Blur()
				return m, m.profile.newPassword.Focus()
			}
			m.profile.newPassword.Blur()
			return m, m.profile.oldPassword.Focus()
		case "esc":
			m.profile.mode = profileShow
			return m, nil
		case "enter":
			old := m.profile.oldPassword.Value()
			new := m.profile.newPassword.Value()
			if old == "" || new == "" {
				m.SetError("both fields required")
				return m, nil
			}
			c := m.client
			m.profile.mode = profileShow
			return m, func() tea.Msg {
				err := c.ChangePassword(context.Background(), old, new)
				if err != nil {
					return profileMsg{err: err}
				}
				return profileMsg{summary: "password changed"}
			}
		}
		var cmd tea.Cmd
		if m.profile.oldPassword.Focused() {
			m.profile.oldPassword, cmd = m.profile.oldPassword.Update(msg)
		} else {
			m.profile.newPassword, cmd = m.profile.newPassword.Update(msg)
		}
		return m, cmd
	}
	return m, nil
}

func (m model) profilePassResize(msg tea.WindowSizeMsg) {
	w := msg.Width - 6
	if w < 20 {
		w = 20
	}
	for _, in := range []*textinput.Model{
		&m.profile.nicknameInput, &m.profile.avatarInput,
		&m.profile.oldPassword, &m.profile.newPassword,
	} {
		in.Width = w
	}
}

func (m model) profileView() string {
	t := m.theme
	switch m.profile.mode {
	case profileEditNickname:
		return m.renderInputDialog("Update Nickname", &m.profile.nicknameInput, "Enter: save  |  Esc: cancel")
	case profileEditAvatar:
		return m.renderInputDialog("Update Avatar", &m.profile.avatarInput, "Enter: save  |  Esc: cancel")
	case profileChangePassword:
		var rows []string
		rows = append(rows, t.Banner.Render("◼ Change Password"))
		rows = append(rows, "")
		rows = append(rows, t.Label.Render("Old Password")+m.profile.oldPassword.View())
		rows = append(rows, t.Label.Render("New Password")+m.profile.newPassword.View())
		rows = append(rows, "")
		rows = append(rows, t.Muted.Render("Tab: switch  Enter: submit  Esc: cancel"))
		dialog := t.Dialog.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))
		return lipgloss.Place(m.width, m.height-lipgloss.Height(m.renderHeader())-lipgloss.Height(m.renderFooter()),
			lipgloss.Center, lipgloss.Center, dialog)
	}

	var rows []string
	rows = append(rows, t.Banner.Render("◼ Profile"))
	rows = append(rows, "")
	u := m.profile.user
	if u == nil {
		rows = append(rows, t.Muted.Render("(loading…)"))
	} else {
		rows = append(rows, t.Label.Render("ID")+t.Value.Render(fmt.Sprintf("%d", u.ID)))
		rows = append(rows, t.Label.Render("Username")+t.Value.Render(u.Username))
		rows = append(rows, t.Label.Render("Email")+t.Value.Render(u.Email))
		rows = append(rows, t.Label.Render("Nickname")+t.Value.Render(u.Nickname))
		rows = append(rows, t.Label.Render("Avatar")+t.Value.Render(util.Truncate(u.Avatar, 60)))
		rows = append(rows, t.Label.Render("Role")+t.Value.Render(util.RoleName(u.Role)))
		rows = append(rows, t.Label.Render("Files")+t.Value.Render(fmt.Sprintf("%d", u.FileCount)))
		rows = append(rows, t.Label.Render("Folders")+t.Value.Render(fmt.Sprintf("%d", u.FolderCount)))
		rows = append(rows, t.Label.Render("Created")+t.Value.Render(util.FormatTime(u.CreatedAt)))
		rows = append(rows, t.Label.Render("Updated")+t.Value.Render(util.FormatTime(u.UpdatedAt)))
	}
	if m.profile.storage != nil {
		s := m.profile.storage
		rows = append(rows, "")
		rows = append(rows, t.Accent.Render("Storage:"))
		rows = append(rows, t.Label.Render("Used")+t.Value.Render(util.FormatBytes(s.Used)+" / "+util.FormatBytes(s.Quota)))
		rows = append(rows, t.Label.Render("Usage")+t.Value.Render(fmt.Sprintf("%.1f%%", s.Percentage)))
	}
	rows = append(rows, "")
	rows = append(rows, t.JoinHelp([][2]string{
		{"n", "edit nickname"}, {"a", "edit avatar"},
		{"p", "change password"}, {"r", "refresh"},
		{"Ctrl+X", "menu"},
	}))
	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}
