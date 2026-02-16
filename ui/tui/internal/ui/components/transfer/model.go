// Package transfer 传输进度组件
package transfer

import (
	"fmt"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	"github.com/liufeng/disk/ui/tui/internal/ui/styles"
)

// TaskStatus 任务状态
type TaskStatus string

const (
	TaskStatusPending   TaskStatus = "pending"
	TaskStatusRunning   TaskStatus = "running"
	TaskStatusPaused    TaskStatus = "paused"
	TaskStatusCompleted TaskStatus = "completed"
	TaskStatusError     TaskStatus = "error"
)

// TaskType 任务类型
type TaskType string

const (
	TaskUpload   TaskType = "上传"
	TaskDownload TaskType = "下载"
)

// Task 传输任务
type Task struct {
	ID        string
	Type      TaskType
	Filename  string
	Progress  float64
	Speed     string
	Status    TaskStatus
	TotalSize uint64
	BytesDone uint64
	Error     string
}

// Model 传输进度模型
type Model struct {
	tasks       []Task
	width       int
	maxVisible  int
	showAll     bool
	focused     bool
	focusedTask int
}

// New 创建传输进度组件
func New() Model {
	return Model{
		tasks:      []Task{},
		maxVisible: 3,
	}
}

// SetWidth 设置宽度
func (m *Model) SetWidth(width int) {
	m.width = width
}

// Width 获取宽度
func (m *Model) Width() int {
	return m.width
}

// SetTasks 设置任务列表
func (m *Model) SetTasks(tasks []Task) {
	m.tasks = tasks
}

// Tasks 获取任务列表
func (m *Model) Tasks() []Task {
	return m.tasks
}

// AddTask 添加任务
func (m *Model) AddTask(task Task) {
	m.tasks = append(m.tasks, task)
}

// UpdateTask 更新任务进度
func (m *Model) UpdateTask(id string, progress float64, speed string) {
	for i, t := range m.tasks {
		if t.ID == id {
			m.tasks[i].Progress = progress
			m.tasks[i].Speed = speed
			break
		}
	}
}

// SetTaskStatus 设置任务状态
func (m *Model) SetTaskStatus(id string, status TaskStatus) {
	for i, t := range m.tasks {
		if t.ID == id {
			m.tasks[i].Status = status
			break
		}
	}
}

// SetTaskError 设置任务错误
func (m *Model) SetTaskError(id string, err string) {
	for i, t := range m.tasks {
		if t.ID == id {
			m.tasks[i].Status = TaskStatusError
			m.tasks[i].Error = err
			break
		}
	}
}

// RemoveTask 移除任务
func (m *Model) RemoveTask(id string) {
	for i, t := range m.tasks {
		if t.ID == id {
			m.tasks = append(m.tasks[:i], m.tasks[i+1:]...)
			break
		}
	}
}

// ClearCompleted 清除已完成任务
func (m *Model) ClearCompleted() {
	var active []Task
	for _, t := range m.tasks {
		if t.Status != TaskStatusCompleted {
			active = append(active, t)
		}
	}
	m.tasks = active
}

// ClearAll 清除所有任务
func (m *Model) ClearAll() {
	m.tasks = []Task{}
}

// ActiveCount 获取活跃任务数量
func (m *Model) ActiveCount() int {
	count := 0
	for _, t := range m.tasks {
		if t.Status == TaskStatusRunning || t.Status == TaskStatusPending {
			count++
		}
	}
	return count
}

// HasActiveTasks 检查是否有活跃任务
func (m *Model) HasActiveTasks() bool {
	return m.ActiveCount() > 0
}

// Focus 获取焦点
func (m *Model) Focus() {
	m.focused = true
}

// Blur 失去焦点
func (m *Model) Blur() {
	m.focused = false
}

// Focused 是否有焦点
func (m *Model) Focused() bool {
	return m.focused
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
	if len(m.tasks) == 0 {
		return ""
	}

	var lines []string

	// 计算要显示的任务数量
	count := len(m.tasks)
	if count > m.maxVisible && !m.showAll {
		count = m.maxVisible
	}

	for i := 0; i < count; i++ {
		task := m.tasks[i]
		line := m.renderTask(task, i == m.focusedTask)
		lines = append(lines, line)
	}

	// 超出数量提示
	if len(m.tasks) > m.maxVisible && !m.showAll {
		extra := fmt.Sprintf("  +%d 个任务...", len(m.tasks)-m.maxVisible)
		lines = append(lines, styles.MutedStyle.Render(extra))
	}

	return strings.Join(lines, "\n")
}

// renderTask 渲染单个任务
func (m *Model) renderTask(task Task, focused bool) string {
	// 进度条宽度
	barWidth := 20
	if m.width > 60 {
		barWidth = (m.width - 40) / 2
	}
	if barWidth < 10 {
		barWidth = 10
	}

	// 进度条
	var progressStyle lipgloss.Style
	switch task.Status {
	case TaskStatusError:
		progressStyle = styles.ProgressErrorStyle
	case TaskStatusPaused:
		progressStyle = styles.ProgressPausedStyle
	default:
		progressStyle = styles.ProgressFilledStyle
	}

	progressBar := styles.MakeProgressBar(barWidth, task.Progress, styles.ProgressFilled, styles.ProgressEmpty)
	progressBar = progressStyle.Render(progressBar)

	// 图标
	icon := styles.IconUploading
	if task.Type == TaskDownload {
		icon = styles.IconDownloading
	}

	// 状态指示
	var statusIcon string
	switch task.Status {
	case TaskStatusCompleted:
		statusIcon = " " + styles.IconSuccess
	case TaskStatusError:
		statusIcon = " " + styles.IconError
	case TaskStatusPaused:
		statusIcon = " (已暂停)"
	}

	// 截断文件名
	filename := truncateFilename(task.Filename, 20)

	// 进度百分比
	percentStr := fmt.Sprintf("%3.0f%%", task.Progress)

	// 速度
	speedStr := ""
	if task.Speed != "" && task.Status == TaskStatusRunning {
		speedStr = " │ 速度: " + task.Speed
	}

	// 构建行
	line := fmt.Sprintf("%s: %s %s %s%s%s",
		task.Type,
		icon+" "+filename,
		progressBar,
		percentStr,
		statusIcon,
		speedStr,
	)

	// 焦点样式
	if focused && m.focused {
		line = styles.SelectedStyle.Render(line)
	}

	return line
}

// truncateFilename 截断文件名
func truncateFilename(name string, maxLen int) string {
	if len(name) <= maxLen {
		return name
	}
	if maxLen <= 3 {
		return name[:maxLen]
	}
	return name[:maxLen-3] + "..."
}

// SetMaxVisible 设置最大可见任务数
func (m *Model) SetMaxVisible(max int) {
	m.maxVisible = max
}

// SetShowAll 设置是否显示所有任务
func (m *Model) SetShowAll(show bool) {
	m.showAll = show
}

// NextTask 切换到下一个任务
func (m *Model) NextTask() {
	if len(m.tasks) == 0 {
		return
	}
	m.focusedTask = (m.focusedTask + 1) % len(m.tasks)
}

// PrevTask 切换到上一个任务
func (m *Model) PrevTask() {
	if len(m.tasks) == 0 {
		return
	}
	m.focusedTask--
	if m.focusedTask < 0 {
		m.focusedTask = len(m.tasks) - 1
	}
}

// FocusedTask 获取当前焦点任务
func (m *Model) FocusedTask() *Task {
	if m.focusedTask >= 0 && m.focusedTask < len(m.tasks) {
		return &m.tasks[m.focusedTask]
	}
	return nil
}

// RenderEmpty 渲染无任务状态
func (m *Model) RenderEmpty() string {
	emptyStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("#6c6c6c")).
		Align(lipgloss.Center)

	return emptyStyle.Render("暂无传输任务")
}
