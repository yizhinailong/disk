package store

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/sha256"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"golang.org/x/crypto/pbkdf2"
)

const (
	saltSize   = 32
	keySize    = 32 // AES-256
	iterations = 100000
)

// TokenData 令牌数据结构
type TokenData struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ExpiresAt    int64  `json:"expires_at"`
}

// TokenStore 令牌存储
type TokenStore struct {
	filePath string
	password []byte
}

// NewTokenStore 创建令牌存储
func NewTokenStore(filePath string, password string) *TokenStore {
	return &TokenStore{
		filePath: filePath,
		password: []byte(password),
	}
}

// Save 保存令牌（加密）
func (s *TokenStore) Save(data *TokenData) error {
	// 序列化
	plaintext, err := json.Marshal(data)
	if err != nil {
		return fmt.Errorf("序列化令牌失败: %w", err)
	}

	// 生成随机盐
	salt := make([]byte, saltSize)
	if _, err := rand.Read(salt); err != nil {
		return fmt.Errorf("生成盐失败: %w", err)
	}

	// 派生密钥
	key := pbkdf2.Key(s.password, salt, iterations, keySize, sha256.New)

	// 加密
	block, err := aes.NewCipher(key)
	if err != nil {
		return fmt.Errorf("创建加密器失败: %w", err)
	}

	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return fmt.Errorf("创建 GCM 失败: %w", err)
	}

	nonce := make([]byte, gcm.NonceSize())
	if _, err := rand.Read(nonce); err != nil {
		return fmt.Errorf("生成 nonce 失败: %w", err)
	}

	ciphertext := gcm.Seal(nil, nonce, plaintext, nil)

	// 组合: salt + nonce + ciphertext
	result := make([]byte, 0, len(salt)+len(nonce)+len(ciphertext))
	result = append(result, salt...)
	result = append(result, nonce...)
	result = append(result, ciphertext...)

	// 确保目录存在
	dir := filepath.Dir(s.filePath)
	if err := os.MkdirAll(dir, 0700); err != nil {
		return fmt.Errorf("创建目录失败: %w", err)
	}

	// 写入文件（权限 600）
	if err := os.WriteFile(s.filePath, result, 0600); err != nil {
		return fmt.Errorf("写入文件失败: %w", err)
	}

	return nil
}

// Load 加载令牌（解密）
func (s *TokenStore) Load() (*TokenData, error) {
	ciphertext, err := os.ReadFile(s.filePath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil // 文件不存在，返回 nil（未登录）
		}
		return nil, fmt.Errorf("读取文件失败: %w", err)
	}

	if len(ciphertext) < saltSize {
		return nil, fmt.Errorf("文件格式无效")
	}

	// 解析
	salt := ciphertext[:saltSize]
	remaining := ciphertext[saltSize:]

	// 派生密钥
	key := pbkdf2.Key(s.password, salt, iterations, keySize, sha256.New)

	// 解密
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, fmt.Errorf("创建解密器失败: %w", err)
	}

	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, fmt.Errorf("创建 GCM 失败: %w", err)
	}

	nonceSize := gcm.NonceSize()
	if len(remaining) < nonceSize {
		return nil, fmt.Errorf("文件格式无效")
	}

	nonce := remaining[:nonceSize]
	ciphertext = remaining[nonceSize:]

	plaintext, err := gcm.Open(nil, nonce, ciphertext, nil)
	if err != nil {
		return nil, fmt.Errorf("解密失败: %w", err)
	}

	// 反序列化
	var data TokenData
	if err := json.Unmarshal(plaintext, &data); err != nil {
		return nil, fmt.Errorf("反序列化失败: %w", err)
	}

	return &data, nil
}

// Delete 删除令牌文件
func (s *TokenStore) Delete() error {
	if err := os.Remove(s.filePath); err != nil && !os.IsNotExist(err) {
		return err
	}
	return nil
}

// Exists 检查令牌文件是否存在
func (s *TokenStore) Exists() bool {
	_, err := os.Stat(s.filePath)
	return err == nil
}
