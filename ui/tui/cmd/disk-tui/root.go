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
	Short: "Disk 网盘系统终端客户端",
	Long: `Disk TUI 是一个基于 Bubble Tea 框架的终端用户界面客户端，
为 Disk 网盘系统提供命令行访问能力。

支持文件管理、上传下载、文件分享、回收站等功能。
默认启动交互式 TUI 界面，也支持通过子命令进行非交互式操作。`,
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
		fmt.Fprintf(os.Stderr, "配置初始化失败: %v\n", err)
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
		fmt.Fprintf(os.Stderr, "启动失败: %v\n", err)
		os.Exit(1)
	}
}

// init 初始化命令
//
// 设置持久化标志和 Viper 绑定。
func init() {
	cobra.OnInitialize(initConfig)

	// 持久化标志（所有子命令可用）
	rootCmd.PersistentFlags().StringVar(&cfgFile, "config", "", "配置文件路径")
	rootCmd.PersistentFlags().String("server", "", "服务器地址")
	rootCmd.PersistentFlags().Bool("version", false, "显示版本信息")

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
