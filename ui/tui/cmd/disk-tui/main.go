// Package main TUI 客户端入口
//
// 提供命令行界面访问 Disk 网盘系统。
// 支持版本显示、配置文件指定、服务器地址覆盖等功能。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"strings"

	tea "github.com/charmbracelet/bubbletea"

	"github.com/yizhinailong/disk/ui/tui/internal/app"
	"github.com/yizhinailong/disk/ui/tui/internal/config"
)

// 构建时注入的版本信息
var (
	version = "dev"     // 版本号
	commit  = "none"    // Git 提交哈希
	date    = "unknown" // 构建日期
)

// main 程序入口
//
// 解析命令行参数，初始化配置，启动 TUI 应用。
func main() {
	var (
		showVersion = flag.Bool("version", false, "显示版本信息")
		configFile  = flag.String("config", "", "配置文件路径")
		serverURL   = flag.String("server", "", "服务器地址")
	)
	flag.Parse()

	if *showVersion {
		fmt.Printf("disk-tui %s (commit: %s, built: %s)\n", version, commit, date)
		os.Exit(0)
	}

	if *configFile != "" {
		config.SetConfigFile(*configFile)
	}

	if err := config.Init(); err != nil {
		fmt.Fprintf(os.Stderr, "配置初始化失败: %v\n", err)
		os.Exit(1)
	}

	if *serverURL != "" {
		config.Get().Server.URL = *serverURL
	}

	password := getPassword()

	cfg := config.Get()
	model := app.New(cfg, password)

	p := tea.NewProgram(
		model,
		tea.WithAltScreen(),
	)

	if _, err := p.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "启动失败: %v\n", err)
		os.Exit(1)
	}
}

// getPassword 获取加密密码
//
// 优先从环境变量 DISK_PASSWORD 读取，否则交互式提示用户输入。
//
// 返回:
//   - string: 用户输入的加密密码
func getPassword() string {
	if pwd := os.Getenv("DISK_PASSWORD"); pwd != "" {
		return pwd
	}

	fmt.Print("请输入加密密码: ")
	reader := bufio.NewReader(os.Stdin)
	password, _ := reader.ReadString('\n')
	return strings.TrimSpace(password)
}
