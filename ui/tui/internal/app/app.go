// Package app 应用主逻辑
//
// 管理 TUI 应用的生命周期、页面路由和全局状态。
// 协调登录页面、文件页面等子模块之间的交互。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
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

// pageState 当前活跃页面状态
type pageState int

// 页面状态常量
const (
	pageLogin pageState = iota // 登录页面
	pageFiles                  // 文件列表页面
	pageTrash                  // 回收站页面
	pageShare                  // 分享管理页面
	pageHelp                   // 帮助页面
)

// =============================================================================
// 消息定义 (Message Definitions)
// =============================================================================

// QuitMsg 退出应用消息
type QuitMsg struct{}

// =============================================================================
// Model 定义 (Model Definition)
// =============================================================================

// Model TUI 应用主模型
//
// 管理 Bubble Tea 应用的生命周期和状态，协调各子页面。
type Model struct {
	config     *config.Config    // 配置对象
	client     *api.Client       // API 客户端
	tokenStore *store.TokenStore // Token 存储器

	// 页面状态
	currentPage pageState // 当前活跃页面

	// 子页面
	loginPage login.Model // 登录页面模型
	filesPage files.Model // 文件列表页面模型

	// 布局
	width  int // 窗口宽度
	height int // 窗口高度
}

// New 创建应用模型
//
// 参数:
//   - cfg: 配置对象
//   - password: Token 加密密码
//
// 返回:
//   - Model: 初始化后的应用模型
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

// Init 初始化应用
//
// 实现 bubbletea.Model 接口。检查登录状态并启动相应页面。
func (m Model) Init() tea.Cmd {
	// 检查是否已登录
	if m.client.IsLoggedIn() {
		m.currentPage = pageFiles
		return m.filesPage.Init()
	}

	m.currentPage = pageLogin
	return m.loginPage.Init()
}

// Update 处理消息并更新模型
//
// 实现 bubbletea.Model 接口。处理全局快捷键和页面切换。
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

// View 渲染当前页面
//
// 实现 bubbletea.Model 接口。根据当前页面状态返回对应视图。
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

// SetSize 设置应用尺寸
func (m *Model) SetSize(width, height int) {
	m.width = width
	m.height = height
	m.loginPage.SetSize(width, height)
}

// CurrentPage 返回当前页面状态
func (m *Model) CurrentPage() pageState {
	return m.currentPage
}

// IsLoggedIn 返回用户是否已登录
func (m *Model) IsLoggedIn() bool {
	return m.client.IsLoggedIn()
}

// Logout 登出用户并返回登录页面
func (m *Model) Logout() {
	m.client.ClearToken()
	_ = m.tokenStore.Delete()
	m.currentPage = pageLogin
}
