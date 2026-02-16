package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"strings"

	tea "github.com/charmbracelet/bubbletea"

	"github.com/liufeng/disk/ui/tui/internal/app"
	"github.com/liufeng/disk/ui/tui/internal/config"
)

var (
	version = "dev"
	commit  = "none"
	date    = "unknown"
)

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

func getPassword() string {
	if pwd := os.Getenv("DISK_PASSWORD"); pwd != "" {
		return pwd
	}

	fmt.Print("请输入加密密码: ")
	reader := bufio.NewReader(os.Stdin)
	password, _ := reader.ReadString('\n')
	return strings.TrimSpace(password)
}
