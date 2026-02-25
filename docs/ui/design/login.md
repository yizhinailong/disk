# 登录页面设计

## 概述

登录页面是用户进入 Disk 网盘系统的入口，支持用户名或邮箱登录。桌面端采用明亮温暖的双区布局设计，左侧装饰区营造氛围，右侧表单卡片承载核心交互。

---

## Qt/QML 设计

### 界面布局

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                                                                              │
│  ┌─────────────────────────────┐  ┌──────────────────────────────────────┐  │
│  │                             │  │                                      │  │
│  │                             │  │         [Logo] 64-80px               │  │
│  │     渐变背景                 │  │                                      │  │
│  │   #FFF0F5 → #E0F7FA         │  │         欢迎回来                      │  │
│  │                             │  │       登录你的账号                    │  │
│  │    ○ 半透明白色光斑          │  │                                      │  │
│  │     (装饰性椭圆)             │  │   ┌────────────────────────────┐    │  │
│  │                             │  │   │ 登录 │ 注册 │  ← 胶囊切换    │    │  │
│  │                             │  │   └────────────────────────────┘    │  │
│  │                             │  │                                      │  │
│  │                             │  │   账号（用户名/邮箱）                 │  │
│  │                             │  │   ┌────────────────────────────┐    │  │
│  │                             │  │   │                            │ 👁  │  │
│  │                             │  │   └────────────────────────────┘    │  │
│  │                             │  │                                      │  │
│  │                             │  │   密码                               │  │
│  │  "欢迎回来！今天也要          │  │   ┌────────────────────────────┐    │  │
│  │   元气满满哦✨"              │  │   │ ••••••••                    │ 👁  │  │
│  │                             │  │   └────────────────────────────┘    │  │
│  │                             │  │                                      │  │
│  │                             │  │   ┌────────────────────────────┐    │  │
│  │                             │  │   │          登 录              │    │  │
│  │                             │  │   └────────────────────────────┘    │  │
│  │                             │  │                                      │  │
│  │                             │  │   忘记密码？      没有账号？立即注册   │  │
│  │                             │  │   (可选/未实现)                       │  │
│  │                             │  │                                      │  │
│  │                             │  │   ─────── 其他方式登录 ───────       │  │
│  │                             │  │   [ 微信 ]  [ QQ ]  (可选/规划中)     │  │
│  └─────────────────────────────┘  └──────────────────────────────────────┘  │
│                                                                              │
│       左侧装饰区 (40-50%)                     右侧表单卡片 (420px)            │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 组件规格

| 组件 | 属性 | 规范 |
|------|------|------|
| Logo | 尺寸 | 64-80 px |
| 主标题 | 文号 | 24px, 粗体, #2D3748 |
| 副标题 | 字号 | 14px, #718096 |
| 登录/注册切换 | 样式 | 胶囊式 Tab，选中态 Primary 背景 + 白字 |
| 输入框 | 高度 | 52px |
| 输入框 | 圆角 | 12px |
| 输入框 | 内边距 | 16px |
| 登录按钮 | 高度 | 52px |
| 登录按钮 | 圆角 | 12px |
| 表单卡片 | 宽度 | 420px |
| 表单卡片 | 圆角 | 16px |
| 表单卡片 | 阴影 | 柔和悬浮阴影 |
| 窗口 | 最小宽度 | 900px |
| 窗口 | 居中 | ✓ |

### 颜色规范

| 元素 | 属性 | 值 |
|------|------|-----|
| 窗口背景 | 背景色 | #FAFAFC |
| 左侧装饰区 | 渐变 | #FFF0F5 (左上) → #E0F7FA (右下) |
| 表单卡片 | 背景色 | #FFFFFF |
| 表单卡片 | 阴影 | 0 8px 32px rgba(0,0,0,0.08) |
| 输入框 | 背景色 | #FAFAFC |
| 输入框 | 边框 | 1px solid #E2E8F0 |
| 输入框聚焦 | 边框色 | #FF6B6B |
| 输入框聚焦 | 光晕 | 0 0 0 3px rgba(255,107,107,0.3) |
| 主按钮 | 背景色 | #FF6B6B |
| 主按钮 | 文字色 | #FFFFFF |
| 主按钮悬停 | 背景色 | #FF8787 |
| 主按钮悬停 | 缩放 | 1.03-1.05 |
| 主按钮禁用 | 背景色 | #E2E8F0 |
| 主按钮禁用 | 文字色 | #718096 |
| 文字主色 | - | #2D3748 |
| 文字辅色 | - | #718096 |
| 链接文字 | - | #FF6B6B |
| 边框 | - | #E2E8F0 |
| 强调色 | Secondary | #4ECDC4 |

### 交互设计

- **登录/注册 Tab 切换**：点击切换表单模式，选中态为 Primary 背景色配白色文字
- **密码显示/隐藏**：点击眼睛图标切换明文/密文
- **回车登录**：在任意输入框按回车触发登录
- **加载状态**：登录中显示加载动画，按钮禁用
- **错误提示**：登录失败在输入框下方显示错误信息（红色）
- **悬停效果**：
  - 输入框悬停：边框色加深
  - 按钮悬停：背景色提亮 8-12%，轻微放大 (scale 1.03-1.05)
  - 链接悬停：添加下划线

### 装饰区设计

**左侧装饰区 (40-50% 宽度)**：
- 渐变背景：从左上 `#FFF0F5` (淡粉) 过渡到右下 `#E0F7FA` (淡青)
- 光斑层：半透明白色圆形/椭圆，营造柔和光感
- 欢迎语：居中偏下，`"欢迎回来！今天也要元气满满哦✨"`，字号 18-20px，#2D3748

---

## Qt Widget 设计

### 界面布局

使用 `QDialog` + `QHBoxLayout` 实现双区布局：

```
QDialog (最小 900x600, 居中显示)
└── QHBoxLayout (无间距)
    ├── QWidget (左侧装饰面板 m_decoPanel, stretch=1)
    │   └── QVBoxLayout
    │       ├── QSpacerItem (弹性空间)
    │       ├── QLabel (欢迎文字)
    │       └── QSpacerItem (弹性空间)
    └── QFrame (右侧表单卡片 m_formCard, 固定 420px)
        └── QVBoxLayout
            ├── QWidget (Logo 区域)
            │   └── QHBoxLayout
            │       ├── QLabel (Logo)
            │       └── QLabel (标题)
            ├── QWidget (登录/注册切换 Tab)
            │   └── QHBoxLayout
            │       ├── QPushButton (登录 Tab)
            │       └── QPushButton (注册 Tab)
            ├── QFormLayout (表单区)
            │   ├── QLabel (账号标签)
            │   ├── QWidget (账号输入区域)
            │   │   └── QHBoxLayout
            │   │       ├── QLineEdit (账号输入)
            │   │       └── QToolButton (清除按钮)
            │   ├── QLabel (密码标签)
            │   └── QWidget (密码区域)
            │       └── QHBoxLayout
            │           ├── QLineEdit (密码输入, EchoMode=Password)
            │           └── QToolButton (显示/隐藏密码)
            ├── QPushButton (登录按钮)
            ├── QWidget (辅助链接区)
            │   └── QHBoxLayout
            │       ├── QPushButton (忘记密码? - 可选/未实现)
            │       └── QPushButton (没有账号? 立即注册)
            ├── QWidget (第三方登录 - 可选/规划中)
            │   └── QVBoxLayout
            │       ├── QLabel (分隔文字)
            │       └── QWidget (第三方按钮组)
            └── QSpacerItem (弹性空间)
```

### 组件实现

| 组件 | 类型 | 说明 |
|------|------|------|
| m_decoPanel | QWidget | 左侧装饰面板，带渐变背景 |
| m_formCard | QFrame | 右侧表单卡片，白色背景+圆角+阴影 |
| m_tabLogin | QPushButton | 登录 Tab 按钮 |
| m_tabRegister | QPushButton | 注册 Tab 按钮 |
| m_accountEdit | QLineEdit | 账号输入框 |
| m_passwordEdit | QLineEdit | 密码输入框 (EchoMode=Password) |
| m_loginButton | QPushButton | 登录按钮 |
| m_togglePwdBtn | QToolButton | 密码显示/隐藏切换按钮 |
| m_forgotLink | QPushButton | 忘记密码链接 (可选/未实现) |
| m_registerLink | QPushButton | 立即注册链接 |
| m_statusLabel | QLabel | 状态/错误信息显示 |

### QStyleSheet 样式

```css
/* 对话框 */
QDialog {
    background-color: #FAFAFC;
}

/* 左侧装饰面板 */
#decoPanel {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 #FFF0F5, stop:1 #E0F7FA);
}

/* 右侧表单卡片 */
#formCard {
    background-color: #FFFFFF;
    border-radius: 16px;
    min-width: 420px;
    max-width: 420px;
}

/* 提示：卡片阴影使用 QGraphicsDropShadowEffect 实现 */

/* 输入框 */
QLineEdit {
    background-color: #FAFAFC;
    border: 1px solid #E2E8F0;
    border-radius: 12px;
    padding: 0 16px;
    min-height: 52px;
    font-size: 15px;
    color: #2D3748;
}

QLineEdit:hover {
    border-color: #CBD5E0;
}

QLineEdit:focus {
    border-color: #FF6B6B;
}

/* 主按钮 */
QPushButton#loginButton {
    background-color: #FF6B6B;
    color: #FFFFFF;
    border: none;
    border-radius: 12px;
    min-height: 52px;
    font-size: 15px;
    font-weight: 600;
}

QPushButton#loginButton:hover {
    background-color: #FF8787;
}

QPushButton#loginButton:disabled {
    background-color: #E2E8F0;
    color: #718096;
}

/* Tab 切换按钮 */
QPushButton#tabLogin, QPushButton#tabRegister {
    background-color: transparent;
    color: #718096;
    border: none;
    border-radius: 20px;
    padding: 10px 24px;
    font-size: 14px;
}

QPushButton#tabLogin:checked, QPushButton#tabRegister:checked {
    background-color: #FF6B6B;
    color: #FFFFFF;
}

/* 链接按钮 */
QPushButton#forgotLink, QPushButton#registerLink {
    background-color: transparent;
    color: #FF6B6B;
    border: none;
    font-size: 13px;
}

QPushButton#forgotLink:hover, QPushButton#registerLink:hover {
    text-decoration: underline;
}

/* 标签 */
QLabel {
    color: #2D3748;
    font-size: 14px;
}

QLabel#mutedLabel {
    color: #718096;
}

/* 错误信息 */
QLabel#errorLabel {
    color: #E53E3E;
    font-size: 13px;
}
```

### 阴影效果

```cpp
// 表单卡片悬浮阴影
auto* shadowEffect = new QGraphicsDropShadowEffect(m_formCard);
shadowEffect->setBlurRadius(32);
shadowEffect->setColor(QColor(0, 0, 0, 20));  // 8% opacity
shadowEffect->setOffset(0, 8);
m_formCard->setGraphicsEffect(shadowEffect);
```

### 交互实现

| 交互 | 实现方式 |
|------|----------|
| 居中显示 | `QDialog::move()` 计算屏幕中心位置 |
| Tab 切换 | `QButtonGroup` + `setExclusive(true)`，切换时更新表单 |
| 密码切换 | `QLineEdit::setEchoMode()` 切换 `Password` / `Normal` |
| 回车登录 | 连接 `QLineEdit::returnPressed` 信号到登录槽 |
| 加载动画 | `QMovie` + `QLabel` 或 `QProgressIndicator` |
| 悬停缩放 | `QPropertyAnimation` 或 CSS `transform` (QML) |

---

## TUI 设计

### 界面布局

```
┌─────────────────────────────────────────────┐
│              Disk TUI Client                │
│                                             │
│  账号: [________________________]           │
│  密码: [________________________]           │
│                                             │
│  [ ] 记住我                                 │
│                                             │
│           [ 登 录 ]                         │
│                                             │
│  提示: 新用户请在 Web 端注册                │
└─────────────────────────────────────────────┘
```

**说明**: TUI 不提供注册页面；新用户请在 Web 端注册。

### 组件规格

| 组件 | 高度 | 说明 |
|------|------|------|
| 标题区 | 3 行 | Logo + 应用名称 |
| 输入框 | 1 行 | 带边框的输入区域 |
| 复选框 | 1 行 | 记住我选项 |
| 按钮 | 1 行 | 登录按钮 |
| 提示区 | 2 行 | 注册提示信息 |
| 总高度 | ~13 行 | 最小终端高度要求 |

### 颜色方案

| 元素 | 16色 | 256色 | 24位色 | 说明 |
|------|------|-------|--------|------|
| 标题 | Blue (4) | 33 | #FF6B6B | 应用名称 (Primary) |
| 输入框边框 | Default | - | - | 默认边框颜色 |
| 聚焦边框 | Blue (4) | 33 | #FF6B6B | 当前输入焦点 (Primary) |
| 错误边框 | Red (1) | 160 | #dc322f | 验证失败 |
| 按钮背景 | Blue (4) | 33 | #FF6B6B | 登录按钮 (Primary) |
| 按钮文字 | White (7) | 231 | #ffffff | 按钮文字 |
| 提示文字 | Gray | 244 | #718096 | 辅助提示 (Muted) |

### 交互设计

| 按键 | 功能 |
|------|------|
| `Tab` | 切换输入焦点 |
| `Shift+Tab` | 反向切换焦点 |
| `Enter` | 提交登录 / 切换复选框 |
| `Space` | 切换复选框状态 |
| `Esc` | 退出程序 |

### 状态反馈

| 状态 | 显示 |
|------|------|
| 登录中 | 按钮显示 "登录中..." 并禁用 |
| 成功 | 跳转到主界面 |
| 失败 | 在按钮下方显示红色错误信息 |

---

## 通用设计规范

### 输入验证

| 字段 | 验证规则 |
|------|----------|
| 账号 | 非空，支持用户名或邮箱格式 |
| 密码 | 非空 |

### 错误提示

| 错误类型 | 提示信息 |
|----------|----------|
| 用户不存在 | 用户不存在 |
| 密码错误 | 用户名或密码错误 |
| 账户锁定 | 账户已锁定，请稍后重试 |
| 网络错误 | 网络连接失败，请检查网络 |
| 服务不可用 | 服务暂时不可用，请稍后重试 |

### 加载状态

- **按钮文字**：登录中...
- **按钮状态**：禁用
- **光标**：等待状态
- **动画**：显示加载指示器

### 可选功能标注

| 功能 | 状态 | 说明 |
|------|------|------|
| 忘记密码 | 可选/未实现 | 链接显示但暂不提供功能 |
| 第三方登录 | 可选/规划中 | 微信、QQ 等登录方式预留 |
| 立即注册 | 已实现 | 点击切换到注册表单 (桌面端) |
| Web 端注册 | 已实现 | TUI 引导用户去 Web 注册 |

---

*最后更新: 2026-02-25*
