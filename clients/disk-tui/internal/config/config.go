// Package config persists disk-tui runtime state (server URL + tokens)
// to a small JSON file under the user's config directory.
package config

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
)

// Config is the on-disk state.
type Config struct {
	BaseURL      string `json:"base_url,omitempty"`
	AccessToken  string `json:"access_token,omitempty"`
	RefreshToken string `json:"refresh_token,omitempty"`
}

// TokenSource adapts a *Store to the client.TokenSource interface.
type TokenSource struct{ s *Store }

func (t *TokenSource) AccessToken() string   { return t.s.AccessToken() }
func (t *TokenSource) RefreshToken() string  { return t.s.RefreshToken() }
func (t *TokenSource) SetTokens(a, r string) { t.s.SetTokens(a, r) }

// Store is a thread-safe config store backed by a JSON file.
type Store struct {
	mu   sync.RWMutex
	path string
	cfg  Config
}

// DefaultConfigPath returns the conventional config file location:
//
//	$XDG_CONFIG_HOME/disk-tui/config.json
//
// or
//
//	$HOME/.config/disk-tui/config.json
func DefaultConfigPath() (string, error) {
	dir, err := os.UserConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "disk-tui", "config.json"), nil
}

// Load reads the config file at path (created empty if missing).
func Load(path string) (*Store, error) {
	s := &Store{path: path}
	data, err := os.ReadFile(path)
	if err == nil {
		if err := json.Unmarshal(data, &s.cfg); err != nil {
			return nil, err
		}
	} else if !os.IsNotExist(err) {
		return nil, err
	}
	return s, nil
}

// LoadDefault loads from DefaultConfigPath().
func LoadDefault() (*Store, error) {
	p, err := DefaultConfigPath()
	if err != nil {
		return nil, err
	}
	return Load(p)
}

// Save writes the config back to disk (atomic write + mkdir).
func (s *Store) Save() error {
	s.mu.RLock()
	defer s.mu.RUnlock()
	if err := os.MkdirAll(filepath.Dir(s.path), 0o700); err != nil {
		return err
	}
	data, err := json.MarshalIndent(s.cfg, "", "  ")
	if err != nil {
		return err
	}
	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, s.path)
}

// Get returns a snapshot of the config.
func (s *Store) Get() Config {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.cfg
}

// SetBaseURL updates the base URL.
func (s *Store) SetBaseURL(u string) {
	s.mu.Lock()
	s.cfg.BaseURL = u
	s.mu.Unlock()
}

// BaseURL returns the configured base URL.
func (s *Store) BaseURL() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.cfg.BaseURL
}

// AccessToken returns the current access token.
func (s *Store) AccessToken() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.cfg.AccessToken
}

// RefreshToken returns the current refresh token.
func (s *Store) RefreshToken() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.cfg.RefreshToken
}

// SetTokens updates both tokens.
func (s *Store) SetTokens(access, refresh string) {
	s.mu.Lock()
	s.cfg.AccessToken = access
	s.cfg.RefreshToken = refresh
	s.mu.Unlock()
}

// ClearTokens wipes the stored tokens.
func (s *Store) ClearTokens() { s.SetTokens("", "") }

// TokenSource returns a client.TokenSource adapter for this store.
func (s *Store) TokenSource() *TokenSource { return &TokenSource{s: s} }
