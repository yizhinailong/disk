# 注册页面设计

## 概述

注册页面是新用户创建 Disk 网盘账号的入口，收集用户名、邮箱和密码信息，提供即时表单验证和错误反馈。

---

## Qt/QML 设计

### 界面布局

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│    ┌─────────────────────────┐    ┌────────────────────────────────┐    │
│    │                         │    │                                │    │
│    │                         │    │         [Logo] Disk            │    │
│    │                         │    │                                │    │
│    │      渐变背景            │    │       创建新账号               │    │
│    │   #FFF0F5 → #E0F7FA     │    │                                │    │
│    │                         │    │   ┌────────────────────────┐   │    │
│    │   ┌─────────────────┐   │    │   │  登录   │  注册  ✓   │   │    │
│    │   │  欢迎语/插图     │   │    │   └────────────────────────┘   │    │
│    │   │                 │   │    │                                │    │
│    │   │ 加入我们吧！     │   │    │   邮箱: [________________]     │    │
│    │   │ 30秒快速注册✨   │   │    │                                │    │
│    │   │                 │   │    │   用户名: [________________]    │    │
│    │   └─────────────────┘   │    │                                │    │
│    │                         │    │   密码: [________________] 👁   │    │
│    │                         │    │                                │    │
│    │                         │    │   确认密码: [____________] ✓   │    │
│    │                         │    │                                │    │
│    │                         │    │         [ 注 册 ]              │    │
│    │                         │    │                                │    │
│    │                         │    │  注册即表示同意《用户协议》     │    │
│    │                         │    │  和《隐私政策》                 │    │
│    │                         │    │                                │    │
│    │                         │    │  已有账号？直接登录             │    │
│    └─────────────────────────┘    └────────────────────────────────┘    │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

### 组件规格

| 组件 | 属性 | 规范 |
|------|------|------|
| Logo | 尺寸 | 48x48 px |
| 标题 | 字号 | H1 (24px), 加粗 |
| Tab 切换 | 高度 | 40px |
| 输入框 | 高度 | 52px |
| 输入框 | 圆角 | 12px |
| 注册按钮 | 高度 | 52px |
| 注册按钮 | 圆角 | 12px |
| 卡片 | 宽度 | 420px |
| 卡片 | 圆角 | 16px |
| 卡片 | 阴影 | 0 4px 24px rgba(0,0,0,0.08) |

### 表单字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| 邮箱 | Email | ✓ | 用户邮箱地址 |
| 用户名 | Text | ✓ | 4-32字符，字母数字下划线 |
| 密码 | Password | ✓ | 8-64字符，需包含大小写字母和数字 |
| 确认密码 | Password | ✓ | 必须与密码一致（仅客户端验证） |

### 交互设计

- **Tab 切换**：登录/注册标签页切换，当前页高亮
- **密码显示/隐藏**：点击眼睛图标切换明文/密文
- **确认密码匹配**：输入时实时检测，匹配后显示绿色勾号 ✓
- **实时验证**：输入框失焦时验证格式，错误显示红色边框和提示
- **回车提交**：在任意输入框按回车触发注册
- **加载状态**：注册中显示加载动画，按钮禁用
- **成功跳转**：注册成功后切换到登录页，预填用户名

### 样式规范

| 元素 | 属性 | 值 |
|------|------|-----|
| 卡片背景 | 背景色 | #FFFFFF |
| 卡片阴影 | box-shadow | 0 4px 24px rgba(0,0,0,0.08) |
| 左侧背景 | 渐变 | linear-gradient(135deg, #FFF0F5, #E0F7FA) |
| 输入框 | 高度 | 52px |
| 输入框 | 内边距 | 14px 16px |
| 输入框 | 边框 | 1px solid #E2E8F0 |
| 输入框 | 圆角 | 12px |
| 输入框 | 字号 | 14px |
| 输入框聚焦 | 边框色 | #FF6B6B |
| 输入框错误 | 边框色 | #E53E3E |
| 主按钮 | 背景色 | #FF6B6B |
| 主按钮 | 文字色 | #FFFFFF |
| 主按钮 | 高度 | 52px |
| 主按钮 | 圆角 | 12px |
| 主按钮 | 字号 | 16px |
| 主按钮 | 字重 | 600 (semibold) |
| 主按钮悬停 | 背景色 | #FF8787 |
| 主按钮禁用 | 背景色 | #CBD5E0 |
| 文字色 | 主文字 | #2D3748 |
| 文字色 | 次要文字 | #718096 |
| 文字色 | 提示文字 | #A0AEC0 |
| 链接色 | 链接 | #FF6B6B |
| 成功色 | 匹配成功 | #48BB78 |

### 调色板

| Token | 值 | 用途 |
|-------|-----|------|
| `--color-primary` | #FF6B6B | 主按钮、链接、聚焦边框 |
| `--color-primary-hover` | #FF8787 | 主按钮悬停 |
| `--color-secondary` | #4ECDC4 | 装饰元素 |
| `--color-background` | #FFFFFF | 卡片背景 |
| `--color-surface` | #FAFAFC | 页面背景 |
| `--color-text` | #2D3748 | 主要文字 |
| `--color-text-muted` | #718096 | 次要文字 |
| `--color-border` | #E2E8F0 | 输入框边框 |
| `--color-success` | #48BB78 | 成功状态、匹配指示 |
| `--color-error` | #E53E3E | 错误状态 |
| `--radius-sm` | 12px | 输入框、按钮圆角 |
| `--radius-lg` | 16px | 卡片圆角 |

---

## Qt Widget 设计

### 界面布局

使用 `QDialog` + `QHBoxLayout` 实现左右分栏布局：

```
QDialog (居中显示, 最小 900x600)
└── QHBoxLayout (spacing: 0)
    ├── QWidget (左侧装饰区, stretch: 1)
    │   └── QVBoxLayout
    │       ├── QWidget (弹性空间)
    │       ├── QLabel (欢迎语)
    │       ├── QLabel (副标题)
    │       └── QWidget (弹性空间)
    │
    └── QWidget (右侧表单区, 固定 420px)
        └── QVBoxLayout
            ├── QWidget (Logo 区域)
            │   └── QHBoxLayout
            │       ├── QLabel (Logo)
            │       └── QLabel (标题)
            ├── QWidget (Tab 切换区)
            │   └── QHBoxLayout
            │       ├── QPushButton (登录 Tab)
            │       └── QPushButton (注册 Tab, 选中)
            ├── QFormLayout (表单区)
            │   ├── QLineEdit (邮箱输入)
            │   ├── QLineEdit (用户名输入)
            │   ├── QWidget (密码区域)
            │   │   └── QHBoxLayout
            │   │       ├── QLineEdit (密码输入, EchoMode=Password)
            │   │       └── QToolButton (显示/隐藏密码)
            │   └── QWidget (确认密码区域)
            │       └── QHBoxLayout
            │           ├── QLineEdit (确认密码, EchoMode=Password)
            │           └── QLabel (匹配指示图标)
            ├── QPushButton (注册按钮)
            ├── QLabel (协议提示)
            └── QLabel (登录链接)
```

### 组件实现

| 组件 | 类型 | 说明 |
|------|------|------|
| m_emailEdit | QLineEdit | 邮箱输入框 |
| m_usernameEdit | QLineEdit | 用户名输入框 |
| m_passwordEdit | QLineEdit | 密码输入框 (EchoMode=Password) |
| m_confirmEdit | QLineEdit | 确认密码输入框 (EchoMode=Password) |
| m_toggleBtn | QToolButton | 密码显示/隐藏切换按钮 |
| m_matchIndicator | QLabel | 密码匹配指示器 (显示 ✓ 或隐藏) |
| m_registerButton | QPushButton | 注册按钮 |
| m_loginTab | QPushButton | 登录标签按钮 |
| m_registerTab | QPushButton | 注册标签按钮 (当前选中) |
| m_statusLabel | QLabel | 状态/错误信息显示 |

### QStyleSheet 样式

```css
/* 对话框 */
QDialog {
    background-color: #FAFAFC;
}

/* 左侧装饰区 */
QWidget#decorationPanel {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 #FFF0F5, stop:1 #E0F7FA);
}

/* 右侧表单卡片 */
QWidget#formCard {
    background-color: #FFFFFF;
    border-radius: 16px;
}

/* 输入框 */
QLineEdit {
    border: 1px solid #E2E8F0;
    border-radius: 12px;
    padding: 14px 16px;
    min-height: 24px;
    font-size: 14px;
    background-color: #FFFFFF;
}

QLineEdit:focus {
    border-color: #FF6B6B;
}

QLineEdit[error="true"] {
    border-color: #E53E3E;
    background-color: #FFF5F5;
}

/* Tab 按钮 */
QPushButton#tabButton {
    background-color: transparent;
    color: #718096;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 12px 24px;
    font-size: 14px;
    font-weight: 500;
}

QPushButton#tabButton:checked {
    color: #FF6B6B;
    border-bottom-color: #FF6B6B;
}

/* 注册按钮 */
QPushButton#registerButton {
    background-color: #FF6B6B;
    color: #FFFFFF;
    border: none;
    border-radius: 12px;
    padding: 14px 24px;
    font-size: 16px;
    font-weight: 600;
}

QPushButton#registerButton:hover {
    background-color: #FF8787;
}

QPushButton#registerButton:disabled {
    background-color: #CBD5E0;
}

/* 链接标签 */
QLabel#linkLabel {
    color: #FF6B6B;
}

QLabel#linkLabel:hover {
    text-decoration: underline;
}

/* 协议提示 */
QLabel#legalLabel {
    color: #718096;
    font-size: 12px;
}
```

### 交互实现

| 交互 | 实现方式 |
|------|----------|
| 居中显示 | `QDialog::move()` 计算屏幕中心位置 |
| 密码切换 | `QLineEdit::setEchoMode()` 切换 `Password` / `Normal` |
| 密码匹配检测 | 连接 `QLineEdit::textChanged` 信号，比较两个密码框内容 |
| Tab 切换 | `QButtonGroup` + `setCheckable(true)` 实现互斥 |
| 实时验证 | 连接 `QLineEdit::editingFinished` 信号触发验证 |
| 回车提交 | 连接 `QLineEdit::returnPressed` 信号到注册槽 |
| 加载动画 | `QMovie` + `QLabel` 或自定义 `QProgressIndicator` |
| 卡片阴影 | `QGraphicsDropShadowEffect` 设置阴影效果 |

---

## TUI 设计

TUI 客户端不提供注册功能，用户需通过桌面客户端或 Web 端完成注册。

详见：[TUI 功能需求规格](../tui/01-功能需求规格.md)

---

## 通用设计规范

### 输入验证

| 字段 | 验证规则 |
|------|----------|
| 用户名 | 4-32字符，仅含字母数字下划线 `[a-zA-Z0-9_]+` |
| 邮箱 | 合法邮箱格式 |
| 密码 | 8-64字符，仅含字母和数字，且需同时包含大小写字母和数字 |
| 确认密码 | 必须与密码一致（仅客户端验证，不提交到服务器） |

### 字段错误提示

| 字段 | 错误情况 | 提示信息 |
|------|----------|----------|
| 用户名 | 为空 | 请输入用户名 |
| 用户名 | 长度不足 | 用户名至少4个字符 |
| 用户名 | 长度超限 | 用户名最多32个字符 |
| 用户名 | 格式错误 | 用户名只能包含字母、数字和下划线 |
| 邮箱 | 为空 | 请输入邮箱 |
| 邮箱 | 格式错误 | 请输入有效的邮箱地址 |
| 密码 | 为空 | 请输入密码 |
| 密码 | 长度不足 | 密码至少8个字符 |
| 密码 | 长度超限 | 密码最多64个字符 |
| 密码 | 格式错误 | 密码需同时包含大小写字母和数字 |
| 确认密码 | 为空 | 请再次输入密码 |
| 确认密码 | 不匹配 | 两次输入的密码不一致 |

### 错误码处理

| 错误码 | 名称 | 用户提示 | UI 行为 |
|--------|------|----------|---------|
| 40001 | UsernameExists | 用户名已被注册 | 聚焦用户名输入框，显示错误 |
| 40002 | EmailExists | 邮箱已被注册 | 聚焦邮箱输入框，显示错误 |
| 10002 | ValidationFailed | 请检查输入信息 | 显示具体字段错误 |
| 10006 | InternalError | 服务器错误，请稍后重试 | 显示重试按钮 |

### 加载状态

- **按钮文字**：注册中...
- **按钮状态**：禁用
- **光标**：等待状态
- **动画**：显示加载指示器
- **输入框**：全部禁用

### 禁用状态

- 按钮背景色变为 `#CBD5E0`
- 文字色变为 `#A0AEC0`
- 鼠标悬停无效果
- 不响应点击事件

### 注册成功流程

1. 显示成功提示消息（Toast 或对话框）
2. 清空注册表单
3. 切换到登录标签页
4. 预填用户名输入框
5. 聚焦密码输入框

---

*最后更新: 2026-02-25*
