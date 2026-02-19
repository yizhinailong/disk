// Package uploader 上传状态持久化管理
//
// 提供上传状态的本地持久化存储，支持断点续传。
// 状态文件存储在 ~/.disk-tui/uploads.json。
//
// 作者: LiuFeng (liufeng.code@outlook.com)
// 日期: 2026-02-19
// 版权: Copyright (c) 2026
package uploader

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

// UploadState 上传状态
//
// 记录文件上传的完整状态，用于断点续传。
type UploadState struct {
	UploadID       string `json:"upload_id"`       // 服务端上传 ID
	FilePath       string `json:"file_path"`       // 本地文件路径
	FileName       string `json:"file_name"`       // 文件名
	FileSize       int64  `json:"file_size"`       // 文件大小（字节）
	FileHash       string `json:"file_hash"`       // 文件 MD5 哈希
	ParentID       uint64 `json:"parent_id"`       // 目标文件夹 ID
	TotalChunks    int    `json:"total_chunks"`    // 总分片数
	UploadedChunks []int  `json:"uploaded_chunks"` // 已上传分片索引列表
	ChunkSize      int    `json:"chunk_size"`      // 分片大小
	CreatedAt      int64  `json:"created_at"`      // 创建时间（Unix 时间戳）
	UpdatedAt      int64  `json:"updated_at"`      // 更新时间（Unix 时间戳）
}

// stateFile 状态文件结构
type stateFile struct {
	Uploads []*UploadState `json:"uploads"`
}

// StateManager 状态管理器
//
// 管理上传状态的持久化存储。
type StateManager struct {
	filePath string // 状态文件路径
}

// NewStateManager 创建状态管理器
//
// 状态文件默认存储在 ~/.disk-tui/uploads.json
//
// 返回:
//   - *StateManager: 状态管理器实例
func NewStateManager() *StateManager {
	return &StateManager{
		filePath: getStateFilePath(),
	}
}

// getStateFilePath 获取状态文件路径
//
// 返回:
//   - string: 状态文件完整路径
func getStateFilePath() string {
	home, err := os.UserHomeDir()
	if err != nil {
		home = "."
	}
	return filepath.Join(home, ".disk-tui", "uploads.json")
}

// Save 保存上传状态
//
// 将上传状态保存到状态文件。如果状态已存在（相同 upload_id），则更新。
//
// 参数:
//   - state: 上传状态
//
// 返回:
//   - error: 错误信息
func (m *StateManager) Save(state *UploadState) error {
	// 读取现有状态
	states, err := m.loadAll()
	if err != nil {
		return err
	}

	// 检查是否已存在，存在则更新
	found := false
	for i, s := range states {
		if s.UploadID == state.UploadID {
			states[i] = state
			found = true
			break
		}
	}

	// 不存在则添加
	if !found {
		states = append(states, state)
	}

	return m.saveAll(states)
}

// Load 按文件路径加载上传状态
//
// 从状态文件中查找指定文件的上传状态。
// 如果文件不存在或未找到状态，返回 nil。
//
// 参数:
//   - filePath: 本地文件路径
//
// 返回:
//   - *UploadState: 上传状态（未找到时为 nil）
//   - error: 错误信息
func (m *StateManager) Load(filePath string) (*UploadState, error) {
	states, err := m.loadAll()
	if err != nil {
		return nil, err
	}

	for _, state := range states {
		if state.FilePath == filePath {
			return state, nil
		}
	}

	return nil, nil
}

// GetByUploadID 按上传 ID 获取上传状态
//
// 从状态文件中查找指定上传 ID 的状态。
// 如果未找到状态，返回 nil。
//
// 参数:
//   - uploadID: 上传 ID
//
// 返回:
//   - *UploadState: 上传状态（未找到时为 nil）
//   - error: 错误信息
func (m *StateManager) GetByUploadID(uploadID string) (*UploadState, error) {
	states, err := m.loadAll()
	if err != nil {
		return nil, err
	}

	for _, state := range states {
		if state.UploadID == uploadID {
			return state, nil
		}
	}

	return nil, nil
}

// Delete 删除上传状态
//
// 从状态文件中删除指定上传 ID 的状态。
// 如果状态不存在，不返回错误。
//
// 参数:
//   - uploadID: 上传 ID
//
// 返回:
//   - error: 错误信息
func (m *StateManager) Delete(uploadID string) error {
	states, err := m.loadAll()
	if err != nil {
		return err
	}

	// 过滤掉要删除的状态
	newStates := make([]*UploadState, 0, len(states))
	for _, state := range states {
		if state.UploadID != uploadID {
			newStates = append(newStates, state)
		}
	}

	// 如果没有变化，直接返回
	if len(newStates) == len(states) {
		return nil
	}

	// 如果全部删除，删除文件
	if len(newStates) == 0 {
		if err := os.Remove(m.filePath); err != nil && !os.IsNotExist(err) {
			return fmt.Errorf("删除状态文件失败: %w", err)
		}
		return nil
	}

	return m.saveAll(newStates)
}

// List 列出所有上传状态
//
// 返回状态文件中的所有上传状态。
//
// 返回:
//   - []*UploadState: 上传状态列表
//   - error: 错误信息
func (m *StateManager) List() ([]*UploadState, error) {
	return m.loadAll()
}

// Update 更新上传状态
//
// 更新已存在的上传状态。如果状态不存在，返回错误。
//
// 参数:
//   - state: 上传状态
//
// 返回:
//   - error: 错误信息
func (m *StateManager) Update(state *UploadState) error {
	states, err := m.loadAll()
	if err != nil {
		return err
	}

	// 查找并更新
	found := false
	for i, s := range states {
		if s.UploadID == state.UploadID {
			states[i] = state
			found = true
			break
		}
	}

	if !found {
		return fmt.Errorf("上传状态不存在: %s", state.UploadID)
	}

	return m.saveAll(states)
}

// loadAll 加载所有上传状态
//
// 从状态文件读取所有上传状态。如果文件不存在，返回空列表。
//
// 返回:
//   - []*UploadState: 上传状态列表
//   - error: 错误信息
func (m *StateManager) loadAll() ([]*UploadState, error) {
	data, err := os.ReadFile(m.filePath)
	if err != nil {
		if os.IsNotExist(err) {
			return []*UploadState{}, nil
		}
		return nil, fmt.Errorf("读取状态文件失败: %w", err)
	}

	var sf stateFile
	if err := json.Unmarshal(data, &sf); err != nil {
		return nil, fmt.Errorf("解析状态文件失败: %w", err)
	}

	return sf.Uploads, nil
}

// saveAll 保存所有上传状态
//
// 将所有上传状态保存到状态文件。
//
// 参数:
//   - states: 上传状态列表
//
// 返回:
//   - error: 错误信息
func (m *StateManager) saveAll(states []*UploadState) error {
	sf := stateFile{Uploads: states}

	data, err := json.MarshalIndent(sf, "", "  ")
	if err != nil {
		return fmt.Errorf("序列化状态失败: %w", err)
	}

	// 确保目录存在
	dir := filepath.Dir(m.filePath)
	if err := os.MkdirAll(dir, 0700); err != nil {
		return fmt.Errorf("创建目录失败: %w", err)
	}

	// 写入文件（权限 600）
	if err := os.WriteFile(m.filePath, data, 0600); err != nil {
		return fmt.Errorf("写入状态文件失败: %w", err)
	}

	return nil
}
