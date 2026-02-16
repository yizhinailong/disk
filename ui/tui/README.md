# Disk TUI Client

基于 [Bubble Tea](https://github.com/charmbracelet/bubbletea) 框架的终端用户界面客户端，为 [Disk](../..) 网盘系统提供命令行访问能力。

## 特性

- 安全认证: JWT 令牌 + 加密存储
- 文件管理: 浏览、上传、下载、重命名、移动、复制、删除
- 高效传输: 分片上传、秒传、断点续传
- vim 风格导航: j/k/gg/G 快捷键
- 现代界面: 基于 Lipgloss 的精美样式

## 安装

### 从源码构建

```bash
cd ui/tui
go build -o bin/disk-tui ./cmd/disk-tui
```

### 运行测试

```bash
go test ./... -v
```

## 使用

### 基本用法

```bash
./disk-tui
```

### 命令行参数

| 参数 | 说明 |
|------|------|
| `--version` | 显示版本信息 |
| `--config <path>` | 指定配置文件路径 |
| `--server <url>` | 指定服务器地址 |

### 示例

```bash
# 显示版本
./disk-tui --version

# 连接指定服务器
./disk-tui --server https://disk.example.com

# 使用自定义配置
./disk-tui --config ~/.config/disk-tui/config.yaml
```

## 配置

### 配置文件

默认配置文件位置（按优先级）：
1. `./config.yaml`
2. `./configs/config.yaml`
3. `~/.config/disk-tui/config.yaml`
4. `/etc/disk-tui/config.yaml`

### 配置示例

```yaml
server:
  url: "https://disk.example.com"
  timeout: 30

storage:
  token_path: "~/.config/disk-tui/token.enc"

log:
  level: "info"
  format: "console"
```

### 环境变量

| 变量 | 说明 |
|------|------|
| `DISK_SERVER_URL` | 服务器地址 |
| `DISK_TOKEN_PATH` | Token 存储路径 |
| `DISK_LOG_LEVEL` | 日志级别 |
| `DISK_PASSWORD` | 加密密码（非交互式）|

## 快捷键

### 全局

| 按键 | 功能 |
|------|------|
| `?` | 显示帮助 |
| `Q` | 退出 |
| `Ctrl+C` | 强制退出 |

### 导航

| 按键 | 功能 |
|------|------|
| `j` / `↓` | 下移 |
| `k` / `↑` | 上移 |
| `gg` | 跳到顶部 |
| `G` | 跳到底部 |
| `Enter` | 打开文件夹/确认 |
| `h` / `Esc` | 返回上级 |

### 文件操作

| 按键 | 功能 |
|------|------|
| `u` | 上传文件 |
| `d` | 下载文件 |
| `r` | 重命名 |
| `m` | 移动 |
| `c` | 复制 |
| `x` / `dd` | 删除 |
| `n` | 新建文件夹 |
| `Space` | 多选切换 |
| `a` | 全选/取消全选 |

## 项目结构

```
ui/tui/
├── cmd/disk-tui/          # 程序入口
├── internal/
│   ├── app/               # 应用主逻辑
│   ├── api/               # API 客户端
│   ├── config/            # 配置管理
│   ├── models/            # 数据模型
│   ├── store/             # Token 存储
│   ├── uploader/          # 上传管理
│   ├── downloader/        # 下载管理
│   └── ui/
│       ├── components/    # UI 组件
│       ├── pages/         # 页面
│       └── styles/        # 样式定义
├── configs/               # 配置文件
├── go.mod
├── go.sum
└── Makefile
```

## 技术栈

| 组件 | 库 |
|------|------|
| TUI 框架 | Bubble Tea |
| 样式 | Lipgloss |
| 组件 | Bubbles |
| 配置 | Viper |
| 日志 | zerolog |

## 相关文档

- [设计文档](../../docs/ui/tui/)
- [API 接口设计](../../docs/design/02-API接口设计.md)

## License

MIT
