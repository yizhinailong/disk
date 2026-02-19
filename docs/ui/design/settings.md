# 设置页面设计

## 概述

设置页面用于管理 Disk 客户端的配置选项，包括服务器设置、传输设置、外观设置等。

---

## Qt/QML 设计

### 界面布局

```
┌─────────────────────────────────────────────────────────────────────┐
│  ⚙ 设置                                                             │
├─────────────────────────────────────────────────────────────────────┤
│  ┌─ 服务器设置 ───────────────────────────────────────────────────┐ │
│  │                                                                │ │
│  │  服务器地址: [http://localhost:8080                         ]  │ │
│  │                                                                │ │
│  └────────────────────────────────────────────────────────────────┘ │
│  ┌─ 传输设置 ─────────────────────────────────────────────────────┐ │
│  │                                                                │ │
│  │  下载目录:   [~/Downloads                    ] [浏览...]       │ │
│  │  并发上传数: [3    ]                                          │ │
│  │  并发下载数: [3    ]                                          │ │
│  │                                                                │ │
│  └────────────────────────────────────────────────────────────────┘ │
│  ┌─ 外观设置 ─────────────────────────────────────────────────────┐ │
│  │                                                                │ │
│  │  主题:       [跟随系统 ▼]                                      │ │
│  │              ○ 跟随系统                                        │ │
│  │              ○ 浅色                                            │ │
│  │              ○ 深色                                            │ │
│  │                                                                │ │
│  │  [✓] 开机自启动                                               │ │
│  │  [✓] 最小化到系统托盘                                         │ │
│  │  [✓] 显示系统通知                                             │ │
│  │  [✓] 删除前确认                                               │ │
│  │                                                                │ │
│  └────────────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────────┤
│                    [恢复默认]              [取消]    [保存]          │
└─────────────────────────────────────────────────────────────────────┘
```

### 配置项分组

| 分组 | 配置项 |
|------|--------|
| 服务器设置 | 服务器地址 |
| 传输设置 | 下载目录、并发上传数、并发下载数 |
| 外观设置 | 主题、开机自启动、最小化到托盘、显示通知、删除确认 |

---

## Qt Widget 设计

### SettingsDialog 实现

```cpp
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    
private slots:
    void OnBrowseDownloadDir();
    void OnThemeChanged(int index);
    void OnAccept();
    void OnRestoreDefaults();
    
private:
    void LoadSettings();
    void SaveSettings();
    void ApplyTheme(const QString& theme);
    
    // 服务器设置
    QLineEdit* m_serverUrlEdit;
    
    // 下载设置
    QLineEdit* m_downloadDirEdit;
    QPushButton* m_browseBtn;
    QSpinBox* m_concurrentUploadsSpin;
    QSpinBox* m_concurrentDownloadsSpin;
    
    // 外观设置
    QComboBox* m_themeCombo;
    QCheckBox* m_autoStartCheck;
    QCheckBox* m_minimizeToTrayCheck;
    QCheckBox* m_showNotificationsCheck;
    QCheckBox* m_confirmDeleteCheck;
    
    // 按钮
    QPushButton* m_okBtn;
    QPushButton* m_cancelBtn;
    QPushButton* m_restoreDefaultsBtn;
};
```

### 布局结构

```cpp
void SettingsDialog::SetupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 服务器设置组
    QGroupBox* serverGroup = new QGroupBox(tr("服务器设置"));
    QFormLayout* serverLayout = new QFormLayout(serverGroup);
    m_serverUrlEdit = new QLineEdit();
    serverLayout->addRow(tr("服务器地址:"), m_serverUrlEdit);
    
    // 传输设置组
    QGroupBox* transferGroup = new QGroupBox(tr("传输设置"));
    QFormLayout* transferLayout = new QFormLayout(transferGroup);
    
    QHBoxLayout* downloadDirLayout = new QHBoxLayout();
    m_downloadDirEdit = new QLineEdit();
    m_browseBtn = new QPushButton(tr("浏览..."));
    downloadDirLayout->addWidget(m_downloadDirEdit);
    downloadDirLayout->addWidget(m_browseBtn);
    
    m_concurrentUploadsSpin = new QSpinBox();
    m_concurrentUploadsSpin->setRange(1, 10);
    m_concurrentDownloadsSpin = new QSpinBox();
    m_concurrentDownloadsSpin->setRange(1, 10);
    
    transferLayout->addRow(tr("下载目录:"), downloadDirLayout);
    transferLayout->addRow(tr("并发上传数:"), m_concurrentUploadsSpin);
    transferLayout->addRow(tr("并发下载数:"), m_concurrentDownloadsSpin);
    
    // 外观设置组
    QGroupBox* appearanceGroup = new QGroupBox(tr("外观设置"));
    QVBoxLayout* appearanceLayout = new QVBoxLayout(appearanceGroup);
    
    m_themeCombo = new QComboBox();
    m_themeCombo->addItems({tr("跟随系统"), tr("浅色"), tr("深色")});
    
    m_autoStartCheck = new QCheckBox(tr("开机自启动"));
    m_minimizeToTrayCheck = new QCheckBox(tr("最小化到系统托盘"));
    m_showNotificationsCheck = new QCheckBox(tr("显示系统通知"));
    m_confirmDeleteCheck = new QCheckBox(tr("删除前确认"));
    
    appearanceLayout->addWidget(new QLabel(tr("主题:")));
    appearanceLayout->addWidget(m_themeCombo);
    appearanceLayout->addSpacing(8);
    appearanceLayout->addWidget(m_autoStartCheck);
    appearanceLayout->addWidget(m_minimizeToTrayCheck);
    appearanceLayout->addWidget(m_showNotificationsCheck);
    appearanceLayout->addWidget(m_confirmDeleteCheck);
    
    // 添加到主布局
    mainLayout->addWidget(serverGroup);
    mainLayout->addWidget(transferGroup);
    mainLayout->addWidget(appearanceGroup);
    mainLayout->addStretch();
    
    // 按钮区域
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_restoreDefaultsBtn = new QPushButton(tr("恢复默认"));
    m_cancelBtn = new QPushButton(tr("取消"));
    m_okBtn = new QPushButton(tr("保存"));
    m_okBtn->setDefault(true);
    
    buttonLayout->addWidget(m_restoreDefaultsBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelBtn);
    buttonLayout->addWidget(m_okBtn);
    
    mainLayout->addLayout(buttonLayout);
}
```

### QStyleSheet 样式

```css
SettingsDialog QGroupBox {
    font-weight: bold;
    border: 1px solid #E0E0E0;
    border-radius: 4px;
    margin-top: 12px;
    padding-top: 8px;
}

SettingsDialog QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 8px;
    background-color: white;
}

SettingsDialog QLineEdit,
SettingsDialog QSpinBox {
    border: 1px solid #E0E0E0;
    border-radius: 4px;
    padding: 6px;
    min-width: 200px;
}

SettingsDialog QPushButton {
    background-color: #2196F3;
    color: white;
    border: none;
    border-radius: 4px;
    padding: 8px 16px;
}

SettingsDialog QPushButton:hover {
    background-color: #1976D2;
}
```

### AppConfig 配置管理

```cpp
class AppConfig : public QObject {
    Q_OBJECT
public:
    static AppConfig* Instance();
    
    QString ServerUrl() const;
    void SetServerUrl(const QString& url);
    
    QString DownloadDir() const;
    void SetDownloadDir(const QString& dir);
    
    int ConcurrentUploads() const;
    void SetConcurrentUploads(int count);
    
    int ConcurrentDownloads() const;
    void SetConcurrentDownloads(int count);
    
    bool AutoStart() const;
    bool MinimizeToTray() const;
    bool ShowNotifications() const;
    bool ConfirmDelete() const;
    
    QString Theme() const;
    void SetTheme(const QString& theme);
    
signals:
    void ThemeChanged(const QString& theme);
    void SettingsChanged();
    
private:
    QSettings m_settings;
};
```

---

## TUI 设计

### 界面布局

```
┌─────────────────────────────────────────┐
│             系统设置                     │
├─────────────────────────────────────────┤
│                                         │
│  服务器设置                              │
│  ─────────────────────────────────────  │
│  服务器地址: [http://localhost:8080    ] │
│                                         │
│  传输设置                                │
│  ─────────────────────────────────────  │
│  下载目录:   [~/Downloads              ] │
│  并发上传数: [3]                        │
│  并发下载数: [3]                        │
│  自动刷新令牌: [x]                      │
│                                         │
│        [ 保存 ]     [ 取消 ]            │
└─────────────────────────────────────────┘
```

### TUI 配置项

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| server_url | string | - | 服务器地址 |
| download_dir | string | ~/Downloads | 默认下载目录 |
| concurrent_uploads | integer | 3 | 并发上传数 |
| concurrent_downloads | integer | 3 | 并发下载数 |
| chunk_size | integer | 5242880 | 分片大小（5MB） |
| auto_refresh_token | boolean | true | 自动刷新令牌 |
| theme | string | default | 主题 |

### 配置文件位置

| 平台 | 路径 |
|------|------|
| Linux/macOS | `~/.config/disk-tui/config.json` |
| Windows | `%APPDATA%\disk-tui\config.json` |

### 专用快捷键

| 快捷键 | 功能 |
|--------|------|
| `Tab` | 切换到下一个配置项 |
| `Shift+Tab` | 切换到上一个配置项 |
| `Enter` | 编辑当前项 / 保存 |
| `Esc` | 取消 / 返回 |

---

## 通用设计规范

### 完整配置项列表

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| server_url | string | - | 服务器地址 |
| download_dir | string | ~/Downloads | 默认下载目录 |
| concurrent_uploads | integer | 3 | 并发上传数 |
| concurrent_downloads | integer | 3 | 并发下载数 |
| auto_start | boolean | false | 开机自启动 |
| minimize_to_tray | boolean | true | 最小化到托盘 |
| show_notifications | boolean | true | 显示系统通知 |
| confirm_delete | boolean | true | 删除前确认 |
| theme | string | system | 主题：system/light/dark |
| chunk_size | integer | 5242880 | 分片大小（5MB） |
| auto_refresh_token | boolean | true | 自动刷新令牌 |

### 主题选项

| 选项 | 说明 |
|------|------|
| system | 跟随系统主题 |
| light | 强制浅色主题 |
| dark | 强制深色主题 |

### 验证规则

| 配置项 | 验证规则 |
|--------|----------|
| server_url | 必须是有效的 URL |
| download_dir | 必须是有效的目录路径 |
| concurrent_uploads | 1-10 |
| concurrent_downloads | 1-10 |

### 保存确认

保存设置时显示确认提示：

```
┌─────────────────────────────────────────┐
│  ✓ 设置已保存                           │
└─────────────────────────────────────────┘
```

---

*最后更新: 2026-02-19*
