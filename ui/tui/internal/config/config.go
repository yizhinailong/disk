// Package config 配置管理模块
//
// 提供基于 Viper 的配置管理功能，支持多配置源（文件、环境变量）。
// 配置文件格式为 JSON，支持热重载和默认值。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package config

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/spf13/viper"
)

// Config 主配置结构
//
// 包含服务器、存储、日志等配置项，通过 Viper 从配置文件或环境变量加载。
type Config struct {
	Server  ServerConfig  `mapstructure:"server"`  // 服务器配置
	Storage StorageConfig `mapstructure:"storage"` // 存储配置
	Log     LogConfig     `mapstructure:"log"`     // 日志配置
}

// ServerConfig 服务器配置
//
// 定义后端服务器的连接参数。
type ServerConfig struct {
	URL     string `mapstructure:"url"`     // 服务器地址（如 http://localhost:8080）
	Timeout int    `mapstructure:"timeout"` // 请求超时时间（秒）
}

// StorageConfig 存储配置
//
// 定义本地存储相关配置。
type StorageConfig struct {
	TokenPath string `mapstructure:"token_path"` // Token 文件存储路径（支持 ~ 展开）
}

// LogConfig 日志配置
//
// 定义日志输出格式和级别。
type LogConfig struct {
	Level  string `mapstructure:"level"`  // 日志级别（debug/info/warn/error）
	Format string `mapstructure:"format"` // 日志格式（console/json）
}

var (
	cfg     *Config // 全局配置实例
	cfgFile string  // 自定义配置文件路径
)

// Init 初始化配置
//
// 从配置文件和环境变量加载配置。配置文件查找顺序：
// 1. 通过 SetConfigFile 设置的路径
// 2. ./config.json
// 3. ./configs/config.json
// 4. ~/.config/disk-tui/config.json
// 5. /etc/disk-tui/config.json
//
// 环境变量前缀为 DISK_，如 DISK_SERVER_URL。
//
// 返回:
//   - error: 配置加载错误
func Init() error {
	// 设置默认值
	setDefaults()

	// 配置文件
	if cfgFile != "" {
		viper.SetConfigFile(cfgFile)
	} else {
		// 查找配置文件
		viper.SetConfigName("config")
		viper.SetConfigType("json")
		viper.AddConfigPath(".")
		viper.AddConfigPath("./configs")
		viper.AddConfigPath("$HOME/.config/disk-tui")
		viper.AddConfigPath("/etc/disk-tui")
	}

	// 环境变量
	viper.SetEnvPrefix("DISK")
	viper.AutomaticEnv()

	// 绑定环境变量
	_ = viper.BindEnv("server.url", "DISK_SERVER_URL")
	_ = viper.BindEnv("storage.token_path", "DISK_TOKEN_PATH")
	_ = viper.BindEnv("log.level", "DISK_LOG_LEVEL")

	// 读取配置文件
	if err := viper.ReadInConfig(); err != nil {
		if _, ok := err.(viper.ConfigFileNotFoundError); !ok {
			return fmt.Errorf("读取配置文件失败: %w", err)
		}
		// 配置文件不存在时使用默认值
	}

	// 解析到结构体
	cfg = &Config{}
	if err := viper.Unmarshal(cfg); err != nil {
		return fmt.Errorf("解析配置失败: %w", err)
	}

	return nil
}

// setDefaults 设置 Viper 默认配置值
func setDefaults() {
	viper.SetDefault("server.url", "http://localhost:8080")
	viper.SetDefault("server.timeout", 30)
	viper.SetDefault("storage.token_path", "~/.config/disk-tui/token.enc")
	viper.SetDefault("log.level", "info")
	viper.SetDefault("log.format", "console")
}

// Get 获取全局配置实例
//
// 返回:
//   - *Config: 配置实例（可能为 nil，如果未初始化）
func Get() *Config {
	return cfg
}

// SetConfigFile 设置自定义配置文件路径
//
// 参数:
//   - path: 配置文件路径
func SetConfigFile(path string) {
	cfgFile = path
}

// GetTokenPath 获取 Token 存储路径（展开 ~）
//
// 将路径中的 ~ 展开为用户主目录。如果路径为空则返回空字符串。
//
// 返回:
//   - string: 展开后的完整路径
func (c *Config) GetTokenPath() string {
	if c.Storage.TokenPath == "" {
		return ""
	}
	if c.Storage.TokenPath[0] == '~' {
		home, _ := os.UserHomeDir()
		return filepath.Join(home, c.Storage.TokenPath[1:])
	}
	return c.Storage.TokenPath
}

// Reset 重置配置（用于测试）
//
// 清空全局配置实例和 Viper 状态，主要用于单元测试。
func Reset() {
	viper.Reset()
	cfg = nil
	cfgFile = ""
}
