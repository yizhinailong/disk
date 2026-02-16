// Package breadcrumb 面包屑导航组件
package breadcrumb

import (
	"strings"

	tea "github.com/charmbracelet/bubbletea"

	"github.com/liufeng/disk/ui/tui/internal/models"
	"github.com/liufeng/disk/ui/tui/internal/ui/styles"
)

// Model 面包屑模型
type Model struct {
	path  []models.BreadcrumbItem
	width int
}

// New 创建面包屑
func New() Model {
	return Model{
		path: []models.BreadcrumbItem{{ID: 0, Name: "根目录"}},
	}
}

// SetPath 设置路径
func (m *Model) SetPath(path []models.BreadcrumbItem) {
	if len(path) == 0 {
		m.path = []models.BreadcrumbItem{{ID: 0, Name: "根目录"}}
	} else {
		m.path = path
	}
}

// Path 获取当前路径
func (m *Model) Path() []models.BreadcrumbItem {
	return m.path
}

// SetWidth 设置宽度
func (m *Model) SetWidth(width int) {
	m.width = width
}

// Width 获取宽度
func (m *Model) Width() int {
	return m.width
}

// CurrentFolder 获取当前文件夹名称
func (m *Model) CurrentFolder() string {
	if len(m.path) == 0 {
		return "根目录"
	}
	return m.path[len(m.path)-1].Name
}

// CurrentFolderID 获取当前文件夹 ID
func (m *Model) CurrentFolderID() uint64 {
	if len(m.path) == 0 {
		return 0
	}
	return m.path[len(m.path)-1].ID
}

// ParentID 获取父文件夹 ID
func (m *Model) ParentID() uint64 {
	if len(m.path) <= 1 {
		return 0
	}
	return m.path[len(m.path)-2].ID
}

// NavigateTo 导航到指定路径项
func (m *Model) NavigateTo(index int) {
	if index >= 0 && index < len(m.path) {
		m.path = m.path[:index+1]
	}
}

// Push 添加路径项
func (m *Model) Push(item models.BreadcrumbItem) {
	m.path = append(m.path, item)
}

// Pop 返回上级
func (m *Model) Pop() bool {
	if len(m.path) > 1 {
		m.path = m.path[:len(m.path)-1]
		return true
	}
	return false
}

// Reset 重置到根目录
func (m *Model) Reset() {
	m.path = []models.BreadcrumbItem{{ID: 0, Name: "根目录"}}
}

// Depth 获取路径深度
func (m *Model) Depth() int {
	return len(m.path)
}

// Init 初始化
func (m Model) Init() tea.Cmd {
	return nil
}

// Update 更新
func (m Model) Update(msg tea.Msg) (Model, tea.Cmd) {
	return m, nil
}

// View 渲染
func (m Model) View() string {
	if len(m.path) == 0 {
		return ""
	}

	parts := make([]string, len(m.path))
	for i, item := range m.path {
		if i == len(m.path)-1 {
			// 最后一项高亮
			parts[i] = styles.BreadcrumbCurrentStyle.Render(item.Name)
		} else {
			parts[i] = styles.BreadcrumbStyle.Render(item.Name)
		}
	}

	sep := styles.DividerStyle.Render(" > ")
	line := "路径: " + strings.Join(parts, sep)

	// 截断超长路径
	if m.width > 0 && len(line) > m.width {
		// 保留最后两级
		if len(m.path) > 2 {
			last := styles.BreadcrumbCurrentStyle.Render(m.path[len(m.path)-1].Name)
			secondLast := styles.BreadcrumbStyle.Render(m.path[len(m.path)-2].Name)
			ellipsis := styles.MutedStyle.Render("...")
			line = "路径: " + ellipsis + " " + sep + secondLast + sep + last
		} else if len(m.path) == 2 {
			// 只有两级，显示完整
			first := styles.BreadcrumbStyle.Render(m.path[0].Name)
			last := styles.BreadcrumbCurrentStyle.Render(m.path[1].Name)
			line = "路径: " + first + sep + last
		}
	}

	return line
}

// Items 获取所有路径项（用于点击导航）
func (m *Model) Items() []models.BreadcrumbItem {
	return m.path
}

// IsAtRoot 是否在根目录
func (m *Model) IsAtRoot() bool {
	return len(m.path) == 1
}
