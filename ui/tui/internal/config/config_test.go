package config

import (
	"os"
	"testing"

	"github.com/spf13/viper"
)

func TestInit(t *testing.T) {
	// 重置 viper
	Reset()

	err := Init()
	if err != nil {
		t.Fatalf("Init() failed: %v", err)
	}

	cfg := Get()
	if cfg == nil {
		t.Fatal("Get() returned nil")
	}

	// 检查默认值
	if cfg.Server.URL != "http://localhost:8080" {
		t.Errorf("Server.URL = %q, want %q", cfg.Server.URL, "http://localhost:8080")
	}
	if cfg.Server.Timeout != 30 {
		t.Errorf("Server.Timeout = %d, want 30", cfg.Server.Timeout)
	}
}

func TestGetTokenPath(t *testing.T) {
	cfg := &Config{
		Storage: StorageConfig{
			TokenPath: "~/.config/disk-tui/token.enc",
		},
	}

	path := cfg.GetTokenPath()
	home, _ := os.UserHomeDir()
	expected := home + "/.config/disk-tui/token.enc"

	if path != expected {
		t.Errorf("GetTokenPath() = %q, want %q", path, expected)
	}
}

func TestGetTokenPathEmpty(t *testing.T) {
	cfg := &Config{
		Storage: StorageConfig{
			TokenPath: "",
		},
	}

	path := cfg.GetTokenPath()
	if path != "" {
		t.Errorf("GetTokenPath() = %q, want empty string", path)
	}
}

func TestGetTokenPathAbsolutePath(t *testing.T) {
	cfg := &Config{
		Storage: StorageConfig{
			TokenPath: "/etc/disk-tui/token.enc",
		},
	}

	path := cfg.GetTokenPath()
	if path != "/etc/disk-tui/token.enc" {
		t.Errorf("GetTokenPath() = %q, want %q", path, "/etc/disk-tui/token.enc")
	}
}

func TestSetConfigFile(t *testing.T) {
	Reset()

	SetConfigFile("/custom/path/config.yaml")

	if cfgFile != "/custom/path/config.yaml" {
		t.Errorf("cfgFile = %q, want %q", cfgFile, "/custom/path/config.yaml")
	}
}

func TestReset(t *testing.T) {
	Init()
	Reset()

	if cfg != nil {
		t.Error("Reset() did not clear cfg")
	}
	if cfgFile != "" {
		t.Error("Reset() did not clear cfgFile")
	}
}

func init() {
	// 确保每个测试开始前 viper 是干净的状态
	viper.Reset()
}
