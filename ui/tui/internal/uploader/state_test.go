// Package uploader_test 上传状态持久化管理测试
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-19
// 版权: Copyright (c) 2026
package uploader

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

// TestStateManager_Save 测试保存上传状态
func TestStateManager_Save(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	state := &UploadState{
		UploadID:       "upload-123",
		FilePath:       "/home/user/test.bin",
		FileName:       "test.bin",
		FileSize:       15728640,
		FileHash:       "md5hash123",
		ParentID:       0,
		TotalChunks:    3,
		UploadedChunks: []int{0, 1},
		ChunkSize:      5242880,
		CreatedAt:      time.Now().Unix(),
		UpdatedAt:      time.Now().Unix(),
	}

	if err := mgr.Save(state); err != nil {
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
}

// TestStateManager_Load 测试加载上传状态
func TestStateManager_Load(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	// 保存测试数据
	state := &UploadState{
		UploadID:       "upload-123",
		FilePath:       "/home/user/test.bin",
		FileName:       "test.bin",
		FileSize:       15728640,
		FileHash:       "md5hash123",
		ParentID:       0,
		TotalChunks:    3,
		UploadedChunks: []int{0, 1},
		ChunkSize:      5242880,
		CreatedAt:      1708329600,
		UpdatedAt:      1708329900,
	}
	if err := mgr.Save(state); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	// 加载
	loaded, err := mgr.Load("/home/user/test.bin")
	if err != nil {
		t.Fatalf("Load() failed: %v", err)
	}

	if loaded == nil {
		t.Fatal("Load() = nil, want state")
	}

	if loaded.UploadID != state.UploadID {
		t.Errorf("UploadID = %q, want %q", loaded.UploadID, state.UploadID)
	}
	if loaded.FileName != state.FileName {
		t.Errorf("FileName = %q, want %q", loaded.FileName, state.FileName)
	}
	if loaded.FileSize != state.FileSize {
		t.Errorf("FileSize = %d, want %d", loaded.FileSize, state.FileSize)
	}
}

// TestStateManager_LoadNotExists 测试加载不存在的状态
func TestStateManager_LoadNotExists(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	// 加载不存在的文件路径
	loaded, err := mgr.Load("/nonexistent/file.bin")
	if err != nil {
		t.Fatalf("Load() failed: %v", err)
	}
	if loaded != nil {
		t.Errorf("Load() = %v, want nil", loaded)
	}
}

// TestStateManager_GetByUploadID 测试按上传 ID 获取状态
func TestStateManager_GetByUploadID(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	// 保存测试数据
	state := &UploadState{
		UploadID:       "upload-abc-123",
		FilePath:       "/home/user/data.zip",
		FileName:       "data.zip",
		FileSize:       10485760,
		FileHash:       "md5hash456",
		ParentID:       5,
		TotalChunks:    2,
		UploadedChunks: []int{0},
		ChunkSize:      5242880,
		CreatedAt:      1708329600,
		UpdatedAt:      1708329900,
	}
	if err := mgr.Save(state); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	// 按上传 ID 获取
	loaded, err := mgr.GetByUploadID("upload-abc-123")
	if err != nil {
		t.Fatalf("GetByUploadID() failed: %v", err)
	}

	if loaded == nil {
		t.Fatal("GetByUploadID() = nil, want state")
	}

	if loaded.UploadID != state.UploadID {
		t.Errorf("UploadID = %q, want %q", loaded.UploadID, state.UploadID)
	}
}

// TestStateManager_GetByUploadIDNotExists 测试获取不存在的上传 ID
func TestStateManager_GetByUploadIDNotExists(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	loaded, err := mgr.GetByUploadID("nonexistent-id")
	if err != nil {
		t.Fatalf("GetByUploadID() failed: %v", err)
	}
	if loaded != nil {
		t.Errorf("GetByUploadID() = %v, want nil", loaded)
	}
}

// TestStateManager_Delete 测试删除上传状态
func TestStateManager_Delete(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	// 保存测试数据
	state := &UploadState{
		UploadID:       "upload-del-123",
		FilePath:       "/home/user/delete.bin",
		FileName:       "delete.bin",
		FileSize:       1024,
		FileHash:       "hash-del",
		ParentID:       0,
		TotalChunks:    1,
		UploadedChunks: []int{0},
		ChunkSize:      5242880,
		CreatedAt:      1708329600,
		UpdatedAt:      1708329900,
	}
	if err := mgr.Save(state); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	// 删除
	if err := mgr.Delete("upload-del-123"); err != nil {
		t.Fatalf("Delete() failed: %v", err)
	}

	// 验证已删除
	loaded, err := mgr.GetByUploadID("upload-del-123")
	if err != nil {
		t.Fatalf("GetByUploadID() failed: %v", err)
	}
	if loaded != nil {
		t.Error("Delete() 后状态应该不存在")
	}
}

// TestStateManager_DeleteNotExists 测试删除不存在的状态
func TestStateManager_DeleteNotExists(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	// 删除不存在的 ID 不应该报错
	if err := mgr.Delete("nonexistent-id"); err != nil {
		t.Fatalf("Delete() 不存在的 ID 不应该报错: %v", err)
	}
}

// TestStateManager_List 测试列出所有上传状态
func TestStateManager_List(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	// 保存多个状态
	states := []*UploadState{
		{
			UploadID:       "upload-1",
			FilePath:       "/home/user/file1.bin",
			FileName:       "file1.bin",
			FileSize:       1024,
			FileHash:       "hash1",
			ParentID:       0,
			TotalChunks:    1,
			UploadedChunks: []int{0},
			ChunkSize:      5242880,
			CreatedAt:      1708329600,
			UpdatedAt:      1708329900,
		},
		{
			UploadID:       "upload-2",
			FilePath:       "/home/user/file2.bin",
			FileName:       "file2.bin",
			FileSize:       2048,
			FileHash:       "hash2",
			ParentID:       0,
			TotalChunks:    1,
			UploadedChunks: []int{0},
			ChunkSize:      5242880,
			CreatedAt:      1708329600,
			UpdatedAt:      1708329900,
		},
	}

	for _, s := range states {
		if err := mgr.Save(s); err != nil {
			t.Fatalf("Save() failed: %v", err)
		}
	}

	// 列出
	list, err := mgr.List()
	if err != nil {
		t.Fatalf("List() failed: %v", err)
	}

	if len(list) != 2 {
		t.Errorf("List() 返回 %d 条记录, want 2", len(list))
	}
}

// TestStateManager_ListEmpty 测试空列表
func TestStateManager_ListEmpty(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	// 文件不存在时应返回空列表
	list, err := mgr.List()
	if err != nil {
		t.Fatalf("List() failed: %v", err)
	}

	if len(list) != 0 {
		t.Errorf("List() 返回 %d 条记录, want 0", len(list))
	}
}

// TestStateManager_Update 测试更新上传状态
func TestStateManager_Update(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	// 保存初始状态
	state := &UploadState{
		UploadID:       "upload-update-123",
		FilePath:       "/home/user/update.bin",
		FileName:       "update.bin",
		FileSize:       1024,
		FileHash:       "hash-update",
		ParentID:       0,
		TotalChunks:    2,
		UploadedChunks: []int{0},
		ChunkSize:      5242880,
		CreatedAt:      1708329600,
		UpdatedAt:      1708329900,
	}
	if err := mgr.Save(state); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	// 更新分片
	state.UploadedChunks = []int{0, 1}
	state.UpdatedAt = 1708330200

	if err := mgr.Update(state); err != nil {
		t.Fatalf("Update() failed: %v", err)
	}

	// 验证更新
	loaded, err := mgr.GetByUploadID("upload-update-123")
	if err != nil {
		t.Fatalf("GetByUploadID() failed: %v", err)
	}

	if len(loaded.UploadedChunks) != 2 {
		t.Errorf("UploadedChunks 长度 = %d, want 2", len(loaded.UploadedChunks))
	}
}

// TestStateManager_UpdateNotExists 测试更新不存在的状态
func TestStateManager_UpdateNotExists(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	state := &UploadState{
		UploadID:       "nonexistent-update",
		FilePath:       "/home/user/nonexistent.bin",
		FileName:       "nonexistent.bin",
		FileSize:       1024,
		FileHash:       "hash",
		ParentID:       0,
		TotalChunks:    1,
		UploadedChunks: []int{0},
		ChunkSize:      5242880,
		CreatedAt:      1708329600,
		UpdatedAt:      1708329900,
	}

	err = mgr.Update(state)
	if err == nil {
		t.Error("Update() 不存在的状态应该返回错误")
	}
}

// TestStateManager_SaveExistingUpdates 测试保存已存在的状态会更新
func TestStateManager_SaveExistingUpdates(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	// 第一次保存
	state := &UploadState{
		UploadID:       "upload-dup-123",
		FilePath:       "/home/user/dup.bin",
		FileName:       "dup.bin",
		FileSize:       1024,
		FileHash:       "hash-dup",
		ParentID:       0,
		TotalChunks:    2,
		UploadedChunks: []int{0},
		ChunkSize:      5242880,
		CreatedAt:      1708329600,
		UpdatedAt:      1708329900,
	}
	if err := mgr.Save(state); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	// 更新并再次保存（相同 upload_id）
	state.UploadedChunks = []int{0, 1}
	state.UpdatedAt = 1708330200
	if err := mgr.Save(state); err != nil {
		t.Fatalf("Save() update failed: %v", err)
	}

	// 验证只有一条记录
	list, err := mgr.List()
	if err != nil {
		t.Fatalf("List() failed: %v", err)
	}
	if len(list) != 1 {
		t.Errorf("List() 返回 %d 条记录, want 1", len(list))
	}

	// 验证已更新
	if len(list[0].UploadedChunks) != 2 {
		t.Errorf("UploadedChunks 长度 = %d, want 2", len(list[0].UploadedChunks))
	}
}

// TestStateManager_DeleteLast 测试删除最后一条记录时删除文件
func TestStateManager_DeleteLast(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")
	mgr := &StateManager{filePath: filePath}

	// 保存一条记录
	state := &UploadState{
		UploadID:       "upload-last-123",
		FilePath:       "/home/user/last.bin",
		FileName:       "last.bin",
		FileSize:       1024,
		FileHash:       "hash-last",
		ParentID:       0,
		TotalChunks:    1,
		UploadedChunks: []int{0},
		ChunkSize:      5242880,
		CreatedAt:      1708329600,
		UpdatedAt:      1708329900,
	}
	if err := mgr.Save(state); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	// 删除最后一条
	if err := mgr.Delete("upload-last-123"); err != nil {
		t.Fatalf("Delete() failed: %v", err)
	}

	// 验证文件已删除
	if _, err := os.Stat(filePath); !os.IsNotExist(err) {
		t.Error("删除最后一条记录后文件应该被删除")
	}
}

// TestStateManager_CreatesDirectory 测试自动创建目录
func TestStateManager_CreatesDirectory(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	// 使用不存在的子目录
	filePath := filepath.Join(tmpDir, "subdir", "deep", "uploads.json")
	mgr := &StateManager{filePath: filePath}

	state := &UploadState{
		UploadID:       "upload-mkdir-123",
		FilePath:       "/home/user/mkdir.bin",
		FileName:       "mkdir.bin",
		FileSize:       1024,
		FileHash:       "hash-mkdir",
		ParentID:       0,
		TotalChunks:    1,
		UploadedChunks: []int{0},
		ChunkSize:      5242880,
		CreatedAt:      1708329600,
		UpdatedAt:      1708329900,
	}
	if err := mgr.Save(state); err != nil {
		t.Fatalf("Save() failed: %v", err)
	}

	// 验证目录已创建
	if _, err := os.Stat(filepath.Dir(filePath)); os.IsNotExist(err) {
		t.Error("目录应该被自动创建")
	}
}

// TestStateManager_InvalidJSON 测试加载无效 JSON 文件
func TestStateManager_InvalidJSON(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "state-test")
	if err != nil {
		t.Fatalf("创建临时目录失败: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	filePath := filepath.Join(tmpDir, "uploads.json")

	// 写入无效 JSON 内容
	if err := os.WriteFile(filePath, []byte("invalid json"), 0600); err != nil {
		t.Fatalf("WriteFile() failed: %v", err)
	}

	mgr := &StateManager{filePath: filePath}
	_, err = mgr.List()
	if err == nil {
		t.Error("List() with invalid JSON should fail")
	}
}

// TestNewStateManager 测试创建状态管理器
func TestNewStateManager(t *testing.T) {
	mgr := NewStateManager()
	if mgr == nil {
		t.Fatal("NewStateManager() = nil, want manager")
	}
	if mgr.filePath == "" {
		t.Error("filePath 不应为空")
	}
}
