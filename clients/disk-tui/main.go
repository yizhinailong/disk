// Command disk-tui is the terminal user interface for the disk backend.
//
// It connects to a disk backend (default http://127.0.0.1:8080/) and
// exposes every documented REST endpoint through a navigable screen
// interface powered by bubbletea + lipgloss.
package main

import (
	"fmt"
	"os"
	"path/filepath"

	tea "github.com/charmbracelet/bubbletea"

	"github.com/disk-tui/internal/client"
	"github.com/disk-tui/internal/config"
	"github.com/disk-tui/internal/tui"
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintf(os.Stderr, "disk-tui: %v\n", err)
		os.Exit(1)
	}
}

func run() error {
	// allow override of config path via env
	cfgPath := os.Getenv("DISK_TUI_CONFIG")
	if cfgPath == "" {
		p, err := config.DefaultConfigPath()
		if err != nil {
			return fmt.Errorf("resolve config path: %w", err)
		}
		cfgPath = p
	}
	// create dir upfront so users can find the file easily
	if err := os.MkdirAll(filepath.Dir(cfgPath), 0o700); err != nil && !os.IsExist(err) {
		return fmt.Errorf("create config dir: %w", err)
	}
	store, err := config.Load(cfgPath)
	if err != nil {
		return fmt.Errorf("load config: %w", err)
	}

	baseURL := store.BaseURL()
	if env := os.Getenv("DISK_BASE_URL"); env != "" {
		baseURL = env
	}
	if baseURL == "" {
		baseURL = client.DefaultBaseURL
	}

	c := client.New(
		client.WithBaseURL(baseURL),
		client.WithTokenSource(store.TokenSource()),
	)

	// restore persisted tokens, if any
	if store.AccessToken() != "" {
		c.SetTokens(store.AccessToken(), store.RefreshToken())
	}

	p := tea.NewProgram(tui.New(c, store), tea.WithAltScreen(), tea.WithMouseCellMotion())
	_, err = p.Run()
	return err
}
