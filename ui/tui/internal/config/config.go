package config

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/spf13/viper"
)

// Config 主配置结构
type Config struct {
	Server  ServerConfig  `mapstructure:"server"`
	Storage StorageConfig `mapstructure:"storage"`
	Log     LogConfig     `mapstructure:"log"`
}

// ServerConfig 服务器配置
type ServerConfig struct {
	URL     string `mapstructure:"url"`
	Timeout int    `mapstructure:"timeout"`
}

// StorageConfig 存储配置
type StorageConfig struct {
	TokenPath string `mapstructure:"token_path"`
}

// LogConfig 日志配置
type LogConfig struct {
	Level  string `mapstructure:"level"`
	Format string `mapstructure:"format"`
}

var (
	cfg     *Config
	cfgFile string
)

// Init 初始化配置
func Init() error {
	// 设置默认值
	setDefaults()

	// 配置文件
	if cfgFile != "" {
		viper.SetConfigFile(cfgFile)
	} else {
		// 查找配置文件
		viper.SetConfigName("config")
		viper.SetConfigType("yaml")
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

// setDefaults 设置默认值
func setDefaults() {
	viper.SetDefault("server.url", "http://localhost:8080")
	viper.SetDefault("server.timeout", 30)
	viper.SetDefault("storage.token_path", "~/.config/disk-tui/token.enc")
	viper.SetDefault("log.level", "info")
	viper.SetDefault("log.format", "console")
}

// Get 获取配置
func Get() *Config {
	return cfg
}

// SetConfigFile 设置配置文件路径
func SetConfigFile(path string) {
	cfgFile = path
}

// GetTokenPath 获取 Token 存储路径（展开 ~）
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
func Reset() {
	viper.Reset()
	cfg = nil
	cfgFile = ""
}
