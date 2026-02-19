// Package main TUI 客户端入口
//
// 提供命令行界面访问 Disk 网盘系统。
// 支持 Cobra 子命令架构，TUI 作为默认行为。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package main

func main() {
	if err := Execute(); err != nil {
		// Cobra already prints the error
	}
}
