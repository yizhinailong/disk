// Package main Cobra CLI 下载命令
//
// 提供文件下载子命令，支持断点续传和进度显示。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package main

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	"github.com/yizhinailong/disk/ui/tui/internal/api"
	"github.com/yizhinailong/disk/ui/tui/internal/config"
	"github.com/yizhinailong/disk/ui/tui/internal/downloader"
	"github.com/yizhinailong/disk/ui/tui/internal/store"
)

var (
	downloadOutput string
	downloadInfo   bool
)

// downloadCmd 下载命令
//
// 从 Disk 云存储下载文件到本地。
var downloadCmd = &cobra.Command{
	Use:   "download <file_id>",
	Short: "Download file",
	Long: `Download file from Disk cloud storage to local.

Supports resuming interrupted downloads and progress display. Use --info to view file information without downloading.`,
	Args: cobra.ExactArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		if err := runDownload(cmd, args); err != nil {
			fmt.Fprintf(cmd.ErrOrStderr(), "Error: %v\n", err)
			os.Exit(1)
		}
	},
}

func init() {
	downloadCmd.Flags().StringVarP(&downloadOutput, "output", "o", "", "output directory (default: current directory)")
	downloadCmd.Flags().BoolVar(&downloadInfo, "info", false, "show file info only")
	rootCmd.AddCommand(downloadCmd)
}

func runDownload(cmd *cobra.Command, args []string) error {
	fileID, err := strconv.ParseUint(args[0], 10, 64)
	if err != nil {
		return fmt.Errorf("invalid file ID: %s", args[0])
	}

	if err := config.Init(); err != nil {
		return fmt.Errorf("failed to initialize config: %w", err)
	}

	if serverURL := viper.GetString("server"); serverURL != "" {
		config.Get().Server.URL = serverURL
	}

	cfg := config.Get()
	client := api.NewClient(cfg, store.NewTokenStore(cfg.GetTokenPath()))

	if err := client.LoadToken(); err != nil {
		return fmt.Errorf("failed to load token: %w (please login first)", err)
	}
	if !client.IsLoggedIn() {
		return fmt.Errorf("not logged in, please login first")
	}

	file, err := client.File.Get(context.Background(), fileID)
	if err != nil {
		return fmt.Errorf("failed to get file info: %w", err)
	}

	if file.IsFolder() {
		return fmt.Errorf("downloading folders is not supported: %s", file.Name)
	}

	if downloadInfo {
		fmt.Printf("Filename: %s\n", file.Name)
		fmt.Printf("Size: %s\n", formatDownloadSize(int64(file.Size)))
		fmt.Printf("Type: %s\n", file.MimeType)
		fmt.Printf("Hash: %s\n", file.Hash)
		return nil
	}

	outputDir := downloadOutput
	if outputDir == "" {
		outputDir = "."
	}

	absPath, err := filepath.Abs(outputDir)
	if err != nil {
		return fmt.Errorf("failed to get output path: %w", err)
	}

	info, err := os.Stat(absPath)
	if err != nil {
		if os.IsNotExist(err) {
			return fmt.Errorf("output directory does not exist: %s", absPath)
		}
		return fmt.Errorf("failed to access output directory: %w", err)
	}
	if !info.IsDir() {
		return fmt.Errorf("output path is not a directory: %s", absPath)
	}

	d := downloader.New(client)

	ctx := context.Background()
	task, err := d.DownloadWithProgress(ctx, file, absPath, func(info downloader.ProgressInfo) {
		printDownloadProgress(info)
	})
	if err != nil {
		return fmt.Errorf("download failed: %w", err)
	}

	fmt.Printf("\n✓ Downloaded: %s (%s)\n", task.FileName, formatDownloadSize(task.FileSize))
	return nil
}

func printDownloadProgress(info downloader.ProgressInfo) {
	width := 40
	filled := int(info.Progress / 100 * float64(width))
	if filled > width {
		filled = width
	}
	bar := strings.Repeat("=", filled) + strings.Repeat(" ", width-filled)

	speed := info.Speed
	if speed == "" {
		speed = "..."
	}

	fmt.Printf("\r[%s] %.1f%% %s  ", bar, info.Progress, speed)
}

func formatDownloadSize(bytes int64) string {
	const KB = 1024
	const MB = KB * 1024
	const GB = MB * 1024

	if bytes >= GB {
		return fmt.Sprintf("%.2f GB", float64(bytes)/float64(GB))
	}
	if bytes >= MB {
		return fmt.Sprintf("%.2f MB", float64(bytes)/float64(MB))
	}
	if bytes >= KB {
		return fmt.Sprintf("%.2f KB", float64(bytes)/float64(KB))
	}
	return fmt.Sprintf("%d B", bytes)
}
