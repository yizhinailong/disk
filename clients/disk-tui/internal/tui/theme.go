package tui

import "github.com/charmbracelet/lipgloss"

// Theme holds the lipgloss styles used across the TUI.
type Theme struct {
	App          lipgloss.Style
	Title        lipgloss.Style
	Subtitle     lipgloss.Style
	Header       lipgloss.Style
	Footer       lipgloss.Style
	MenuItem     lipgloss.Style
	MenuItemSel  lipgloss.Style
	MenuItemDesc lipgloss.Style
	ListHeader   lipgloss.Style
	ListRow      lipgloss.Style
	Selected     lipgloss.Style
	Error        lipgloss.Style
	Success      lipgloss.Style
	Warning      lipgloss.Style
	Muted        lipgloss.Style
	Accent       lipgloss.Style
	Label        lipgloss.Style
	Value        lipgloss.Style
	HelpKey      lipgloss.Style
	HelpDesc     lipgloss.Style
	Dialog       lipgloss.Style
	Banner       lipgloss.Style
	Folder       lipgloss.Style
	File         lipgloss.Style
}

// DefaultTheme returns the default disk-tui theme.
func DefaultTheme() Theme {
	title := lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#7DD3FC"))
	subtitle := lipgloss.NewStyle().Foreground(lipgloss.Color("#A5B4FC"))
	header := lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#0EA5E9")).Padding(0, 1).
		Background(lipgloss.Color("#0B1220"))
	footer := lipgloss.NewStyle().Foreground(lipgloss.Color("#94A3B8")).Padding(0, 1)
	menuItem := lipgloss.NewStyle().Padding(0, 1)
	menuSel := lipgloss.NewStyle().Bold(true).Padding(0, 1).
		Foreground(lipgloss.Color("#0B1220")).Background(lipgloss.Color("#7DD3FC"))
	menuDesc := lipgloss.NewStyle().Foreground(lipgloss.Color("#64748B"))
	listHeader := lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#94A3B8"))
	listRow := lipgloss.NewStyle()
	sel := lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#7DD3FC"))
	errS := lipgloss.NewStyle().Foreground(lipgloss.Color("#F87171")).Bold(true)
	successS := lipgloss.NewStyle().Foreground(lipgloss.Color("#34D399"))
	warnS := lipgloss.NewStyle().Foreground(lipgloss.Color("#FBBF24"))
	muted := lipgloss.NewStyle().Foreground(lipgloss.Color("#64748B"))
	accent := lipgloss.NewStyle().Foreground(lipgloss.Color("#A78BFA")).Bold(true)
	label := lipgloss.NewStyle().Foreground(lipgloss.Color("#94A3B8")).Width(14)
	value := lipgloss.NewStyle().Foreground(lipgloss.Color("#E2E8F0"))
	helpKey := lipgloss.NewStyle().Foreground(lipgloss.Color("#7DD3FC")).Bold(true)
	helpDesc := lipgloss.NewStyle().Foreground(lipgloss.Color("#64748B"))
	dialog := lipgloss.NewStyle().Border(lipgloss.RoundedBorder()).BorderForeground(lipgloss.Color("#7DD3FC")).Padding(1, 2)
	banner := lipgloss.NewStyle().Foreground(lipgloss.Color("#7DD3FC")).Bold(true)
	folder := lipgloss.NewStyle().Foreground(lipgloss.Color("#FBBF24")).Bold(true)
	file := lipgloss.NewStyle().Foreground(lipgloss.Color("#94A3B8"))
	return Theme{
		Title:        title,
		Subtitle:     subtitle,
		Header:       header,
		Footer:       footer,
		MenuItem:     menuItem,
		MenuItemSel:  menuSel,
		MenuItemDesc: menuDesc,
		ListHeader:   listHeader,
		ListRow:      listRow,
		Selected:     sel,
		Error:        errS,
		Success:      successS,
		Warning:      warnS,
		Muted:        muted,
		Accent:       accent,
		Label:        label,
		Value:        value,
		HelpKey:      helpKey,
		HelpDesc:     helpDesc,
		Dialog:       dialog,
		Banner:       banner,
		Folder:       folder,
		File:         file,
	}
}

// JoinHelp renders a row of key/description hints.
func (t Theme) JoinHelp(pairs [][2]string) string {
	parts := make([]string, 0, len(pairs))
	for _, p := range pairs {
		parts = append(parts, t.HelpKey.Render(p[0])+t.HelpDesc.Render(" "+p[1]))
	}
	return lipgloss.JoinHorizontal(lipgloss.Left, parts...)
}
