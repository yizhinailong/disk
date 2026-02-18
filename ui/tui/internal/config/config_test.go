// Package config_test 配置模块测试
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package config

import (
	"os"
	"testing"

	"github.com/spf13/viper"
)

// TestInit 测试配置初始化
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

// TestGetTokenPath 测试获取 Token 路径（~ 展开）
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

// TestGetTokenPathEmpty 测试空 Token 路径
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

// TestGetTokenPathAbsolutePath 测试绝对路径 Token 路径
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

// TestSetConfigFile 测试设置配置文件路径
func TestSetConfigFile(t *testing.T) {
	Reset()

	SetConfigFile("/custom/path/config.yaml")

	if cfgFile != "/custom/path/config.yaml" {
		t.Errorf("cfgFile = %q, want %q", cfgFile, "/custom/path/config.yaml")
	}
}

// TestReset 测试配置重置
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

// init 初始化测试环境
func init() {
	// 确保每个测试开始前 viper 是干净的状态
	viper.Reset()
}
