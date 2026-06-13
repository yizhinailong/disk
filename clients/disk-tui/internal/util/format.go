// Package util contains formatting helpers for the TUI layer.
package util

import (
	"fmt"
	"strings"
	"time"
)

// FormatBytes converts a byte count into a human-readable string.
func FormatBytes(n uint64) string {
	if n < 1024 {
		return fmt.Sprintf("%d B", n)
	}
	const u = 1024.0
	f := float64(n)
	units := []string{"B", "KB", "MB", "GB", "TB", "PB"}
	idx := 0
	for f >= u && idx < len(units)-1 {
		f /= u
		idx++
	}
	return fmt.Sprintf("%.2f %s", f, units[idx])
}

// FormatBytesI64 is the int64 variant of FormatBytes.
func FormatBytesI64(n int64) string {
	if n < 0 {
		return "-"
	}
	return FormatBytes(uint64(n))
}

// FormatTime parses an ISO-8601/RFC3339 string and reformats for display.
// Falls back to the raw string on parse failure.
func FormatTime(s string) string {
	if s == "" {
		return "-"
	}
	for _, layout := range []string{time.RFC3339Nano, time.RFC3339, "2006-01-02 15:04:05", "2006-01-02T15:04:05"} {
		if t, err := time.Parse(layout, s); err == nil {
			return t.Local().Format("2006-01-02 15:04:05")
		}
	}
	return s
}

// FormatDuration converts seconds into "1d2h3m" style.
func FormatDuration(seconds uint64) string {
	if seconds == 0 {
		return "-"
	}
	d := time.Duration(seconds) * time.Second
	days := int(d.Hours()) / 24
	d -= time.Duration(days) * 24 * time.Hour
	hours := int(d.Hours())
	d -= time.Duration(hours) * time.Hour
	mins := int(d.Minutes())
	d -= time.Duration(mins) * time.Minute
	secs := int(d.Seconds())
	parts := []string{}
	if days > 0 {
		parts = append(parts, fmt.Sprintf("%dd", days))
	}
	if hours > 0 {
		parts = append(parts, fmt.Sprintf("%dh", hours))
	}
	if mins > 0 {
		parts = append(parts, fmt.Sprintf("%dm", mins))
	}
	if secs > 0 || len(parts) == 0 {
		parts = append(parts, fmt.Sprintf("%ds", secs))
	}
	return strings.Join(parts, "")
}

// Truncate shortens s to n runes, appending an ellipsis if shortened.
func Truncate(s string, n int) string {
	if n <= 0 {
		return ""
	}
	r := []rune(s)
	if len(r) <= n {
		return s
	}
	return string(r[:n-1]) + "…"
}

// UserStatusName maps a numeric admin user status to a display name.
func UserStatusName(status int) string {
	switch status {
	case 0:
		return "disabled"
	case 1:
		return "active"
	case 2:
		return "locked"
	default:
		return fmt.Sprintf("status(%d)", status)
	}
}

// RoleName maps a numeric admin role to a display name.
func RoleName(role int) string {
	switch role {
	case 0:
		return "user"
	case 1:
		return "admin"
	default:
		return fmt.Sprintf("role(%d)", role)
	}
}
