// Package main Cobra CLI 根命令
//
// 提供根命令定义，TUI 作为默认行为，支持子命令扩展。
// 集成 Viper 配置管理和 Cobra CLI 框架。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package main

import (
	"fmt"
	"os"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	"github.com/yizhinailong/disk/ui/tui/internal/app"
	"github.com/yizhinailong/disk/ui/tui/internal/config"
)

// 构建时注入的版本信息
var (
	version = "dev"     // 版本号
	commit  = "none"    // Git 提交哈希
	date    = "unknown" // 构建日期
)

// cfgFile 配置文件路径
var cfgFile string

// rootCmd 根命令
//
// 默认行为是启动 TUI 界面，支持 --version、--config、--server 等全局参数。
var rootCmd = &cobra.Command{
	Use:   "disk-tui",
	Short: "Disk cloud storage terminal client",
	Long: `Disk TUI is a terminal user interface client based on Bubble Tea framework,
providing command-line access to the Disk cloud storage system.

Supports file management, upload/download, file sharing, trash, and more.
By default, starts an interactive TUI interface, also supports non-interactive operations via subcommands.`,
	Run: runTUI,
}

// runTUI 启动 TUI 界面
//
// 初始化配置并启动 Bubble Tea TUI 应用。
func runTUI(cmd *cobra.Command, args []string) {
	// 检查版本标志
	showVersion, _ := cmd.Flags().GetBool("version")
	if showVersion {
		fmt.Printf("disk-tui %s (commit: %s, built: %s)\n", version, commit, date)
		return
	}

	// 初始化配置
	if err := config.Init(); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to initialize config: %v\n", err)
		os.Exit(1)
	}

	// 从命令行参数覆盖配置
	serverURL := viper.GetString("server")
	if serverURL != "" {
		config.Get().Server.URL = serverURL
	}

	// 启动 TUI
	cfg := config.Get()
	model := app.New(cfg)

	p := tea.NewProgram(
		model,
		tea.WithAltScreen(),
	)

	if _, err := p.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to start: %v\n", err)
		os.Exit(1)
	}
}

// init 初始化命令
//
// 设置持久化标志和 Viper 绑定。
func init() {
	cobra.OnInitialize(initConfig)

	// 持久化标志（所有子命令可用）
	rootCmd.PersistentFlags().StringVar(&cfgFile, "config", "", "config file path")
	rootCmd.PersistentFlags().String("server", "", "server address")
	rootCmd.PersistentFlags().Bool("version", false, "show version information")

	// 绑定到 Viper
	viper.BindPFlag("config", rootCmd.PersistentFlags().Lookup("config"))
	viper.BindPFlag("server", rootCmd.PersistentFlags().Lookup("server"))
	viper.BindPFlag("version", rootCmd.PersistentFlags().Lookup("version"))
}

// initConfig 初始化配置
//
// 读取配置文件和环境变量。
func initConfig() {
	if cfgFile != "" {
		config.SetConfigFile(cfgFile)
	}
}

// Execute 执行根命令
//
// 程序入口点，由 main.go 调用。
func Execute() error {
	return rootCmd.Execute()
}
