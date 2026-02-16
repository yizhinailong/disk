// Package app implements the main application model for the TUI client.
// It manages page routing, global state, and coordinates between pages.
package app

import (
	tea "github.com/charmbracelet/bubbletea"

	"github.com/liufeng/disk/ui/tui/internal/api"
	"github.com/liufeng/disk/ui/tui/internal/config"
	"github.com/liufeng/disk/ui/tui/internal/store"
	"github.com/liufeng/disk/ui/tui/internal/ui/pages/files"
	"github.com/liufeng/disk/ui/tui/internal/ui/pages/login"
)

// =============================================================================
// 页面状态 (Page State)
// =============================================================================

// pageState represents the current active page.
type pageState int

const (
	pageLogin pageState = iota
	pageFiles
	pageTrash
	pageShare
	pageHelp
)

// =============================================================================
// 消息定义 (Message Definitions)
// =============================================================================

// QuitMsg is dispatched when the application should quit.
type QuitMsg struct{}

// =============================================================================
// Model 定义 (Model Definition)
// =============================================================================

// Model is the main application model that manages page routing and global state.
type Model struct {
	config     *config.Config
	client     *api.Client
	tokenStore *store.TokenStore

	// 页面状态
	currentPage pageState

	// 子页面
	loginPage login.Model
	filesPage files.Model

	// 布局
	width  int
	height int
}

// New creates a new application model.
func New(cfg *config.Config, password string) Model {
	// 创建 Token 存储
	tokenStore := store.NewTokenStore(cfg.GetTokenPath(), password)

	// 创建 API 客户端
	client := api.NewClient(cfg, tokenStore)

	// 尝试加载已保存的 Token
	_ = client.LoadToken()

	return Model{
		config:     cfg,
		client:     client,
		tokenStore: tokenStore,
		loginPage:  login.New(cfg, client),
		filesPage:  files.New(client),
	}
}

// Init initializes the application.
func (m Model) Init() tea.Cmd {
	// 检查是否已登录
	if m.client.IsLoggedIn() {
		m.currentPage = pageFiles
		return m.filesPage.Init()
	}

	m.currentPage = pageLogin
	return m.loginPage.Init()
}

// Update handles messages and updates the model.
func (m Model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		m.loginPage.SetSize(msg.Width, msg.Height)
		// files page handles size through its own Update
		return m, nil

	case tea.KeyMsg:
		// 全局快捷键
		switch msg.String() {
		case "ctrl+c":
			return m, tea.Quit
		}

	case login.LoginSuccessMsg:
		// 登录成功，切换到文件页面
		m.currentPage = pageFiles
		// 保存 Token
		_ = m.client.SaveToken()
		return m, m.filesPage.Init()

	case QuitMsg:
		return m, tea.Quit
	}

	// 根据当前页面分发消息
	switch m.currentPage {
	case pageLogin:
		lp, cmd := m.loginPage.Update(msg)
		m.loginPage = lp
		return m, cmd

	case pageFiles:
		fp, cmd := m.filesPage.Update(msg)
		m.filesPage = fp
		return m, cmd
	}

	return m, nil
}

// View renders the current page.
func (m Model) View() string {
	switch m.currentPage {
	case pageLogin:
		return m.loginPage.View()
	case pageFiles:
		return m.filesPage.View()
	default:
		return ""
	}
}

// =============================================================================
// 辅助方法 (Helper Methods)
// =============================================================================

// SetSize sets the application dimensions.
func (m *Model) SetSize(width, height int) {
	m.width = width
	m.height = height
	m.loginPage.SetSize(width, height)
}

// CurrentPage returns the current page state.
func (m *Model) CurrentPage() pageState {
	return m.currentPage
}

// IsLoggedIn returns whether the user is logged in.
func (m *Model) IsLoggedIn() bool {
	return m.client.IsLoggedIn()
}

// Logout logs out the user and returns to login page.
func (m *Model) Logout() {
	m.client.ClearToken()
	_ = m.tokenStore.Delete()
	m.currentPage = pageLogin
}
