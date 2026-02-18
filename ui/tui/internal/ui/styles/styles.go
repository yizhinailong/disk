// Package styles TUI 样式定义
//
// 提供颜色方案、组件样式和工具函数，
// 确保终端界面视觉呈现的一致性。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package styles

import (
	"strconv"

	"github.com/charmbracelet/lipgloss"
)

// =============================================================================
// 颜色定义 (Color Definitions)
// Based on docs/ui/tui/02-界面设计规范.md Section 5
// =============================================================================

// 主色调 (Primary Colors)
var (
	// ColorPrimary - 主色，用于标题、选中项背景
	ColorPrimary = lipgloss.Color("#268bd2")
	// ColorSecondary - 辅助色，用于链接、次要信息
	ColorSecondary = lipgloss.Color("#2aa198")
	// ColorAccent - 强调色
	ColorAccent = lipgloss.Color("#2aa198")
)

// 状态颜色 (Status Colors)
var (
	// ColorSuccess - 成功消息、完成状态
	ColorSuccess = lipgloss.Color("#859900")
	// ColorWarning - 警告消息、注意提示
	ColorWarning = lipgloss.Color("#b58900")
	// ColorError - 错误消息、失败状态
	ColorError = lipgloss.Color("#dc322f")
	// ColorInfo - 信息提示
	ColorInfo = lipgloss.Color("#268bd2")
)

// 文字颜色 (Text Colors)
var (
	// ColorText - 主文字颜色
	ColorText = lipgloss.Color("#d4d4d4")
	// ColorTextMuted - 暗淡文字颜色
	ColorTextMuted = lipgloss.Color("#9CA3AF")
	// ColorTextDark - 深色文字
	ColorTextDark = lipgloss.Color("#374151")
	// ColorTextHighlight - 高亮文字（搜索匹配）
	ColorTextHighlight = lipgloss.Color("#ffff00")
	// ColorTextSelected - 选中项文字
	ColorTextSelected = lipgloss.Color("#ffffff")
)

// 背景颜色 (Background Colors)
var (
	// ColorBgPrimary - 主背景色
	ColorBgPrimary = lipgloss.Color("#1e1e1e")
	// ColorBgSecondary - 次要背景色
	ColorBgSecondary = lipgloss.Color("#374151")
	// ColorBgHighlight - 高亮背景色（鼠标悬停行）
	ColorBgHighlight = lipgloss.Color("#303030")
	// ColorBgSelected - 选中项背景色
	ColorBgSelected = lipgloss.Color("#005f87")
)

// 边框颜色 (Border Colors)
var (
	// ColorBorder - 默认边框颜色
	ColorBorder = lipgloss.Color("#4B5563")
	// ColorBorderFocus - 焦点边框颜色
	ColorBorderFocus = lipgloss.Color("#268bd2")
	// ColorBorderError - 错误边框颜色
	ColorBorderError = lipgloss.Color("#dc322f")
)

// 文件类型颜色 (File Type Colors)
var (
	// ColorFolder - 文件夹颜色
	ColorFolder = lipgloss.Color("#268bd2")
	// ColorDocument - 文档颜色
	ColorDocument = lipgloss.Color("#d4d4d4")
	// ColorImage - 图片颜色
	ColorImage = lipgloss.Color("#d33682")
	// ColorVideo - 视频颜色
	ColorVideo = lipgloss.Color("#dc322f")
	// ColorAudio - 音频颜色
	ColorAudio = lipgloss.Color("#859900")
	// ColorArchive - 压缩包颜色
	ColorArchive = lipgloss.Color("#b58900")
	// ColorCode - 代码文件颜色
	ColorCode = lipgloss.Color("#2aa198")
	// ColorOther - 其他文件颜色
	ColorOther = lipgloss.Color("#808080")
)

// =============================================================================
// 256色降级方案 (256-color Fallback)
// =============================================================================

var (
	// ColorPrimary256 - 256色主色
	ColorPrimary256 = lipgloss.Color("33")
	// ColorSecondary256 - 256色辅助色
	ColorSecondary256 = lipgloss.Color("37")
	// ColorBgPrimary256 - 256色主背景
	ColorBgPrimary256 = lipgloss.Color("235")
	// ColorText256 - 256色前景
	ColorText256 = lipgloss.Color("252")
	// ColorSuccess256 - 256色成功
	ColorSuccess256 = lipgloss.Color("70")
	// ColorWarning256 - 256色警告
	ColorWarning256 = lipgloss.Color("136")
	// ColorError256 - 256色错误
	ColorError256 = lipgloss.Color("160")
	// ColorBgSelected256 - 256色选中背景
	ColorBgSelected256 = lipgloss.Color("24")
	// ColorTextSelected256 - 256色选中文字
	ColorTextSelected256 = lipgloss.Color("231")
	// ColorHighlight256 - 256色高亮行
	ColorHighlight256 = lipgloss.Color("236")
)

// =============================================================================
// 基础样式 (Base Styles)
// =============================================================================

var (
	// TitleStyle - 标题样式
	TitleStyle = lipgloss.NewStyle().
			Foreground(ColorPrimary).
			Bold(true).
			Padding(0, 1)

	// SubtitleStyle - 副标题样式
	SubtitleStyle = lipgloss.NewStyle().
			Foreground(ColorTextMuted).
			Padding(0, 1)

	// TextStyle - 正文样式
	TextStyle = lipgloss.NewStyle().
			Foreground(ColorText)

	// MutedStyle - 暗淡文字样式
	MutedStyle = lipgloss.NewStyle().
			Foreground(ColorTextMuted)

	// BoldStyle - 粗体样式
	BoldStyle = lipgloss.NewStyle().
			Bold(true)

	// ItalicStyle - 斜体样式
	ItalicStyle = lipgloss.NewStyle().
			Italic(true)
)

// =============================================================================
// 组件样式 (Component Styles)
// =============================================================================

var (
	// SelectedStyle - 选中项样式
	SelectedStyle = lipgloss.NewStyle().
			Foreground(ColorTextSelected).
			Background(ColorBgSelected).
			Bold(true).
			Padding(0, 1)

	// ListItemStyle - 列表项样式
	ListItemStyle = lipgloss.NewStyle().
			Foreground(ColorText).
			Padding(0, 1)

	// ListItemHighlightStyle - 列表项高亮样式（鼠标悬停）
	ListItemHighlightStyle = lipgloss.NewStyle().
				Background(ColorBgHighlight).
				Padding(0, 1)

	// FolderStyle - 文件夹样式
	FolderStyle = lipgloss.NewStyle().
			Foreground(ColorFolder).
			Bold(true)

	// FileStyle - 文件样式
	FileStyle = lipgloss.NewStyle().
			Foreground(ColorDocument)

	// SizeStyle - 大小样式
	SizeStyle = lipgloss.NewStyle().
			Foreground(ColorTextMuted).
			Align(lipgloss.Right)

	// TimeStyle - 时间样式
	TimeStyle = lipgloss.NewStyle().
			Foreground(ColorTextMuted)

	// BreadcrumbStyle - 面包屑样式
	BreadcrumbStyle = lipgloss.NewStyle().
			Foreground(ColorTextMuted)

	// BreadcrumbCurrentStyle - 当前路径样式
	BreadcrumbCurrentStyle = lipgloss.NewStyle().
				Foreground(ColorPrimary).
				Bold(true)
)

// =============================================================================
// 状态样式 (Status Styles)
// =============================================================================

var (
	// SuccessStyle - 成功样式
	SuccessStyle = lipgloss.NewStyle().
			Foreground(ColorSuccess).
			Bold(true)

	// WarningStyle - 警告样式
	WarningStyle = lipgloss.NewStyle().
			Foreground(ColorWarning).
			Bold(true)

	// ErrorStyle - 错误样式
	ErrorStyle = lipgloss.NewStyle().
			Foreground(ColorError).
			Bold(true)

	// InfoStyle - 信息样式
	InfoStyle = lipgloss.NewStyle().
			Foreground(ColorInfo)

	// LoadingStyle - 加载中样式
	LoadingStyle = lipgloss.NewStyle().
			Foreground(ColorTextMuted).
			Italic(true)
)

// =============================================================================
// 边框样式 (Border Styles)
// =============================================================================

var (
	// BorderStyle - 普通边框
	BorderStyle = lipgloss.NewStyle().
			Border(lipgloss.NormalBorder()).
			BorderForeground(ColorBorder).
			Padding(0, 1)

	// BorderFocusStyle - 焦点边框
	BorderFocusStyle = lipgloss.NewStyle().
				Border(lipgloss.NormalBorder()).
				BorderForeground(ColorBorderFocus).
				Padding(0, 1)

	// BorderErrorStyle - 错误边框
	BorderErrorStyle = lipgloss.NewStyle().
				Border(lipgloss.NormalBorder()).
				BorderForeground(ColorBorderError).
				Padding(0, 1)

	// RoundedBorderStyle - 圆角边框
	RoundedBorderStyle = lipgloss.NewStyle().
				Border(lipgloss.RoundedBorder()).
				BorderForeground(ColorBorder).
				Padding(0, 1)

	// RoundedBorderFocusStyle - 焦点圆角边框
	RoundedBorderFocusStyle = lipgloss.NewStyle().
				Border(lipgloss.RoundedBorder()).
				BorderForeground(ColorBorderFocus).
				Padding(0, 1)
)

// =============================================================================
// 进度条样式 (Progress Bar Styles)
// =============================================================================

var (
	// ProgressContainerStyle - 进度条容器
	ProgressContainerStyle = lipgloss.NewStyle().
				Foreground(ColorTextMuted)

	// ProgressFilledStyle - 进度条已填充
	ProgressFilledStyle = lipgloss.NewStyle().
				Foreground(ColorPrimary)

	// ProgressEmptyStyle - 进度条未填充
	ProgressEmptyStyle = lipgloss.NewStyle().
				Foreground(ColorBgSecondary)

	// ProgressPausedStyle - 暂停状态进度条
	ProgressPausedStyle = lipgloss.NewStyle().
				Foreground(ColorWarning)

	// ProgressErrorStyle - 错误状态进度条
	ProgressErrorStyle = lipgloss.NewStyle().
				Foreground(ColorError)
)

// 进度条字符
const (
	ProgressFilled   = "█" // 已填充
	ProgressEmpty    = "░" // 未填充
	ProgressPaused   = "▓" // 暂停
	ProgressComplete = "█" // 完成
)

// =============================================================================
// 输入框样式 (Input Styles)
// =============================================================================

var (
	// InputStyle - 输入框
	InputStyle = lipgloss.NewStyle().
			Border(lipgloss.NormalBorder()).
			BorderForeground(ColorBorder).
			Padding(0, 1)

	// InputFocusStyle - 焦点输入框
	InputFocusStyle = lipgloss.NewStyle().
			Border(lipgloss.NormalBorder()).
			BorderForeground(ColorBorderFocus).
			Padding(0, 1)

	// InputErrorStyle - 错误输入框
	InputErrorStyle = lipgloss.NewStyle().
			Border(lipgloss.NormalBorder()).
			BorderForeground(ColorBorderError).
			Padding(0, 1)

	// PlaceholderStyle - 占位符
	PlaceholderStyle = lipgloss.NewStyle().
				Foreground(ColorTextMuted)

	// LabelStyle - 标签样式
	LabelStyle = lipgloss.NewStyle().
			Foreground(ColorText).
			Bold(true)

	// CursorStyle - 光标样式
	CursorStyle = lipgloss.NewStyle().
			Foreground(ColorPrimary).
			Bold(true)
)

// =============================================================================
// 状态栏样式 (Status Bar Styles)
// =============================================================================

var (
	// StatusBarStyle - 状态栏
	StatusBarStyle = lipgloss.NewStyle().
			Background(ColorBgPrimary).
			Foreground(ColorText).
			Padding(0, 1)

	// TopBarStyle - 顶部状态栏样式
	TopBarStyle = lipgloss.NewStyle().
			Background(ColorPrimary).
			Foreground(ColorTextSelected).
			Padding(0, 1)

	// KeyStyle - 快捷键提示
	KeyStyle = lipgloss.NewStyle().
			Foreground(ColorPrimary).
			Bold(true)

	// KeyDescStyle - 快捷键描述
	KeyDescStyle = lipgloss.NewStyle().
			Foreground(ColorTextMuted)

	// DividerStyle - 分隔符样式
	DividerStyle = lipgloss.NewStyle().
			Foreground(ColorTextMuted)
)

// =============================================================================
// 帮助样式 (Help Styles)
// =============================================================================

var (
	// HelpTitleStyle - 帮助标题
	HelpTitleStyle = lipgloss.NewStyle().
			Foreground(ColorPrimary).
			Bold(true).
			Margin(1, 0)

	// HelpItemStyle - 帮助项
	HelpItemStyle = lipgloss.NewStyle().
			Foreground(ColorText).
			Padding(0, 2)

	// HelpKeyStyle - 帮助快捷键
	HelpKeyStyle = lipgloss.NewStyle().
			Foreground(ColorPrimary).
			Bold(true).
			Width(12)

	// HelpDescStyle - 帮助描述
	HelpDescStyle = lipgloss.NewStyle().
			Foreground(ColorTextMuted)
)

// =============================================================================
// 对话框样式 (Dialog Styles)
// =============================================================================

var (
	// DialogStyle - 对话框样式
	DialogStyle = lipgloss.NewStyle().
			Border(lipgloss.RoundedBorder()).
			BorderForeground(ColorPrimary).
			Padding(1, 2)

	// DialogTitleStyle - 对话框标题
	DialogTitleStyle = lipgloss.NewStyle().
				Foreground(ColorPrimary).
				Bold(true).
				Align(lipgloss.Center)

	// DialogContentStyle - 对话框内容
	DialogContentStyle = lipgloss.NewStyle().
				Foreground(ColorText).
				Padding(1, 0)

	// DialogFooterStyle - 对话框底部
	DialogFooterStyle = lipgloss.NewStyle().
				Foreground(ColorTextMuted).
				Align(lipgloss.Center).
				MarginTop(1)

	// ConfirmButtonStyle - 确认按钮
	ConfirmButtonStyle = lipgloss.NewStyle().
				Foreground(ColorSuccess).
				Bold(true).
				Padding(0, 1)

	// CancelButtonStyle - 取消按钮
	CancelButtonStyle = lipgloss.NewStyle().
				Foreground(ColorTextMuted).
				Padding(0, 1)
)

// =============================================================================
// 图标定义 (Icon Definitions)
// =============================================================================

// 文件类型图标
const (
	IconFolder   = "📁"
	IconFile     = "📄"
	IconImage    = "📷"
	IconVideo    = "🎬"
	IconAudio    = "🎵"
	IconArchive  = "📦"
	IconCode     = "⚙️"
	IconDocument = "📝"
	IconSpread   = "📊"
	IconPresent  = "📽️"
	IconLink     = "🔗"
	IconUnknown  = "📎"
)

// 状态图标
const (
	IconSelected    = "▶"
	IconUploading   = "📤"
	IconDownloading = "📥"
	IconSuccess     = "✓"
	IconError       = "✗"
	IconWarning     = "⚠"
	IconInfo        = "ℹ"
	IconLoading     = "⏳"
	IconLocked      = "🔒"
	IconEmpty       = "📭"
)

// 操作图标
const (
	IconSearch = "🔍"
	IconSet    = "⚙"
	IconHelp   = "❓"
	IconUser   = "👤"
)

// ASCII 降级符号（用于不支持 Unicode 的终端）
const (
	IconFolderASCII   = "[D]"
	IconFileASCII     = "[F]"
	IconSuccessASCII  = "[OK]"
	IconErrorASCII    = "[X]"
	IconWarningASCII  = "[!]"
	IconLoadingASCII  = "..."
	IconSelectedASCII = ">"
)

// =============================================================================
// 工具函数 (Utility Functions)
// =============================================================================

// GetFileIcon 根据文件类型获取图标
//
// 参数:
//   - name: 文件名
//   - isFolder: 是否为文件夹
//
// 返回:
//   - string: 对应的图标
func GetFileIcon(name string, isFolder bool) string {
	if isFolder {
		return IconFolder
	}

	ext := getFileExtension(name)
	switch ext {
	case ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".svg", ".ico":
		return IconImage
	case ".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".webm":
		return IconVideo
	case ".mp3", ".wav", ".flac", ".aac", ".ogg", ".m4a", ".wma":
		return IconAudio
	case ".zip", ".rar", ".7z", ".tar", ".gz", ".bz2", ".xz":
		return IconArchive
	case ".go", ".js", ".ts", ".py", ".java", ".c", ".cpp", ".h", ".hpp",
		".rs", ".rb", ".php", ".cs", ".swift", ".kt", ".scala":
		return IconCode
	case ".pdf":
		return IconDocument
	case ".doc", ".docx", ".txt", ".md", ".rtf", ".odt":
		return IconDocument
	case ".xls", ".xlsx", ".csv", ".ods":
		return IconSpread
	case ".ppt", ".pptx", ".odp":
		return IconPresent
	case ".lnk", ".url":
		return IconLink
	default:
		return IconFile
	}
}

// GetFileIconASCII 获取 ASCII 降级图标
//
// 参数:
//   - name: 文件名（未使用）
//   - isFolder: 是否为文件夹
//
// 返回:
//   - string: ASCII 图标
func GetFileIconASCII(_ string, isFolder bool) string {
	if isFolder {
		return IconFolderASCII
	}
	return IconFileASCII
}

// getFileExtension 获取文件扩展名（包含点号）
//
// 参数:
//   - name: 文件名
//
// 返回:
//   - string: 扩展名（如 ".txt"）
func getFileExtension(name string) string {
	for i := len(name) - 1; i >= 0; i-- {
		if name[i] == '.' {
			return name[i:]
		}
		if name[i] == '/' || name[i] == '\\' {
			break
		}
	}
	return ""
}

// GetFileColor 根据文件类型获取颜色
//
// 参数:
//   - name: 文件名
//   - isFolder: 是否为文件夹
//
// 返回:
//   - lipgloss.Color: 对应的颜色
func GetFileColor(name string, isFolder bool) lipgloss.Color {
	if isFolder {
		return ColorFolder
	}

	ext := getFileExtension(name)
	switch ext {
	case ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".svg", ".ico":
		return ColorImage
	case ".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".webm":
		return ColorVideo
	case ".mp3", ".wav", ".flac", ".aac", ".ogg", ".m4a", ".wma":
		return ColorAudio
	case ".zip", ".rar", ".7z", ".tar", ".gz", ".bz2", ".xz":
		return ColorArchive
	case ".go", ".js", ".ts", ".py", ".java", ".c", ".cpp", ".h", ".hpp",
		".rs", ".rb", ".php", ".cs", ".swift", ".kt", ".scala":
		return ColorCode
	default:
		return ColorDocument
	}
}

// FormatSize 格式化文件大小
//
// 参数:
//   - size: 文件大小（字节）
//
// 返回:
//   - string: 格式化后的大小字符串（如 "1.5 MB"）
func FormatSize(size uint64) string {
	const (
		KB = 1024
		MB = KB * 1024
		GB = MB * 1024
		TB = GB * 1024
	)

	switch {
	case size >= TB:
		return formatFloat(float64(size)/float64(TB), 2) + " TB"
	case size >= GB:
		return formatFloat(float64(size)/float64(GB), 2) + " GB"
	case size >= MB:
		return formatFloat(float64(size)/float64(MB), 2) + " MB"
	case size >= KB:
		return formatFloat(float64(size)/float64(KB), 2) + " KB"
	default:
		return strconv.FormatUint(size, 10) + " B"
	}
}

// FormatSizeShort 格式化文件大小（短格式，无空格）
//
// 参数:
//   - size: 文件大小（字节）
//
// 返回:
//   - string: 格式化后的大小字符串（如 "1.5MB"）
func FormatSizeShort(size uint64) string {
	const (
		KB = 1024
		MB = KB * 1024
		GB = MB * 1024
	)

	switch {
	case size >= GB:
		return formatFloat(float64(size)/float64(GB), 1) + "GB"
	case size >= MB:
		return formatFloat(float64(size)/float64(MB), 1) + "MB"
	case size >= KB:
		return formatFloat(float64(size)/float64(KB), 1) + "KB"
	default:
		return strconv.FormatUint(size, 10) + "B"
	}
}

// formatFloat 格式化浮点数
//
// 参数:
//   - f: 浮点数
//   - precision: 小数位数
//
// 返回:
//   - string: 格式化后的字符串
func formatFloat(f float64, precision int) string {
	return strconv.FormatFloat(f, 'f', precision, 64)
}

// FormatPercent 格式化百分比
//
// 参数:
//   - current: 当前值
//   - total: 总值
//
// 返回:
//   - string: 百分比字符串（如 "75%"）
func FormatPercent(current, total uint64) string {
	if total == 0 {
		return "0%"
	}
	percent := float64(current) / float64(total) * 100
	if percent > 100 {
		percent = 100
	}
	return strconv.FormatFloat(percent, 'f', 0, 64) + "%"
}

// FormatStorage 格式化存储空间显示
//
// 参数:
//   - used: 已使用空间
//   - total: 总空间
//
// 返回:
//   - string: 存储空间字符串（如 "1.5 GB / 10 GB (15%)"）
func FormatStorage(used, total uint64) string {
	return FormatSize(used) + "/" + FormatSize(total) + " (" + FormatPercent(used, total) + ")"
}

// MakeProgressBar 生成进度条字符串
//
// 参数:
//   - width: 进度条宽度
//   - percent: 进度百分比
//   - filled: 填充字符
//   - empty: 空白字符
//
// 返回:
//   - string: 进度条字符串
func MakeProgressBar(width int, percent float64, filled, empty string) string {
	if width <= 0 {
		return ""
	}

	filledWidth := int(float64(width) * percent / 100)
	if filledWidth > width {
		filledWidth = width
	}
	if filledWidth < 0 {
		filledWidth = 0
	}

	emptyWidth := width - filledWidth

	result := ""
	for i := 0; i < filledWidth; i++ {
		result += filled
	}
	for i := 0; i < emptyWidth; i++ {
		result += empty
	}

	return result
}

// MakeProgressBarDefault 使用默认字符生成进度条
//
// 参数:
//   - width: 进度条宽度
//   - percent: 进度百分比
//
// 返回:
//   - string: 进度条字符串
func MakeProgressBarDefault(width int, percent float64) string {
	return MakeProgressBar(width, percent, ProgressFilled, ProgressEmpty)
}

// JoinHorizontal 水平连接多个字符串
//
// 参数:
//   - strs: 字符串列表
//
// 返回:
//   - string: 连接后的字符串
func JoinHorizontal(strs ...string) string {
	return lipgloss.JoinHorizontal(lipgloss.Top, strs...)
}

// JoinVertical 垂直连接多个字符串
//
// 参数:
//   - strs: 字符串列表
//
// 返回:
//   - string: 连接后的字符串
func JoinVertical(strs ...string) string {
	return lipgloss.JoinVertical(lipgloss.Left, strs...)
}

// Width 获取字符串显示宽度
//
// 参数:
//   - s: 字符串
//
// 返回:
//   - int: 显示宽度
func Width(s string) int {
	return lipgloss.Width(s)
}

// Height 获取字符串显示高度
//
// 参数:
//   - s: 字符串
//
// 返回:
//   - int: 显示高度（行数）
func Height(s string) int {
	return lipgloss.Height(s)
}

// Center 居中字符串
//
// 参数:
//   - s: 字符串
//   - width: 目标宽度
//
// 返回:
//   - string: 居中后的字符串
func Center(s string, width int) string {
	return lipgloss.NewStyle().Width(width).Align(lipgloss.Center).Render(s)
}

// PadLeft 左侧填充空格
//
// 参数:
//   - s: 字符串
//   - width: 目标宽度
//
// 返回:
//   - string: 左填充后的字符串
func PadLeft(s string, width int) string {
	return lipgloss.NewStyle().Width(width).Align(lipgloss.Right).Render(s)
}

// PadRight 右侧填充空格
//
// 参数:
//   - s: 字符串
//   - width: 目标宽度
//
// 返回:
//   - string: 右填充后的字符串
func PadRight(s string, width int) string {
	return lipgloss.NewStyle().Width(width).Align(lipgloss.Left).Render(s)
}

// Truncate 截断字符串（超出部分用省略号）
//
// 参数:
//   - s: 字符串
//   - maxLen: 最大长度
//
// 返回:
//   - string: 截断后的字符串
func Truncate(s string, maxLen int) string {
	if len(s) <= maxLen {
		return s
	}
	if maxLen <= 3 {
		return s[:maxLen]
	}
	return s[:maxLen-3] + "..."
}

// RepeatString 重复字符串
//
// 参数:
//   - s: 字符串
//   - count: 重复次数
//
// 返回:
//   - string: 重复后的字符串
func RepeatString(s string, count int) string {
	result := ""
	for i := 0; i < count; i++ {
		result += s
	}
	return result
}
