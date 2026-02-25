# 对话框组件设计

## 概述

对话框是 Disk 客户端中用于确认操作、收集用户输入和显示进度的通用组件。

视觉样式：默认使用目标框架的默认样式与系统配色；本文档仅约束布局与交互，不约束具体配色、字体或深浅色切换。
---

## Qt/QML 设计

### 确认对话框

```
┌─────────────────────────────────────────┐
│  ⚠️ 确认删除                             │
├─────────────────────────────────────────┤
│                                         │
│  确定要删除 "年度报告.docx" 吗？         │
│  此操作将移入回收站，可在 30 天内恢复。  │
│                                         │
├─────────────────────────────────────────┤
│                    [取消]    [确认删除]  │
└─────────────────────────────────────────┘
```

**规格**:
- 宽度: 400px
- 圆角: 8px
- 遮罩: rgba(0, 0, 0, 0.5)

### 输入对话框

```
┌─────────────────────────────────────────┐
│  📝 重命名                               │
├─────────────────────────────────────────┤
│                                         │
│  新名称                                  │
│  ┌───────────────────────────────────┐  │
│  │ 年度报告_v2.docx                  │  │
│  └───────────────────────────────────┘  │
│                                         │
├─────────────────────────────────────────┤
│                    [取消]    [确认]      │
└─────────────────────────────────────────┘
```

**输入框规格**:
- 边框: 1px solid
- 聚焦: Primary 边框
- 圆角: 4px
- 内边距: 12px

### 进度对话框

```
┌─────────────────────────────────────────┐
│  📤 上传中                               │
├─────────────────────────────────────────┤
│                                         │
│  文件: 项目文档.zip                      │
│  大小: 125.6 MB                         │
│                                         │
│  ████████████████████░░░░░░░░░░░░░░░░░░  67%
│                                         │
│  已上传: 84.2 MB │ 速度: 2.1 MB/s │ 剩余: 约 20 秒
│                                         │
├─────────────────────────────────────────┤
│                         [取消]          │
└─────────────────────────────────────────┘
```

**进度条规格**:
- 高度: 4px
- 圆角: 2px

---

## Qt Widget 设计

### 确认对话框 (QMessageBox)

```cpp
void MainWindow::showDeleteConfirm(const QString& filename) {
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("确认删除"));
    msgBox.setText(tr("确定要删除 \"%1\" 吗？").arg(filename));
    msgBox.setInformativeText(tr("此操作将移入回收站，可在 30 天内恢复。"));
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Cancel);
    msgBox.button(QMessageBox::Ok)->setText(tr("确认删除"));
    
    msgBox.exec();
}
```

### 输入对话框 (QDialog)

```cpp
class RenameDialog : public QDialog {
    Q_OBJECT
public:
    explicit RenameDialog(const QString& currentName, QWidget* parent = nullptr);
    QString GetNewName() const;
    
private slots:
    void OnTextChanged(const QString& text);
    void OnAccepted();
    
private:
    bool ValidateName(const QString& name);
    
    QLineEdit* m_nameEdit;
    QLabel* m_errorLabel;
    QPushButton* m_okButton;
    QString m_currentName;
};
```

**布局实现**:

```cpp
void RenameDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    
    // 标签
    QLabel* label = new QLabel(tr("新名称"));
    mainLayout->addWidget(label);
    
    // 输入框
    m_nameEdit = new QLineEdit();
    m_nameEdit->setText(m_currentName);
    m_nameEdit->selectAll();
    mainLayout->addWidget(m_nameEdit);
    
    // 错误标签
    m_errorLabel = new QLabel();
    m_errorLabel->setObjectName("errorLabel");
    mainLayout->addWidget(m_errorLabel);
    
    // 弹性空间
    mainLayout->addStretch();
    
    // 按钮区域
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    QPushButton* cancelBtn = new QPushButton(tr("取消"));
    m_okButton = new QPushButton(tr("确认"));
    m_okButton->setDefault(true);
    
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(m_okButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // 连接信号槽
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &RenameDialog::validateInput);
}
```

### 进度对话框 (QDialog + QProgressBar)

```cpp
class ProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProgressDialog(const QString& title, QWidget* parent = nullptr);
    
    void SetFileName(const QString& name);
    void SetFileSize(qint64 bytes);
    void SetProgress(int percent);
    void SetSpeed(const QString& speed);
    void SetRemainingTime(const QString& time);
    
private:
    QLabel* m_fileLabel;
    QLabel* m_sizeLabel;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QPushButton* m_cancelBtn;
};
```

---

## TUI 设计

### 确认对话框

```
┌─────────────────────────────────────────┐
│                         ⚠️ 确认删除      │
├─────────────────────────────────────────┤
│                                         │
│  确定要删除 "年度报告.docx" 吗？         │
│  此操作将移入回收站，可在 30 天内恢复。  │
│                                         │
├─────────────────────────────────────────┤
│                        [y]确认  [n]取消  │
└─────────────────────────────────────────┘
```

### 输入对话框

```
┌─────────────────────────────────────────┐
│                          📝 重命名       │
├─────────────────────────────────────────┤
│                                         │
│  ┌─ 新名称 ─────────────────────────┐  │
│  │ 年度报告_v2.docx_                │  │
│  └───────────────────────────────────┘  │
│                                         │
├─────────────────────────────────────────┤
│                        [Enter]确认  [Esc]取消
└─────────────────────────────────────────┘
```

### 进度对话框

```
┌─────────────────────────────────────────┐
│                        📤 上传中         │
├─────────────────────────────────────────┤
│                                         │
│  文件: 项目文档.zip                      │
│  大小: 125.6 MB                         │
│                                         │
│  ████████████████████░░░░░░░░░░░░░░░░░░  67%
│                                         │
│  已上传: 84.2 MB │ 速度: 2.1 MB/s │ 剩余: 约 20 秒
│                                         │
├─────────────────────────────────────────┤
│                         [x] 取消        │
└─────────────────────────────────────────┘
```

### 进度条样式

| 字符 | 用途 |
|------|------|
| `█` | 已完成部分 |
| `░` | 未完成部分 |
| `▓` | 错误/暂停状态 |

**长度计算**:
```
进度条长度 = min(可用宽度 - 百分比显示宽度, 40)
```

**示例**:
```
上传进度:
████████████████████░░░░░░░░░░░░░░░░░░░░  50%
████████████████████████████████████████  100% ✓
████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  20% (已暂停)
```

### 对话框快捷键

| 快捷键 | 功能 |
|--------|------|
| `Tab` | 切换焦点到下一个元素 |
| `Shift+Tab` | 切换焦点到上一个元素 |
| `Enter` | 确认/提交 |
| `Esc` / `q` | 取消/关闭 |
| `y` | 确认（是/否 对话框） |
| `n` | 取消（是/否 对话框） |

### 输入框组件

**基本结构**:
```
┌─ <标签> ────────────────────────────────────────────────────────┐
│ <输入内容>_                                                      │
└─────────────────────────────────────────────────────────────────┘
```

**状态样式**:

| 状态 | 边框颜色 | 说明 |
|------|----------|------|
| 普通 | 默认颜色 | 正常输入状态 |
| 聚焦 | 蓝色 | 当前输入焦点 |
| 错误 | 红色 | 输入验证失败 |
| 禁用 | 灰色 | 不可编辑 |

---

## 通用设计规范

### 对话框尺寸

| 类型 | 宽度 | 圆角 |
|------|------|------|
| 确认对话框 | 400px | 8px |
| 输入对话框 | 450px | 8px |
| 进度对话框 | 500px | 8px |

### 按钮布局

- **确认/危险操作**: 右对齐，主按钮在最右侧
- **顺序**: [取消] [确认] 或 [取消] [保存]

### 图标使用

| 类型 | 图标 | 说明 |
|------|------|------|
| 确认/警告 | ⚠️ | 需要确认的危险操作 |
| 信息 | ℹ️ | 信息提示 |
| 成功 | ✓ | 操作成功 |
| 错误 | ✗ | 操作失败 |
| 输入 | 📝 | 需要用户输入 |
| 上传 | 📤 | 上传操作 |
| 下载 | 📥 | 下载操作 |

### 按钮文字

| 操作 | 确认按钮 | 取消按钮 |
|------|----------|----------|
| 删除 | 确认删除 | 取消 |
| 重命名 | 确认 | 取消 |
| 保存 | 保存 | 取消 |
| 上传/下载 | - | 取消 |

---

*最后更新: 2026-02-19*
