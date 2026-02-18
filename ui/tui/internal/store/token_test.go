// Package store_test Token 存储模块测试
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-18
// 版权: Copyright (c) 2026
package store

import (
	"os"
	"path/filepath"
	"testing"
)

// TestTokenStore_SaveAndLoad 测试令牌保存和加载
func TestTokenStore_SaveAndLoad(t *testing.T) {
	// 创建临时目录
	tmpDir, err := os.MkdirTemp("", "token-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "token.enc")
	store := NewTokenStore(filePath)

	// 保存
	data := &TokenData{
		AccessToken:  "access-token-123",
		RefreshToken: "refresh-token-456",
		ExpiresAt:    1234567890,
	}

	if err := store.Save(data); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	// 检查文件权限
	info, err := os.Stat(filePath)
	if err != nil {
		t.Fatalf("Stat() failed: %v", err)
	}
	if info.Mode().Perm() != 0600 {
		t.Errorf("文件权限 = %v, want 0600", info.Mode().Perm())
	}

	// 加载
	loaded, err := store.Load()
	if err != nil {
		t.Fatalf("Load() failed: %v", err)
	}

	if loaded.AccessToken != data.AccessToken {
		t.Errorf("AccessToken = %q, want %q", loaded.AccessToken, data.AccessToken)
	}
	if loaded.RefreshToken != data.RefreshToken {
		t.Errorf("RefreshToken = %q, want %q", loaded.RefreshToken, data.RefreshToken)
	}
	if loaded.ExpiresAt != data.ExpiresAt {
		t.Errorf("ExpiresAt = %d, want %d", loaded.ExpiresAt, data.ExpiresAt)
	}
}

// TestTokenStore_LoadNotExists 测试加载不存在的令牌文件
func TestTokenStore_LoadNotExists(t *testing.T) {
	store := NewTokenStore("/nonexistent/path/token.enc")

	data, err := store.Load()
	if err != nil {
		t.Fatalf("Load() failed: %v", err)
	}
	if data != nil {
		t.Errorf("Load() = %v, want nil", data)
	}
}

// TestTokenStore_Delete 测试删除令牌文件
func TestTokenStore_Delete(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "token-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "token.enc")
	store := NewTokenStore(filePath)

	// 保存
	data := &TokenData{AccessToken: "test"}
	if err := store.Save(data); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	// 删除
	if err := store.Delete(); err != nil {
		t.Fatalf("Delete() failed: %v", err)
	}

	// 检查已删除
	if store.Exists() {
		t.Error("Exists() = true, want false")
	}
}

// TestTokenStore_Exists 测试检查令牌文件是否存在
func TestTokenStore_Exists(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "token-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "token.enc")
	store := NewTokenStore(filePath)

	// 初始不存在
	if store.Exists() {
		t.Error("Exists() = true, want false (file not created)")
	}

	// 保存后存在
	data := &TokenData{AccessToken: "test"}
	if err := store.Save(data); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	if !store.Exists() {
		t.Error("Exists() = false, want true (file created)")
	}
}

// TestTokenStore_CreatesDirectory 测试自动创建目录
func TestTokenStore_CreatesDirectory(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "token-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	// 使用不存在的子目录
	filePath := filepath.Join(tmpDir, "subdir", "deep", "token.enc")
	store := NewTokenStore(filePath)

	data := &TokenData{AccessToken: "test"}
	if err := store.Save(data); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	// 验证目录已创建
	if _, err := os.Stat(filepath.Dir(filePath)); os.IsNotExist(err) {
		t.Error("目录应该被自动创建")
	}
}

// TestTokenStore_LoadInvalidJSON 测试加载无效 JSON 文件
func TestTokenStore_LoadInvalidJSON(t *testing.T) {
	// 创建临时目录
	tmpDir, err := os.MkdirTemp("", "token-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "token.enc")

	// 写入无效 JSON 内容
	if err := os.WriteFile(filePath, []byte("invalid json"), 0600); err != nil {
		t.Fatalf("WriteFile() failed: %v", err)
	}

	store := NewTokenStore(filePath)
	_, err = store.Load()
	if err == nil {
		t.Error("Load() with invalid JSON should fail")
	}
}

// TestTokenStore_LoadEmptyFile 测试加载空文件
func TestTokenStore_LoadEmptyFile(t *testing.T) {
	// 创建临时目录
	tmpDir, err := os.MkdirTemp("", "token-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "token.enc")

	// 写入空文件
	if err := os.WriteFile(filePath, []byte(""), 0600); err != nil {
		t.Fatalf("WriteFile() failed: %v", err)
	}

	store := NewTokenStore(filePath)
	_, err = store.Load()
	if err == nil {
		t.Error("Load() with empty file should fail")
	}
}
