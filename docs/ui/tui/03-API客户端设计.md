# TUI API 客户端设计

## 1. 设计概述

### 1.1 设计目标

本文档定义 TUI（终端用户界面）客户端与后端 API 交互的设计规范，主要目标包括：

1. **统一的 HTTP 客户端封装** - 提供一致的请求/响应处理机制
2. **无缝的 JWT 认证流程** - 自动处理令牌刷新和过期
3. **健壮的错误处理** - 将后端错误码映射为用户友好的提示
4. **高效的缓存策略** - 减少重复请求，提升响应速度
5. **灵活的配置管理** - 支持 CLI、环境变量、配置文件多源配置

### 1.2 架构原则

| 原则 | 说明 |
|------|------|
| **分层设计** | HTTP 客户端 → 认证层 → API 模块 → UI 层，职责清晰 |
| **依赖注入** | 通过接口抽象，便于测试和替换实现 |
| **错误传播** - 使用 Go 的 error 模式，统一错误处理 |
| **并发安全** - Token 存储和缓存操作需保证线程安全 |
| **可观测性** - 支持请求日志和追踪 ID |

---

## 2. HTTP 客户端封装

### 2.1 基础客户端设计

```go
// pkg/httpclient/client.go
package httpclient

import (
    "context"
    "net/http"
    "time"
)

// Client 封装 HTTP 客户端
type Client struct {
    baseURL    string
    httpClient *http.Client
    tokenMgr   TokenManager
    retryMax   int
    timeout    time.Duration
}

// ClientOption 客户端配置选项
type ClientOption func(*Client)

// NewClient 创建新的 HTTP 客户端
func NewClient(baseURL string, opts ...ClientOption) *Client {
    c := &Client{
        baseURL: baseURL,
        httpClient: &http.Client{
            Timeout: 30 * time.Second,
            Transport: &http.Transport{
                MaxIdleConns:        100,
                MaxIdleConnsPerHost: 10,
                IdleConnTimeout:     90 * time.Second,
            },
        },
        retryMax: 3,
        timeout:  30 * time.Second,
    }

    for _, opt := range opts {
        opt(c)
    }

    return c
}

// WithTimeout 设置请求超时
func WithTimeout(timeout time.Duration) ClientOption {
    return func(c *Client) {
        c.timeout = timeout
        c.httpClient.Timeout = timeout
    }
}

// WithRetry 设置重试次数
func WithRetry(max int) ClientOption {
    return func(c *Client) {
        c.retryMax = max
    }
}

// WithTokenManager 设置令牌管理器
func WithTokenManager(tm TokenManager) ClientOption {
    return func(c *Client) {
        c.tokenMgr = tm
    }
}
```

### 2.2 请求/响应处理

```go
// pkg/httpclient/request.go
package httpclient

import (
    "bytes"
    "context"
    "encoding/json"
    "fmt"
    "io"
    "net/http"
)

// Request 请求构建器
type Request struct {
    client   *Client
    method   string
    path     string
    body     interface{}
    headers  map[string]string
    query    map[string]string
    ctx      context.Context
    noAuth   bool // 是否跳过认证
}

// APIResponse 标准响应结构
type APIResponse struct {
    Code    int             `json:"code"`
    Message string          `json:"message"`
    Data    json.RawMessage `json:"data"`
}

// NewRequest 创建新请求
func (c *Client) NewRequest(ctx context.Context, method, path string) *Request {
    return &Request{
        client:  c,
        method:  method,
        path:    path,
        headers: make(map[string]string),
        query:   make(map[string]string),
        ctx:     ctx,
    }
}

// WithJSON 设置 JSON 请求体
func (r *Request) WithJSON(body interface{}) *Request {
    r.body = body
    r.headers["Content-Type"] = "application/json"
    return r
}

// WithQuery 设置查询参数
func (r *Request) WithQuery(key, value string) *Request {
    r.query[key] = value
    return r
}

// WithHeader 设置请求头
func (r *Request) WithHeader(key, value string) *Request {
    r.headers[key] = value
    return r
}

// WithoutAuth 跳过认证
func (r *Request) WithoutAuth() *Request {
    r.noAuth = true
    return r
}

// Do 执行请求
func (r *Request) Do() (*http.Response, error) {
    // 构建完整 URL
    url := r.client.baseURL + r.path

    // 序列化请求体
    var bodyReader io.Reader
    if r.body != nil {
        bodyBytes, err := json.Marshal(r.body)
        if err != nil {
            return nil, fmt.Errorf("序列化请求体失败: %w", err)
        }
        bodyReader = bytes.NewReader(bodyBytes)
    }

    // 创建 HTTP 请求
    req, err := http.NewRequestWithContext(r.ctx, r.method, url, bodyReader)
    if err != nil {
        return nil, fmt.Errorf("创建请求失败: %w", err)
    }

    // 设置请求头
    for key, value := range r.headers {
        req.Header.Set(key, value)
    }

    // 添加认证令牌
    if !r.noAuth && r.client.tokenMgr != nil {
        token, err := r.client.tokenMgr.GetValidToken(r.ctx)
        if err != nil {
            return nil, fmt.Errorf("获取认证令牌失败: %w", err)
        }
        req.Header.Set("Authorization", "Bearer "+token)
    }

    // 设置追踪 ID
    req.Header.Set("X-Request-ID", generateRequestID())

    // 添加查询参数
    if len(r.query) > 0 {
        q := req.URL.Query()
        for key, value := range r.query {
            q.Add(key, value)
        }
        req.URL.RawQuery = q.Encode()
    }

    return r.client.httpClient.Do(req)
}

// DoJSON 执行请求并解析 JSON 响应
func (r *Request) DoJSON(v interface{}) error {
    resp, err := r.Do()
    if err != nil {
        return err
    }
    defer resp.Body.Close()

    body, err := io.ReadAll(resp.Body)
    if err != nil {
        return fmt.Errorf("读取响应体失败: %w", err)
    }

    var apiResp APIResponse
    if err := json.Unmarshal(body, &apiResp); err != nil {
        return fmt.Errorf("解析响应失败: %w", err)
    }

    // 检查业务错误码
    if apiResp.Code != 0 {
        return &APIError{
            Code:    apiResp.Code,
            Message: apiResp.Message,
            HTTPStatus: resp.StatusCode,
        }
    }

    // 解析数据
    if v != nil && len(apiResp.Data) > 0 {
        if err := json.Unmarshal(apiResp.Data, v); err != nil {
            return fmt.Errorf("解析数据失败: %w", err)
        }
    }

    return nil
}

// APIError API 错误
type APIError struct {
    Code       int
    Message    string
    HTTPStatus int
    Data       interface{}
}

func (e *APIError) Error() string {
    return fmt.Sprintf("[%d] %s", e.Code, e.Message)
}
```

### 2.3 超时和重试策略

```go
// pkg/httpclient/retry.go
package httpclient

import (
    "context"
    "math/rand"
    "net/http"
    "time"
)

// RetryableError 可重试的错误
type RetryableError interface {
    Retryable() bool
}

// DoWithRetry 带重试的请求执行
func (r *Request) DoWithRetry() (*http.Response, error) {
    var lastErr error

    for attempt := 0; attempt <= r.client.retryMax; attempt++ {
        if attempt > 0 {
            // 指数退避 + 随机抖动
            backoff := time.Duration(attempt*attempt) * 100 * time.Millisecond
            jitter := time.Duration(rand.Intn(100)) * time.Millisecond
            time.Sleep(backoff + jitter)
        }

        resp, err := r.Do()
        if err == nil {
            return resp, nil
        }

        lastErr = err

        // 检查是否可重试
        if !isRetryable(err) {
            break
        }

        // 检查上下文是否已取消
        if r.ctx.Err() != nil {
            return nil, r.ctx.Err()
        }
    }

    return nil, lastErr
}

// isRetryable 判断错误是否可重试
func isRetryable(err error) bool {
    // 网络错误可重试
    if _, ok := err.(interface{ Timeout() bool }); ok {
        return true
    }

    // API 错误检查
    if apiErr, ok := err.(*APIError); ok {
        // 5xx 服务器错误可重试
        if apiErr.HTTPStatus >= 500 {
            return true
        }
        // 429 限流可重试
        if apiErr.HTTPStatus == http.StatusTooManyRequests {
            return true
        }
    }

    return false
}

// RetryableTransport 可重试的 Transport
type RetryableTransport struct {
    transport http.RoundTripper
    maxRetry  int
}

func (t *RetryableTransport) RoundTrip(req *http.Request) (*http.Response, error) {
    var lastErr error

    for attempt := 0; attempt <= t.maxRetry; attempt++ {
        if attempt > 0 {
            time.Sleep(time.Duration(attempt*attempt) * 100 * time.Millisecond)
        }

        resp, err := t.transport.RoundTrip(req)
        if err == nil && resp.StatusCode < 500 {
            return resp, nil
        }

        if err != nil {
            lastErr = err
        } else {
            resp.Body.Close()
            lastErr = &APIError{HTTPStatus: resp.StatusCode}
        }
    }

    return nil, lastErr
}
```

---

## 3. JWT 认证流程

### 3.1 令牌结构说明

后端使用 JWT (JSON Web Token) 进行身份认证，令牌结构如下：

```
┌─────────────────────────────────────────────────────────────────┐
│                         JWT 令牌结构                             │
├─────────────────────────────────────────────────────────────────┤
│  Header (Base64)                                                 │
│  {                                                               │
│    "alg": "HS256",                                               │
│    "typ": "JWT"                                                  │
│  }                                                               │
├─────────────────────────────────────────────────────────────────┤
│  Payload (Base64)                                                │
│  {                                                               │
│    "iss": "disk",              // 签发者                          │
│    "sub": "12345",             // 用户 ID                         │
│    "iat": 1705363200,          // 签发时间                        │
│    "exp": 1705370400,          // 过期时间                        │
│    "type": "access|refresh",   // 令牌类型                        │
│    "username": "john_doe",     // 用户名 (仅 Access Token)        │
│    "jti": "uuid-xxxx"          // 令牌唯一标识                    │
│  }                                                               │
├─────────────────────────────────────────────────────────────────┤
│  Signature (HMAC-SHA256)                                         │
│  HMACSHA256(base64(header) + "." + base64(payload), secret)      │
└─────────────────────────────────────────────────────────────────┘
```

**令牌类型**：

| 类型 | 有效期 | 用途 | 存储 |
|------|--------|------|------|
| Access Token | 2 小时 | API 认证 | 内存 |
| Refresh Token | 7 天 | 刷新 Access Token | 加密文件 |
| Share Token | 1 小时 | 分享访问 | 不存储 |

### 3.2 登录流程

```
┌──────────────────────────────────────────────────────────────────┐
│                          用户登录流程                             │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  用户输入账号密码                                                 │
│         │                                                        │
│         ▼                                                        │
│  ┌─────────────────┐                                             │
│  │ POST /api/auth/ │                                             │
│  │     login       │                                             │
│  └────────┬────────┘                                             │
│           │                                                      │
│           ▼                                                      │
│  ┌─────────────────┐    失败     ┌─────────────────┐             │
│  │   验证凭证      │────────────→│ 显示错误信息     │             │
│  └────────┬────────┘             └─────────────────┘             │
│           │ 成功                                                  │
│           ▼                                                      │
│  ┌─────────────────────────────────────┐                         │
│  │ 保存令牌:                            │                         │
│  │ • Access Token → 内存                │                         │
│  │ • Refresh Token → 加密文件存储        │                         │
│  │ • User Info → 内存缓存               │                         │
│  └─────────────────────────────────────┘                         │
│           │                                                      │
│           ▼                                                      │
│  ┌─────────────────┐                                             │
│  │   进入主界面     │                                             │
│  └─────────────────┘                                             │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 3.3 令牌刷新流程

```
┌─────────────────────────────────────────────────────────────────┐
│                     JWT Token 刷新流程                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. API 请求前检查 Access Token 有效期                           │
│     └─→ 剩余时间 < 5分钟？触发刷新                               │
│                                                                 │
│  2. 刷新请求                                                    │
│     POST /api/auth/refresh                                      │
│     Body: { "refresh_token": "xxx" }                            │
│                                                                 │
│  3. 响应处理                                                    │
│     ├─→ 成功：更新内存和文件中的 Token                           │
│     └─→ 失败：跳转登录页面                                      │
│                                                                 │
│  4. 错误处理                                                    │
│     ├─→ 40110 RefreshTokenAlreadyUsed → 清除凭证 → 跳转登录      │
│     ├─→ 40108 TokenExpired → 清除凭证 → 跳转登录                 │
│     └─→ 网络错误 → 保持当前状态 → 下次请求重试                   │
│                                                                 │
│  ⚠️ 关键点：Refresh Token 单次使用，刷新后旧 Token 立即失效       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**详细流程图**：

```
┌──────────────┐      ┌──────────────┐      ┌──────────────┐
│    UI 层     │      │  TokenMgr    │      │   后端 API   │
└──────┬───────┘      └──────┬───────┘      └──────┬───────┘
       │                     │                     │
       │  API 请求           │                     │
       │────────────────────>│                     │
       │                     │                     │
       │                     │ 检查 Access Token   │
       │                     │ 剩余时间            │
       │                     │                     │
       │                     │ [剩余 < 5分钟]      │
       │                     │                     │
       │                     │ POST /auth/refresh  │
       │                     │────────────────────>│
       │                     │                     │
       │                     │                     │ 验证 Refresh Token
       │                     │                     │ 生成新令牌对
       │                     │                     │ 作废旧 Refresh Token
       │                     │                     │
       │                     │ 新 Token 对         │
       │                     │<────────────────────│
       │                     │                     │
       │                     │ 更新内存            │
       │                     │ 更新文件            │
       │                     │                     │
       │                     │ 返回有效 Token      │
       │<────────────────────│                     │
       │                     │                     │
       │ 带认证的请求        │                     │
       │────────────────────────────────────────────>│
       │                     │                     │
```

### 3.4 登出流程

```go
// Logout 登出流程
func (tm *TokenManager) Logout(ctx context.Context) error {
    // 1. 调用后端登出接口
    if tm.accessToken != "" {
        req := tm.client.NewRequest(ctx, http.MethodPost, "/api/auth/logout")
        if err := req.DoJSON(nil); err != nil {
            // 即使失败也继续清理本地凭证
            log.Warnf("登出接口调用失败: %v", err)
        }
    }

    // 2. 清除内存中的令牌
    tm.accessToken = ""
    tm.accessTokenExpire = time.Time{}
    tm.userInfo = nil

    // 3. 删除加密文件中的 Refresh Token
    if err := tm.storage.Delete(); err != nil {
        log.Warnf("删除令牌文件失败: %v", err)
    }

    // 4. 清除缓存
    tm.cache.Clear()

    return nil
}
```

### 3.5 令牌存储（加密文件方案）

```go
// pkg/auth/token_storage.go
package auth

import (
    "crypto/aes"
    "crypto/cipher"
    "crypto/rand"
    "crypto/sha256"
    "encoding/json"
    "os"
    "path/filepath"

    "golang.org/x/crypto/pbkdf2"
)

const (
    saltSize   = 32
    keySize    = 32 // AES-256
    iterations = 100000
    nonceSize  = 12 // GCM nonce
)

// TokenStorage 令牌加密存储
type TokenStorage struct {
    filePath  string
    masterKey []byte // 从用户密码派生
}

// StoredTokens 存储的令牌结构
type StoredTokens struct {
    RefreshToken string    `json:"refresh_token"`
    UserID       uint64    `json:"user_id"`
    Username     string    `json:"username"`
    CreatedAt    time.Time `json:"created_at"`
}

// NewTokenStorage 创建令牌存储
func NewTokenStorage(configDir string, masterPassword string) *TokenStorage {
    filePath := filepath.Join(configDir, ".tokens.enc")

    // 从主密码派生加密密钥
    salt := []byte("disk-tui-token-storage-salt-v1")
    key := pbkdf2.Key([]byte(masterPassword), salt, iterations, keySize, sha256.New)

    return &TokenStorage{
        filePath:  filePath,
        masterKey: key,
    }
}

// Save 保存令牌（AES-GCM 加密）
func (s *TokenStorage) Save(tokens *StoredTokens) error {
    // 序列化
    plaintext, err := json.Marshal(tokens)
    if err != nil {
        return fmt.Errorf("序列化失败: %w", err)
    }

    // 生成随机 nonce
    nonce := make([]byte, nonceSize)
    if _, err := rand.Read(nonce); err != nil {
        return fmt.Errorf("生成 nonce 失败: %w", err)
    }

    // AES-GCM 加密
    block, err := aes.NewCipher(s.masterKey)
    if err != nil {
        return fmt.Errorf("创建加密块失败: %w", err)
    }

    gcm, err := cipher.NewGCM(block)
    if err != nil {
        return fmt.Errorf("创建 GCM 失败: %w", err)
    }

    ciphertext := gcm.Seal(nil, nonce, plaintext, nil)

    // 组合: nonce + ciphertext
    data := append(nonce, ciphertext...)

    // 写入文件（权限 0600）
    if err := os.WriteFile(s.filePath, data, 0600); err != nil {
        return fmt.Errorf("写入文件失败: %w", err)
    }

    return nil
}

// Load 加载令牌
func (s *TokenStorage) Load() (*StoredTokens, error) {
    data, err := os.ReadFile(s.filePath)
    if err != nil {
        if os.IsNotExist(err) {
            return nil, nil // 文件不存在是正常情况
        }
        return nil, fmt.Errorf("读取文件失败: %w", err)
    }

    if len(data) < nonceSize {
        return nil, fmt.Errorf("文件内容无效")
    }

    // 分离 nonce 和 ciphertext
    nonce := data[:nonceSize]
    ciphertext := data[nonceSize:]

    // AES-GCM 解密
    block, err := aes.NewCipher(s.masterKey)
    if err != nil {
        return nil, fmt.Errorf("创建加密块失败: %w", err)
    }

    gcm, err := cipher.NewGCM(block)
    if err != nil {
        return nil, fmt.Errorf("创建 GCM 失败: %w", err)
    }

    plaintext, err := gcm.Open(nil, nonce, ciphertext, nil)
    if err != nil {
        return nil, fmt.Errorf("解密失败: %w", err)
    }

    var tokens StoredTokens
    if err := json.Unmarshal(plaintext, &tokens); err != nil {
        return nil, fmt.Errorf("反序列化失败: %w", err)
    }

    return &tokens, nil
}

// Delete 删除令牌文件
func (s *TokenStorage) Delete() error {
    return os.Remove(s.filePath)
}

// Exists 检查令牌文件是否存在
func (s *TokenStorage) Exists() bool {
    _, err := os.Stat(s.filePath)
    return err == nil
}
```

---

## 4. API 模块设计

### 4.1 认证 API (auth.go)

```go
// pkg/api/auth.go
package api

import (
    "context"
    "net/http"
)

// AuthAPI 认证相关 API
type AuthAPI struct {
    client *httpclient.Client
}

// RegisterRequest 注册请求
type RegisterRequest struct {
    Username string `json:"username"`
    Email    string `json:"email"`
    Password string `json:"password"`
}

// LoginRequest 登录请求
type LoginRequest struct {
    Account  string `json:"account"`  // 用户名或邮箱
    Password string `json:"password"`
}

// LoginResponse 登录响应
type LoginResponse struct {
    AccessToken  string    `json:"access_token"`
    RefreshToken string    `json:"refresh_token"`
    TokenType    string    `json:"token_type"`
    ExpiresIn    int       `json:"expires_in"`
    User         *UserInfo `json:"user"`
}

// RefreshRequest 刷新令牌请求
type RefreshRequest struct {
    RefreshToken string `json:"refresh_token"`
}

// RefreshResponse 刷新令牌响应
type RefreshResponse struct {
    AccessToken  string `json:"access_token"`
    RefreshToken string `json:"refresh_token"`
    ExpiresIn    int    `json:"expires_in"`
}

// Register 用户注册
func (a *AuthAPI) Register(ctx context.Context, req *RegisterRequest) (*UserInfo, error) {
    var resp struct {
        User *UserInfo `json:"user"`
    }

    err := a.client.NewRequest(ctx, http.MethodPost, "/api/auth/register").
        WithJSON(req).
        WithoutAuth().
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return resp.User, nil
}

// Login 用户登录
func (a *AuthAPI) Login(ctx context.Context, req *LoginRequest) (*LoginResponse, error) {
    var resp LoginResponse

    err := a.client.NewRequest(ctx, http.MethodPost, "/api/auth/login").
        WithJSON(req).
        WithoutAuth().
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return &resp, nil
}

// Refresh 刷新令牌
func (a *AuthAPI) Refresh(ctx context.Context, refreshToken string) (*RefreshResponse, error) {
    var resp RefreshResponse

    err := a.client.NewRequest(ctx, http.MethodPost, "/api/auth/refresh").
        WithJSON(&RefreshRequest{RefreshToken: refreshToken}).
        WithoutAuth().
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return &resp, nil
}

// Logout 用户登出
func (a *AuthAPI) Logout(ctx context.Context) error {
    return a.client.NewRequest(ctx, http.MethodPost, "/api/auth/logout").
        DoJSON(nil)
}
```

### 4.2 用户 API (user.go)

```go
// pkg/api/user.go
package api

import (
    "context"
    "net/http"
)

// UserAPI 用户相关 API
type UserAPI struct {
    client *httpclient.Client
}

// UserInfo 用户信息
type UserInfo struct {
    ID           uint64  `json:"id"`
    Username     string  `json:"username"`
    Email        string  `json:"email"`
    Nickname     string  `json:"nickname"`
    Avatar       string  `json:"avatar"`
    StorageUsed  int64   `json:"storage_used"`
    StorageQuota int64   `json:"storage_quota"`
    FileCount    int     `json:"file_count"`
    FolderCount  int     `json:"folder_count"`
    CreatedAt    string  `json:"created_at"`
    UpdatedAt    string  `json:"updated_at"`
}

// UpdateProfileRequest 更新用户信息请求
type UpdateProfileRequest struct {
    Nickname string `json:"nickname,omitempty"`
    Avatar   string `json:"avatar,omitempty"`
}

// ChangePasswordRequest 修改密码请求
type ChangePasswordRequest struct {
    OldPassword string `json:"old_password"`
    NewPassword string `json:"new_password"`
}

// StorageInfo 存储空间信息
type StorageInfo struct {
    Used       int64             `json:"used"`
    Quota      int64             `json:"quota"`
    Percentage float64           `json:"percentage"`
    Categories []StorageCategory `json:"categories"`
}

// StorageCategory 存储分类
type StorageCategory struct {
    Type  string `json:"type"`
    Size  int64  `json:"size"`
    Count int    `json:"count"`
}

// GetProfile 获取用户信息
func (u *UserAPI) GetProfile(ctx context.Context) (*UserInfo, error) {
    var resp struct {
        User *UserInfo `json:"user"`
    }

    err := u.client.NewRequest(ctx, http.MethodGet, "/api/user/profile").
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return resp.User, nil
}

// UpdateProfile 更新用户信息
func (u *UserAPI) UpdateProfile(ctx context.Context, req *UpdateProfileRequest) (*UserInfo, error) {
    var resp struct {
        User *UserInfo `json:"user"`
    }

    err := u.client.NewRequest(ctx, http.MethodPatch, "/api/user/profile").
        WithJSON(req).
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return resp.User, nil
}

// ChangePassword 修改密码
func (u *UserAPI) ChangePassword(ctx context.Context, req *ChangePasswordRequest) error {
    return u.client.NewRequest(ctx, http.MethodPut, "/api/user/password").
        WithJSON(req).
        DoJSON(nil)
}

// GetStorage 获取存储空间统计
func (u *UserAPI) GetStorage(ctx context.Context) (*StorageInfo, error) {
    var resp struct {
        Data *StorageInfo `json:"data"`
    }

    err := u.client.NewRequest(ctx, http.MethodGet, "/api/user/storage").
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return resp.Data, nil
}
```

### 4.3 文件 API (file.go)

```go
// pkg/api/file.go
package api

import (
    "context"
    "io"
    "net/http"
)

// FileAPI 文件相关 API
type FileAPI struct {
    client *httpclient.Client
}

// FileItem 文件项（文件或文件夹）
type FileItem struct {
    ID         uint64 `json:"id"`
    Name       string `json:"name"`
    Type       string `json:"type"`        // "file" 或 "folder"
    Size       int64  `json:"size"`        // 文件大小
    MimeType   string `json:"mime_type"`   // 文件 MIME 类型
    Hash       string `json:"hash"`        // 文件哈希
    ItemCount  int    `json:"item_count"`  // 文件夹子项数量
    ParentID   uint64 `json:"parent_id"`
    Path       string `json:"path"`
    CreatedAt  string `json:"created_at"`
    UpdatedAt  string `json:"updated_at"`
}

// FileListResponse 文件列表响应
type FileListResponse struct {
    Items      []FileItem `json:"items"`
    Pagination Pagination `json:"pagination"`
}

// Pagination 分页信息
type Pagination struct {
    Page       int `json:"page"`
    PageSize   int `json:"page_size"`
    Total      int `json:"total"`
    TotalPages int `json:"total_pages"`
}

// InitUploadRequest 初始化上传请求
type InitUploadRequest struct {
    Filename string `json:"filename"`
    FileSize int64  `json:"file_size"`
    FileHash string `json:"file_hash"`
    ParentID uint64 `json:"parent_id,omitempty"`
}

// InitUploadResponse 初始化上传响应
type InitUploadResponse struct {
    FileID        uint64 `json:"file_id"`
    Filename      string `json:"filename"`
    FileSize      int64  `json:"file_size"`
    FileHash      string `json:"file_hash"`
    MimeType      string `json:"mime_type"`
    SupportsRange bool   `json:"supports_range"`
}

// MoveRequest 移动文件请求
type MoveRequest struct {
    FileIDs        []uint64 `json:"file_ids"`
    TargetFolderID uint64   `json:"target_folder_id"`
}

// CopyRequest 复制文件请求
type CopyRequest struct {
    FileIDs        []uint64 `json:"file_ids"`
    TargetFolderID uint64   `json:"target_folder_id"`
}

// DeleteRequest 删除文件请求
type DeleteRequest struct {
    FileIDs []uint64 `json:"file_ids"`
}

// RenameRequest 重命名请求
type RenameRequest struct {
    NewName string `json:"new_name"`
}

// FileListOptions 文件列表查询选项
type FileListOptions struct {
    ParentID  uint64 `json:"parent_id,omitempty"`
    Page      int    `json:"page,omitempty"`
    PageSize  int    `json:"page_size,omitempty"`
    SortBy    string `json:"sort_by,omitempty"`
    SortOrder string `json:"sort_order,omitempty"`
    Type      string `json:"type,omitempty"`
}

// List 获取文件列表
func (f *FileAPI) List(ctx context.Context, opts *FileListOptions) (*FileListResponse, error) {
    req := f.client.NewRequest(ctx, http.MethodGet, "/api/file/list")

    if opts != nil {
        if opts.ParentID > 0 {
            req.WithQuery("parent_id", strconv.FormatUint(opts.ParentID, 10))
        }
        if opts.Page > 0 {
            req.WithQuery("page", strconv.Itoa(opts.Page))
        }
        if opts.PageSize > 0 {
            req.WithQuery("page_size", strconv.Itoa(opts.PageSize))
        }
        if opts.SortBy != "" {
            req.WithQuery("sort_by", opts.SortBy)
        }
        if opts.SortOrder != "" {
            req.WithQuery("sort_order", opts.SortOrder)
        }
        if opts.Type != "" {
            req.WithQuery("type", opts.Type)
        }
    }

    var resp FileListResponse
    if err := req.DoJSON(&resp); err != nil {
        return nil, err
    }
    return &resp, nil
}

// InitUpload 初始化上传
func (f *FileAPI) InitUpload(ctx context.Context, req *InitUploadRequest) (*InitUploadResponse, error) {
    var resp InitUploadResponse

    err := f.client.NewRequest(ctx, http.MethodPost, "/api/file/upload/init").
        WithJSON(req).
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return &resp, nil
}

// Download 下载文件
func (f *FileAPI) Download(ctx context.Context, fileID uint64) (io.ReadCloser, int64, error) {
    path := fmt.Sprintf("/api/file/download/%d", fileID)

    resp, err := f.client.NewRequest(ctx, http.MethodGet, path).Do()
    if err != nil {
        return nil, 0, err
    }

    return resp.Body, resp.ContentLength, nil
}

// DownloadRange 范围下载（断点续传）
func (f *FileAPI) DownloadRange(ctx context.Context, fileID uint64, start, end int64) (io.ReadCloser, int64, error) {
    path := fmt.Sprintf("/api/file/download/%d", fileID)

    resp, err := f.client.NewRequest(ctx, http.MethodGet, path).
        WithHeader("Range", fmt.Sprintf("bytes=%d-%d", start, end)).
        Do()

    if err != nil {
        return nil, 0, err
    }

    return resp.Body, resp.ContentLength, nil
}

// Rename 重命名文件
func (f *FileAPI) Rename(ctx context.Context, fileID uint64, newName string) error {
    path := fmt.Sprintf("/api/file/%d/rename", fileID)

    return f.client.NewRequest(ctx, http.MethodPut, path).
        WithJSON(&RenameRequest{NewName: newName}).
        DoJSON(nil)
}

// Move 移动文件
func (f *FileAPI) Move(ctx context.Context, req *MoveRequest) error {
    return f.client.NewRequest(ctx, http.MethodPut, "/api/file/move").
        WithJSON(req).
        DoJSON(nil)
}

// Copy 复制文件
func (f *FileAPI) Copy(ctx context.Context, req *CopyRequest) error {
    return f.client.NewRequest(ctx, http.MethodPost, "/api/file/copy").
        WithJSON(req).
        DoJSON(nil)
}

// Delete 删除文件（移入回收站）
func (f *FileAPI) Delete(ctx context.Context, fileIDs []uint64) error {
    return f.client.NewRequest(ctx, http.MethodDelete, "/api/file").
        WithJSON(&DeleteRequest{FileIDs: fileIDs}).
        DoJSON(nil)
}
```

### 4.4 文件夹 API (folder.go)

```go
// pkg/api/folder.go
package api

import (
    "context"
    "net/http"
)

// FolderAPI 文件夹相关 API
type FolderAPI struct {
    client *httpclient.Client
}

// CreateFolderRequest 创建文件夹请求
type CreateFolderRequest struct {
    Name     string `json:"name"`
    ParentID uint64 `json:"parent_id,omitempty"`
}

// CreateFolderResponse 创建文件夹响应
type CreateFolderResponse struct {
    ID        uint64 `json:"id"`
    Name      string `json:"name"`
    ParentID  uint64 `json:"parent_id"`
    Path      string `json:"path"`
    CreatedAt string `json:"created_at"`
}

// TreeNode 目录树节点
type TreeNode struct {
    ID       uint64      `json:"id"`
    Name     string      `json:"name"`
    Children []*TreeNode `json:"children"`
}

// BreadcrumbItem 面包屑项
type BreadcrumbItem struct {
    ID   uint64 `json:"id"`
    Name string `json:"name"`
}

// Create 创建文件夹
func (f *FolderAPI) Create(ctx context.Context, req *CreateFolderRequest) (*CreateFolderResponse, error) {
    var resp CreateFolderResponse

    err := f.client.NewRequest(ctx, http.MethodPost, "/api/folder/create").
        WithJSON(req).
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return &resp, nil
}

// Tree 获取目录树
func (f *FolderAPI) Tree(ctx context.Context, parentID uint64, depth int) (*TreeNode, error) {
    req := f.client.NewRequest(ctx, http.MethodGet, "/api/folder/tree")

    if parentID > 0 {
        req.WithQuery("parent_id", strconv.FormatUint(parentID, 10))
    }
    if depth > 0 {
        req.WithQuery("depth", strconv.Itoa(depth))
    }

    var resp TreeNode
    if err := req.DoJSON(&resp); err != nil {
        return nil, err
    }
    return &resp, nil
}

// Breadcrumb 获取路径面包屑
func (f *FolderAPI) Breadcrumb(ctx context.Context, folderID uint64) ([]BreadcrumbItem, error) {
    path := fmt.Sprintf("/api/folder/%d/breadcrumb", folderID)

    var resp struct {
        Path []BreadcrumbItem `json:"path"`
    }

    if err := f.client.NewRequest(ctx, http.MethodGet, path).DoJSON(&resp); err != nil {
        return nil, err
    }
    return resp.Path, nil
}
```

### 4.5 回收站 API (trash.go)

```go
// pkg/api/trash.go
package api

import (
    "context"
    "net/http"
)

// TrashAPI 回收站相关 API
type TrashAPI struct {
    client *httpclient.Client
}

// TrashItem 回收站项目
type TrashItem struct {
    ID           uint64 `json:"id"`
    OriginalID   uint64 `json:"original_id"`
    Name         string `json:"name"`
    Type         string `json:"type"`
    Size         int64  `json:"size"`
    OriginalPath string `json:"original_path"`
    DeletedAt    string `json:"deleted_at"`
    ExpiresAt    string `json:"expires_at"`
}

// TrashListResponse 回收站列表响应
type TrashListResponse struct {
    Items      []TrashItem `json:"items"`
    Pagination Pagination  `json:"pagination"`
}

// RestoreRequest 恢复请求
type RestoreRequest struct {
    TrashIDs []uint64 `json:"trash_ids"`
}

// RestoreResult 恢复结果
type RestoreResult struct {
    TrashID uint64 `json:"trash_id"`
    Status  string `json:"status"` // "success" or "failed"
    FileID  uint64 `json:"file_id,omitempty"`
    Path    string `json:"path,omitempty"`
    Error   struct {
        Code    int    `json:"code"`
        Message string `json:"message"`
    } `json:"error,omitempty"`
}

// RestoreResponse 恢复响应
type RestoreResponse struct {
    Summary struct {
        Total        int `json:"total"`
        SuccessCount int `json:"success_count"`
        FailureCount int `json:"failure_count"`
    } `json:"summary"`
    Results []RestoreResult `json:"results"`
}

// PermanentDeleteRequest 彻底删除请求
type PermanentDeleteRequest struct {
    TrashIDs []uint64 `json:"trash_ids"`
}

// PermanentDeleteResult 彻底删除结果
type PermanentDeleteResult struct {
    TrashID     uint64 `json:"trash_id"`
    Status      string `json:"status"`
    FreedSpace  int64  `json:"freed_space,omitempty"`
}

// PermanentDeleteResponse 彻底删除响应
type PermanentDeleteResponse struct {
    Summary struct {
        Total        int `json:"total"`
        SuccessCount int `json:"success_count"`
        FailureCount int `json:"failure_count"`
    } `json:"summary"`
    Results []PermanentDeleteResult `json:"results"`
}

// List 获取回收站列表
func (t *TrashAPI) List(ctx context.Context, page, pageSize int) (*TrashListResponse, error) {
    req := t.client.NewRequest(ctx, http.MethodGet, "/api/trash")

    if page > 0 {
        req.WithQuery("page", strconv.Itoa(page))
    }
    if pageSize > 0 {
        req.WithQuery("page_size", strconv.Itoa(pageSize))
    }

    var resp TrashListResponse
    if err := req.DoJSON(&resp); err != nil {
        return nil, err
    }
    return &resp, nil
}

// Restore 恢复文件
func (t *TrashAPI) Restore(ctx context.Context, trashIDs []uint64) (*RestoreResponse, error) {
    var resp RestoreResponse

    err := t.client.NewRequest(ctx, http.MethodPost, "/api/trash/restore").
        WithJSON(&RestoreRequest{TrashIDs: trashIDs}).
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return &resp, nil
}

// PermanentDelete 彻底删除
func (t *TrashAPI) PermanentDelete(ctx context.Context, trashIDs []uint64) (*PermanentDeleteResponse, error) {
    var resp PermanentDeleteResponse

    err := t.client.NewRequest(ctx, http.MethodDelete, "/api/trash").
        WithJSON(&PermanentDeleteRequest{TrashIDs: trashIDs}).
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return &resp, nil
}
```

### 4.6 分享 API (share.go)

```go
// pkg/api/share.go
package api

import (
    "context"
    "net/http"
)

// ShareAPI 分享相关 API
type ShareAPI struct {
    client *httpclient.Client
}

// ShareInfo 分享信息
type ShareInfo struct {
    ID           uint64   `json:"id"`
    Code         string   `json:"code"`
    CreatorID    uint64   `json:"creator_id"`
    CreatorName  string   `json:"creator_name"`
    Name         string   `json:"name"`
    Description  string   `json:"description"`
    HasPassword  bool     `json:"has_password"`
    Permission   string   `json:"permission"` // "view" or "download"
    Files        []SharedFile `json:"files"`
    ExpiresAt    string   `json:"expires_at"`
    CreatedAt    string   `json:"created_at"`
    ViewCount    int      `json:"view_count"`
    DownloadCount int     `json:"download_count"`
}

// SharedFile 分享的文件
type SharedFile struct {
    ID       uint64 `json:"id"`
    Name     string `json:"name"`
    Size     int64  `json:"size"`
    Type     string `json:"type"`
    MimeType string `json:"mime_type"`
}

// AccessShareRequest 访问分享请求
type AccessShareRequest struct {
    Code     string `json:"code"`
    Password string `json:"password,omitempty"`
}

// AccessShareResponse 访问分享响应
type AccessShareResponse struct {
    ShareToken string     `json:"share_token"`
    Share      *ShareInfo `json:"share"`
}

// CreateShareRequest 创建分享请求
type CreateShareRequest struct {
    FileIDs     []uint64 `json:"file_ids"`
    Name        string   `json:"name,omitempty"`
    Description string   `json:"description,omitempty"`
    Password    string   `json:"password,omitempty"`
    Permission  string   `json:"permission,omitempty"`
    ExpiresIn   int      `json:"expires_in,omitempty"` // 小时
}

// CreateShareResponse 创建分享响应
type CreateShareResponse struct {
    ID        uint64 `json:"id"`
    Code      string `json:"code"`
    ExpiresAt string `json:"expires_at"`
}

// ListSharesOptions 分享列表查询选项
type ListSharesOptions struct {
    Page     int `json:"page,omitempty"`
    PageSize int `json:"page_size,omitempty"`
}

// ListSharesResponse 分享列表响应
type ListSharesResponse struct {
    Items      []ShareInfo `json:"items"`
    Pagination Pagination  `json:"pagination"`
}

// Create 创建分享
func (s *ShareAPI) Create(ctx context.Context, req *CreateShareRequest) (*CreateShareResponse, error) {
    var resp CreateShareResponse

    err := s.client.NewRequest(ctx, http.MethodPost, "/api/share").
        WithJSON(req).
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return &resp, nil
}

// Access 访问分享
func (s *ShareAPI) Access(ctx context.Context, req *AccessShareRequest) (*AccessShareResponse, error) {
    var resp AccessShareResponse

    err := s.client.NewRequest(ctx, http.MethodPost, "/api/share/access").
        WithJSON(req).
        WithoutAuth().
        DoJSON(&resp)

    if err != nil {
        return nil, err
    }
    return &resp, nil
}

// List 获取我的分享列表
func (s *ShareAPI) List(ctx context.Context, opts *ListSharesOptions) (*ListSharesResponse, error) {
    req := s.client.NewRequest(ctx, http.MethodGet, "/api/share")

    if opts != nil {
        if opts.Page > 0 {
            req.WithQuery("page", strconv.Itoa(opts.Page))
        }
        if opts.PageSize > 0 {
            req.WithQuery("page_size", strconv.Itoa(opts.PageSize))
        }
    }

    var resp ListSharesResponse
    if err := req.DoJSON(&resp); err != nil {
        return nil, err
    }
    return &resp, nil
}

// Cancel 取消分享（批量）
func (s *ShareAPI) Cancel(ctx context.Context, shareIDs []string) error {
    body := map[string]any{"share_ids": shareIDs}
    return s.client.NewRequest(ctx, http.MethodDelete, "/api/share").
        WithJSON(body).
        DoJSON(nil)
}

// DownloadShare 下载分享文件
func (s *ShareAPI) DownloadShare(ctx context.Context, shareCode string, fileID uint64, shareToken string) (io.ReadCloser, int64, error) {
    path := fmt.Sprintf("/api/share/%s/download/%d", shareCode, fileID)

    resp, err := s.client.NewRequest(ctx, http.MethodGet, path).
        WithHeader("X-Share-Token", shareToken).
        Do()

    if err != nil {
        return nil, 0, err
    }

    return resp.Body, resp.ContentLength, nil
}
```

---

## 5. 错误处理

### 5.1 错误码映射表

| 后端错误码 | 枚举名称 | UI 行为 | 用户提示 |
|-----------|----------|---------|----------|
| **通用错误** | | | |
| 0 | Success | 正常处理 | - |
| 10001 | InvalidParameter | 显示表单错误 | "参数格式错误，请检查输入" |
| 10002 | ValidationFailed | 显示字段错误 | "输入验证失败：{field} {reason}" |
| 10003 | ResourceNotFound | 返回列表 | "资源不存在或已被删除" |
| 10004 | ResourceConflict | 刷新数据 | "资源已被修改，请刷新后重试" |
| 10005 | TooManyRequests | 显示等待 | "操作过于频繁，请稍后再试" |
| 10006 | InternalError | 显示重试 | "服务器错误，请稍后再试" |
| **认证错误** | | | |
| 40001 | UsernameExists | 聚焦用户名 | "用户名已被注册" |
| 40002 | EmailExists | 聚焦邮箱 | "邮箱已被注册" |
| 40101 | InvalidCredentials | 聚焦密码 | "用户名或密码错误" |
| 40102 | AccountLocked | 显示等待 | "账户已锁定，请 15 分钟后重试" |
| 40103 | AccountDisabled | 跳转登录 | "账户已被禁用，请联系管理员" |
| 40104 | InvalidToken | 跳转登录 | "登录已失效，请重新登录" |
| 40105 | InvalidRefreshToken | 跳转登录 | "登录已失效，请重新登录" |
| 40106 | TokenMissing | 跳转登录 | "请先登录" |
| 40107 | TokenMalformed | 跳转登录 | "登录状态异常，请重新登录" |
| 40108 | TokenExpired | 自动刷新 | 触发令牌刷新流程 |
| 40109 | TokenWrongType | 跳转登录 | "登录状态异常，请重新登录" |
| 40110 | RefreshTokenAlreadyUsed | 跳转登录 | "登录已在其他设备使用，请重新登录" |
| 40111 | TokenRevoked | 跳转登录 | "登录已被注销，请重新登录" |
| **文件错误** | | | |
| 50001 | InvalidFilename | 聚焦文件名 | "文件名包含非法字符" |
| 50002 | FileTypeNotAllowed | 显示提示 | "不支持此文件类型" |
| 50003 | FileSizeExceeded | 显示提示 | "文件大小超出限制" |
| 50004 | StorageQuotaExceeded | 显示配额 | "存储空间不足，请清理后重试" |
| 50005 | FileNotFound | 刷新列表 | "文件不存在或已被删除" |
| 50006 | FolderNotFound | 返回根目录 | "文件夹不存在或已被删除" |
| 50007 | FileAlreadyExists | 聚焦文件名 | "同名文件已存在" |
| 50010 | FolderAlreadyExists | 聚焦文件夹名 | "同名文件夹已存在" |
| **分享错误** | | | |
| 60001 | ShareNotFound | 显示提示 | "分享不存在或已过期" |
| 60002 | ShareExpired | 显示提示 | "分享已过期" |
| 60003 | SharePasswordError | 聚焦密码 | "分享密码错误" |
| 60004 | ShareAccessDenied | 显示提示 | "无权限访问此分享" |

### 5.2 网络错误处理

```go
// pkg/api/error_handler.go
package api

import (
    "errors"
    "net"
    "net/url"
    "strings"
)

// NetworkError 网络错误
type NetworkError struct {
    Op  string // 操作
    Err error  // 原始错误
}

func (e *NetworkError) Error() string {
    return e.Op + ": " + e.Err.Error()
}

// IsNetworkError 判断是否为网络错误
func IsNetworkError(err error) bool {
    var netErr net.Error
    if errors.As(err, &netErr) {
        return true
    }

    var urlErr *url.Error
    if errors.As(err, &urlErr) {
        return true
    }

    // DNS 错误
    if strings.Contains(err.Error(), "no such host") {
        return true
    }

    // 连接拒绝
    if strings.Contains(err.Error(), "connection refused") {
        return true
    }

    // 超时
    if strings.Contains(err.Error(), "timeout") {
        return true
    }

    return false
}

// HandleNetworkError 处理网络错误
func HandleNetworkError(err error) (message string, retryable bool) {
    if !IsNetworkError(err) {
        return "未知错误", false
    }

    errStr := err.Error()

    switch {
    case strings.Contains(errStr, "timeout"):
        return "网络请求超时，请检查网络连接", true
    case strings.Contains(errStr, "connection refused"):
        return "无法连接到服务器", true
    case strings.Contains(errStr, "no such host"):
        return "DNS 解析失败，请检查服务器地址", false
    case strings.Contains(errStr, "connection reset"):
        return "连接被重置", true
    default:
        return "网络连接失败", true
    }
}
```

### 5.3 业务错误处理

```go
// pkg/api/error_handler.go

// HandleAPIError 处理 API 错误
func HandleAPIError(err *APIError) (message string, action ErrorAction) {
    switch err.Code {
    // 认证相关 - 需要重新登录
    case 40104, 40105, 40106, 40107, 40109, 40110, 40111:
        return err.Message, ActionRelogin

    // 令牌过期 - 触发刷新
    case 40108:
        return err.Message, ActionRefreshToken

    // 账户锁定 - 等待
    case 40102:
        return "账户已锁定，请 15 分钟后重试", ActionWait

    // 限流 - 等待
    case 10005:
        return "操作过于频繁，请稍后再试", ActionWait

    // 资源不存在 - 刷新
    case 10003, 50005, 50006:
        return err.Message, ActionRefresh

    // 资源冲突 - 刷新
    case 10004:
        return "资源已被修改，请刷新后重试", ActionRefresh

    // 存储空间不足 - 显示配额
    case 50004:
        return "存储空间不足", ActionShowQuota

    // 服务器错误 - 重试
    case 10006:
        return "服务器错误，请稍后再试", ActionRetry

    // 默认 - 显示错误
    default:
        return err.Message, ActionShowError
    }
}

// ErrorAction 错误处理动作
type ErrorAction int

const (
    ActionShowError    ErrorAction = iota // 显示错误信息
    ActionRelogin                         // 跳转登录
    ActionRefreshToken                    // 刷新令牌
    ActionRefresh                         // 刷新数据
    ActionRetry                           // 重试操作
    ActionWait                            // 等待后重试
    ActionShowQuota                       // 显示配额信息
)
```

---

## 6. 缓存策略

### 6.1 文件列表缓存

```go
// pkg/cache/file_cache.go
package cache

import (
    "sync"
    "time"
)

// FileListCache 文件列表缓存
type FileListCache struct {
    mu       sync.RWMutex
    data     map[string]*cacheEntry
    ttl      time.Duration
    maxItems int
}

type cacheEntry struct {
    data      interface{}
    expiresAt time.Time
    etag      string // 用于缓存验证
}

// NewFileListCache 创建文件列表缓存
func NewFileListCache(ttl time.Duration, maxItems int) *FileListCache {
    return &FileListCache{
        data:     make(map[string]*cacheEntry),
        ttl:      ttl,
        maxItems: maxItems,
    }
}

// Get 获取缓存
func (c *FileListCache) Get(key string) (interface{}, bool) {
    c.mu.RLock()
    defer c.mu.RUnlock()

    entry, ok := c.data[key]
    if !ok {
        return nil, false
    }

    if time.Now().After(entry.expiresAt) {
        return nil, false
    }

    return entry.data, true
}

// Set 设置缓存
func (c *FileListCache) Set(key string, data interface{}, etag string) {
    c.mu.Lock()
    defer c.mu.Unlock()

    // LRU 淘汰
    if len(c.data) >= c.maxItems {
        c.evictOldest()
    }

    c.data[key] = &cacheEntry{
        data:      data,
        expiresAt: time.Now().Add(c.ttl),
        etag:      etag,
    }
}

// Invalidate 使缓存失效
func (c *FileListCache) Invalidate(key string) {
    c.mu.Lock()
    defer c.mu.Unlock()
    delete(c.data, key)
}

// InvalidateByPrefix 按前缀使缓存失效
func (c *FileListCache) InvalidateByPrefix(prefix string) {
    c.mu.Lock()
    defer c.mu.Unlock()

    for key := range c.data {
        if strings.HasPrefix(key, prefix) {
            delete(c.data, key)
        }
    }
}

// Clear 清空缓存
func (c *FileListCache) Clear() {
    c.mu.Lock()
    defer c.mu.Unlock()
    c.data = make(map[string]*cacheEntry)
}

// evictOldest 淘汰最旧的缓存
func (c *FileListCache) evictOldest() {
    var oldestKey string
    var oldestTime time.Time

    for key, entry := range c.data {
        if oldestKey == "" || entry.expiresAt.Before(oldestTime) {
            oldestKey = key
            oldestTime = entry.expiresAt
        }
    }

    if oldestKey != "" {
        delete(c.data, oldestKey)
    }
}

// 文件列表缓存键格式: "filelist:{parent_id}:{page}:{sort}"
func FileListCacheKey(parentID uint64, page int, sortBy string) string {
    return fmt.Sprintf("filelist:%d:%d:%s", parentID, page, sortBy)
}
```

### 6.2 用户信息缓存

```go
// pkg/cache/user_cache.go
package cache

import (
    "sync"
    "time"
)

// UserCache 用户信息缓存
type UserCache struct {
    mu          sync.RWMutex
    userInfo    *UserInfo
    storageInfo *StorageInfo
    expiresAt   time.Time
    ttl         time.Duration
}

// NewUserCache 创建用户信息缓存
func NewUserCache(ttl time.Duration) *UserCache {
    return &UserCache{
        ttl: ttl,
    }
}

// GetUserInfo 获取用户信息
func (c *UserCache) GetUserInfo() *UserInfo {
    c.mu.RLock()
    defer c.mu.RUnlock()

    if c.userInfo == nil || time.Now().After(c.expiresAt) {
        return nil
    }
    return c.userInfo
}

// SetUserInfo 设置用户信息
func (c *UserCache) SetUserInfo(info *UserInfo) {
    c.mu.Lock()
    defer c.mu.Unlock()

    c.userInfo = info
    c.expiresAt = time.Now().Add(c.ttl)
}

// GetStorageInfo 获取存储信息
func (c *UserCache) GetStorageInfo() *StorageInfo {
    c.mu.RLock()
    defer c.mu.RUnlock()

    if c.storageInfo == nil || time.Now().After(c.expiresAt) {
        return nil
    }
    return c.storageInfo
}

// SetStorageInfo 设置存储信息
func (c *UserCache) SetStorageInfo(info *StorageInfo) {
    c.mu.Lock()
    defer c.mu.Unlock()

    c.storageInfo = info
    c.expiresAt = time.Now().Add(c.ttl)
}

// UpdateStorageUsed 更新已用空间（上传/删除后调用）
func (c *UserCache) UpdateStorageUsed(delta int64) {
    c.mu.Lock()
    defer c.mu.Unlock()

    if c.storageInfo != nil {
        c.storageInfo.Used += delta
        if c.storageInfo.Quota > 0 {
            c.storageInfo.Percentage = float64(c.storageInfo.Used) / float64(c.storageInfo.Quota) * 100
        }
    }
}

// Clear 清空缓存
func (c *UserCache) Clear() {
    c.mu.Lock()
    defer c.mu.Unlock()

    c.userInfo = nil
    c.storageInfo = nil
    c.expiresAt = time.Time{}
}

// IsExpired 检查缓存是否过期
func (c *UserCache) IsExpired() bool {
    c.mu.RLock()
    defer c.mu.RUnlock()
    return time.Now().After(c.expiresAt)
}
```

---

## 7. 配置管理

### 7.1 配置源优先级

配置加载顺序（后加载覆盖先加载）：

```
1. 默认值（代码内置）
2. 配置文件 (~/.config/disk/tui/config.yaml)
3. 环境变量 (DISK_*)
4. CLI 参数（最高优先级）
```

### 7.2 配置结构定义

```go
// pkg/config/config.go
package config

import (
    "os"
    "path/filepath"
    "time"
)

// Config 应用配置
type Config struct {
    Server  ServerConfig  `yaml:"server" mapstructure:"server"`
    Storage StorageConfig `yaml:"storage" mapstructure:"storage"`
    Log     LogConfig     `yaml:"log" mapstructure:"log"`
    UI      UIConfig      `yaml:"ui" mapstructure:"ui"`
}

// ServerConfig 服务器配置
type ServerConfig struct {
    // API 服务器地址
    BaseURL string `yaml:"base_url" mapstructure:"base_url" env:"DISK_SERVER_URL"`
    // 请求超时（秒）
    Timeout int `yaml:"timeout" mapstructure:"timeout" env:"DISK_TIMEOUT"`
    // 重试次数
    MaxRetry int `yaml:"max_retry" mapstructure:"max_retry" env:"DISK_MAX_RETRY"`
    // 是否跳过 TLS 验证（开发用）
    InsecureSkipVerify bool `yaml:"insecure_skip_verify" mapstructure:"insecure_skip_verify" env:"DISK_INSECURE"`
}

// StorageConfig 存储配置
type StorageConfig struct {
    // 配置目录
    ConfigDir string `yaml:"config_dir" mapstructure:"config_dir" env:"DISK_CONFIG_DIR"`
    // 下载目录
    DownloadDir string `yaml:"download_dir" mapstructure:"download_dir" env:"DISK_DOWNLOAD_DIR"`
    // 分片上传大小（字节）
    ChunkSize int64 `yaml:"chunk_size" mapstructure:"chunk_size" env:"DISK_CHUNK_SIZE"`
    // 并发上传数
    ConcurrentUploads int `yaml:"concurrent_uploads" mapstructure:"concurrent_uploads" env:"DISK_CONCURRENT_UPLOADS"`
}

// LogConfig 日志配置
type LogConfig struct {
    // 日志级别: debug, info, warn, error
    Level string `yaml:"level" mapstructure:"level" env:"DISK_LOG_LEVEL"`
    // 日志文件路径（空则输出到终端）
    File string `yaml:"file" mapstructure:"file" env:"DISK_LOG_FILE"`
    // 是否输出到终端
    Console bool `yaml:"console" mapstructure:"console" env:"DISK_LOG_CONSOLE"`
}

// UIConfig 界面配置
type UIConfig struct {
    // 每页显示条数
    PageSize int `yaml:"page_size" mapstructure:"page_size" env:"DISK_PAGE_SIZE"`
    // 默认排序字段
    DefaultSort string `yaml:"default_sort" mapstructure:"default_sort" env:"DISK_DEFAULT_SORT"`
    // 默认排序方向
    DefaultOrder string `yaml:"default_order" mapstructure:"default_order" env:"DISK_DEFAULT_ORDER"`
    // 显示隐藏文件
    ShowHidden bool `yaml:"show_hidden" mapstructure:"show_hidden" env:"DISK_SHOW_HIDDEN"`
    // 文件大小显示格式: binary, decimal
    SizeFormat string `yaml:"size_format" mapstructure:"size_format" env:"DISK_SIZE_FORMAT"`
}

// DefaultConfig 返回默认配置
func DefaultConfig() *Config {
    homeDir, _ := os.UserHomeDir()

    return &Config{
        Server: ServerConfig{
            BaseURL:           "https://disk.example.com/api",
            Timeout:           30,
            MaxRetry:          3,
            InsecureSkipVerify: false,
        },
        Storage: StorageConfig{
            ConfigDir:         filepath.Join(homeDir, ".config", "disk", "tui"),
            DownloadDir:       filepath.Join(homeDir, "Downloads"),
            ChunkSize:         5 * 1024 * 1024, // 5MB
            ConcurrentUploads: 3,
        },
        Log: LogConfig{
            Level:    "info",
            File:     "",
            Console:  true,
        },
        UI: UIConfig{
            PageSize:     20,
            DefaultSort:  "name",
            DefaultOrder: "asc",
            ShowHidden:   false,
            SizeFormat:   "binary",
        },
    }
}
```

### 7.3 配置文件示例

```yaml
# ~/.config/disk/tui/config.yaml

# 服务器配置
server:
  # API 服务器地址
  base_url: https://disk.example.com/api
  # 请求超时（秒）
  timeout: 30
  # 重试次数
  max_retry: 3
  # 跳过 TLS 验证（仅开发环境）
  # insecure_skip_verify: false

# 存储配置
storage:
  # 配置目录（令牌文件存储位置）
  config_dir: ~/.config/disk/tui
  # 下载目录
  download_dir: ~/Downloads
  # 分片上传大小（字节），默认 5MB
  chunk_size: 5242880
  # 并发上传数
  concurrent_uploads: 3

# 日志配置
log:
  # 日志级别: debug, info, warn, error
  level: info
  # 日志文件（空则仅输出到终端）
  # file: ~/.config/disk/tui/disk-tui.log
  # 是否输出到终端
  console: true

# 界面配置
ui:
  # 每页显示条数
  page_size: 20
  # 默认排序字段: name, size, created_at, updated_at
  default_sort: name
  # 默认排序方向: asc, desc
  default_order: asc
  # 显示隐藏文件（以 . 开头）
  show_hidden: false
  # 文件大小显示格式: binary (1024), decimal (1000)
  size_format: binary
```

### 7.4 配置加载实现

```go
// pkg/config/loader.go
package config

import (
    "fmt"
    "os"
    "reflect"
    "strconv"
    "strings"

    "github.com/spf13/cobra"
    "github.com/spf13/viper"
)

// Load 加载配置
func Load(cmd *cobra.Command) (*Config, error) {
    cfg := DefaultConfig()

    v := viper.New()

    // 设置默认值
    setDefaults(v, cfg)

    // 配置文件
    v.SetConfigName("config")
    v.SetConfigType("yaml")
    v.AddConfigPath(cfg.Storage.ConfigDir)
    v.AddConfigPath("$HOME/.config/disk/tui")
    v.AddConfigPath(".")

    // 读取配置文件
    if err := v.ReadInConfig(); err != nil {
        if _, ok := err.(viper.ConfigFileNotFoundError); !ok {
            return nil, fmt.Errorf("读取配置文件失败: %w", err)
        }
        // 配置文件不存在是正常情况
    }

    // 绑定环境变量
    v.SetEnvPrefix("DISK")
    v.SetEnvKeyReplacer(strings.NewReplacer(".", "_"))
    v.AutomaticEnv()

    // 绑定 CLI 参数
    if cmd != nil {
        bindFlags(v, cmd)
    }

    // 解析到结构体
    if err := v.Unmarshal(cfg); err != nil {
        return nil, fmt.Errorf("解析配置失败: %w", err)
    }

    // 验证配置
    if err := validateConfig(cfg); err != nil {
        return nil, err
    }

    return cfg, nil
}

// setDefaults 设置默认值
func setDefaults(v *viper.Viper, cfg *Config) {
    v.SetDefault("server.base_url", cfg.Server.BaseURL)
    v.SetDefault("server.timeout", cfg.Server.Timeout)
    v.SetDefault("server.max_retry", cfg.Server.MaxRetry)
    v.SetDefault("server.insecure_skip_verify", cfg.Server.InsecureSkipVerify)

    v.SetDefault("storage.config_dir", cfg.Storage.ConfigDir)
    v.SetDefault("storage.download_dir", cfg.Storage.DownloadDir)
    v.SetDefault("storage.chunk_size", cfg.Storage.ChunkSize)
    v.SetDefault("storage.concurrent_uploads", cfg.Storage.ConcurrentUploads)

    v.SetDefault("log.level", cfg.Log.Level)
    v.SetDefault("log.file", cfg.Log.File)
    v.SetDefault("log.console", cfg.Log.Console)

    v.SetDefault("ui.page_size", cfg.UI.PageSize)
    v.SetDefault("ui.default_sort", cfg.UI.DefaultSort)
    v.SetDefault("ui.default_order", cfg.UI.DefaultOrder)
    v.SetDefault("ui.show_hidden", cfg.UI.ShowHidden)
    v.SetDefault("ui.size_format", cfg.UI.SizeFormat)
}

// bindFlags 绑定 CLI 参数
func bindFlags(v *viper.Viper, cmd *cobra.Command) {
    // 服务器地址
    if cmd.Flags().Lookup("server") != nil {
        v.BindPFlag("server.base_url", cmd.Flags().Lookup("server"))
    }

    // 超时
    if cmd.Flags().Lookup("timeout") != nil {
        v.BindPFlag("server.timeout", cmd.Flags().Lookup("timeout"))
    }

    // 日志级别
    if cmd.Flags().Lookup("log-level") != nil {
        v.BindPFlag("log.level", cmd.Flags().Lookup("log-level"))
    }
}

// validateConfig 验证配置
func validateConfig(cfg *Config) error {
    if cfg.Server.BaseURL == "" {
        return fmt.Errorf("服务器地址不能为空")
    }

    if !strings.HasPrefix(cfg.Server.BaseURL, "https://") &&
       !strings.HasPrefix(cfg.Server.BaseURL, "http://") {
        return fmt.Errorf("服务器地址必须是有效的 URL")
    }

    if cfg.Server.Timeout <= 0 {
        return fmt.Errorf("超时时间必须大于 0")
    }

    if cfg.UI.PageSize <= 0 || cfg.UI.PageSize > 100 {
        return fmt.Errorf("每页条数必须在 1-100 之间")
    }

    return nil
}

// GetConfigDir 获取配置目录
func GetConfigDir() string {
    if dir := os.Getenv("DISK_CONFIG_DIR"); dir != "" {
        return dir
    }

    homeDir, _ := os.UserHomeDir()
    return filepath.Join(homeDir, ".config", "disk", "tui")
}
```

---

## 附录 A: 文件命名规范

| 文件 | 路径 | 说明 |
|------|------|------|
| HTTP 客户端 | `pkg/httpclient/client.go` | 基础客户端封装 |
| 请求处理 | `pkg/httpclient/request.go` | 请求/响应处理 |
| 重试策略 | `pkg/httpclient/retry.go` | 重试逻辑 |
| 认证 API | `pkg/api/auth.go` | 认证接口 |
| 用户 API | `pkg/api/user.go` | 用户接口 |
| 文件 API | `pkg/api/file.go` | 文件接口 |
| 文件夹 API | `pkg/api/folder.go` | 文件夹接口 |
| 回收站 API | `pkg/api/trash.go` | 回收站接口 |
| 分享 API | `pkg/api/share.go` | 分享接口 |
| 错误处理 | `pkg/api/error_handler.go` | 错误码映射 |
| 令牌管理 | `pkg/auth/token_manager.go` | 令牌刷新逻辑 |
| 令牌存储 | `pkg/auth/token_storage.go` | 加密存储 |
| 文件缓存 | `pkg/cache/file_cache.go` | 文件列表缓存 |
| 用户缓存 | `pkg/cache/user_cache.go` | 用户信息缓存 |
| 配置结构 | `pkg/config/config.go` | 配置定义 |
| 配置加载 | `pkg/config/loader.go` | 配置加载 |

---

*文档版本: 1.0.0*
*最后更新: 2026-02-16*
