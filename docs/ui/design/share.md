# 分享管理页面设计

## 概述

分享管理页面用于创建、查看和管理文件分享链接，支持设置有效期和访问密码。

视觉样式：默认使用目标框架的默认样式与系统配色；本文档仅约束布局与交互，不约束具体配色、字体或深浅色切换。
---

## Qt/QML 设计

### 创建分享对话框

```
┌─────────────────────────────────────────┐
│            🔗 创建分享                   │
├─────────────────────────────────────────┤
│                                         │
│  文件: 报告.pdf, 项目文档 (2 项)         │
│                                         │
│  有效期: [7 天 ▼]                        │
│    • 1 天                               │
│    • 7 天                               │
│    • 30 天                              │
│    • 永久                               │
│                                         │
│  访问密码: [        ] (留空则无密码)    │
│                                         │
├─────────────────────────────────────────┤
│                 [取消]    [创建分享]     │
└─────────────────────────────────────────┘
```

### 分享创建成功

```
┌─────────────────────────────────────────┐
│            ✓ 分享已创建                  │
├─────────────────────────────────────────┤
│                                         │
│  分享链接:                              │
│  ┌─────────────────────────────────────┐│
│  │ https://disk.app/s/abc123xyz       ││
│  └─────────────────────────────────────┘│
│                                         │
│  访问密码: xK9m                         │
│  有效期: 7 天 (至 2026-02-26)           │
│                                         │
│  [📋 复制链接]  [📋 复制链接和密码]      │
│                                         │
├─────────────────────────────────────────┤
│                              [关闭]      │
└─────────────────────────────────────────┘
```

### 分享列表

```
┌─────────────────────────────────────────────────────────────────────┐
│  我的分享                                                           │
├─────────────────────────────────────────────────────────────────────┤
│  名称          密码   访问次数   下载次数   创建时间   状态          │
├─────────────────────────────────────────────────────────────────────┤
│  📄 报告.pdf    🔒     25         10        01-10     有效 (5天)    │
│  📁 项目文档    -      12         3         01-08     已过期        │
│  📄 数据.xlsx   🔒     8          2         01-05     永久          │
├─────────────────────────────────────────────────────────────────────┤
│  [复制链接]  [取消分享]                                              │
└─────────────────────────────────────────────────────────────────────┘
```

### 列表字段

| 字段 | 说明 |
|------|------|
| 名称 | 分享的文件/文件夹名称 |
| 密码 | 是否有密码保护 |
| 访问次数 | 分享被访问的次数 |
| 下载次数 | 分享被下载的次数 |
| 创建时间 | 分享创建时间 |
| 过期时间 | 分享过期时间 |
| 状态 | 有效/已过期 |

---

## Qt Widget 设计

### CreateShareDialog 实现

```cpp
class CreateShareDialog : public QDialog {
    Q_OBJECT
public:
    explicit CreateShareDialog(const QList<FileItem>& items, QWidget* parent = nullptr);
    
    struct ShareResult {
        QString shareLink;
        QString password;
        QDateTime expireTime;
    };
    
    ShareResult GetResult() const;
    
private slots:
    void OnCreateShare();
    void OnCopyLink();
    void OnExpireChanged(int index);
    void OnPasswordToggled(bool enabled);
    
private:
    void SetupUi();
    
    QLabel* m_fileListLabel;
    QComboBox* m_expireCombo;
    QCheckBox* m_passwordCheck;
    QLineEdit* m_passwordEdit;
    QTextEdit* m_resultText;
    QPushButton* m_copyBtn;
    QPushButton* m_closeBtn;
    
    ShareResult m_result;
};
```

### ShareListView 实现

```cpp
class ShareListView : public QWidget {
    Q_OBJECT
public:
    explicit ShareListView(QWidget* parent = nullptr);
    void RefreshList();
    
signals:
    void CancelShareRequested(const QString& shareCode);
    void CopyShareLinkRequested(const QString& shareCode);
    
private slots:
    void OnCancelShare();
    void OnCopyLink();
    void OnSelectionChanged(const QItemSelection& selected);
    
private:
    QTableView* m_tableView;
    ShareListModel* m_model;
    QPushButton* m_cancelBtn;
    QPushButton* m_copyLinkBtn;
};
```

### ShareListModel 数据模型

```cpp
class ShareListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { FileName, HasPassword, AccessCount, DownloadCount, CreatedAt, ExpiresAt, Status };
    
    void LoadShares();
    void RemoveShare(const QString& shareCode);
    
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    
private:
    QList<ShareItem> m_shares;
};
```

### 取消分享确认

```cpp
void ShareListView::OnCancelShare() {
    QModelIndexList selected = m_tableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) return;
    
    QString shareCode = m_model->GetShareCode(selected.first().row());
    
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setWindowTitle(tr("取消分享"));
    msgBox.setText(tr("确定要取消此分享吗？\n取消后分享链接将立即失效。"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    
    if (msgBox.exec() == QMessageBox::Yes) {
        m_apiClient->CancelShare(shareCode, [this, shareCode](ApiResponse response) {
            if (response.IsSuccess()) {
                m_model->RemoveShare(shareCode);
                ShowNotification(tr("分享已取消"));
            } else {
                ShowError(response.ErrorMessage());
            }
        });
    }
}
```

---

## TUI 设计

### 创建分享界面

```
┌─────────────────────────────────────────┐
│            创建分享                      │
├─────────────────────────────────────────┤
│ 文件: 报告.pdf                          │
│                                         │
│ 有效期: [7天 ▼]                         │
│   - 1 天                                │
│   - 7 天                                │
│   - 30 天                               │
│   - 永久                                │
│                                         │
│ 访问密码: [        ] (留空则无密码)     │
│                                         │
│         [ 取消 ]    [ 创建 ]            │
└─────────────────────────────────────────┘
```

### 分享列表界面

```
┌───────────────────────────────────────────────────────────────┐
│                          我的分享                             │
├───────────────────────────────────────────────────────────────┤
│   名称          密码   访问   下载   创建时间   状态          │
├───────────────────────────────────────────────────────────────┤
│ 📄 报告.pdf    🔒     25     10    01-10     有效 (5天)      │
│ 📁 项目文档    -      12      3    01-08     已过期          │
│ 📄 数据.xlsx   🔒     8       2    01-05     永久            │
├───────────────────────────────────────────────────────────────┤
│ [Enter 复制链接] [c 取消分享] [q 返回]                       │
└───────────────────────────────────────────────────────────────┘
```

### 专用快捷键

| 快捷键 | 功能 |
|--------|------|
| `Enter` | 复制分享链接 |
| `c` | 取消分享 |
| `e` | 编辑分享设置 |
| `q` | 返回文件列表 |

### 分享链接复制成功提示

```
┌───────────────────────────────────────────────────────────────┐
│ ✓ 已复制到剪贴板                                              │
│   https://disk.app/s/abc123xyz                               │
└───────────────────────────────────────────────────────────────┘
```

---

## 通用设计规范

### 分享参数

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| file_ids | array | 是 | - | 文件/文件夹 ID 列表 |
| expire_days | integer | 否 | 7 | 有效天数：0/1/7/30 |
| password | string | 否 | - | 访问密码（4-8字符） |
| permission | string | 否 | download | 权限：view/download |

### 有效期选项

| 选项 | 天数 | 说明 |
|------|------|------|
| 1 天 | 1 | 短期分享 |
| 7 天 | 7 | 默认选项 |
| 30 天 | 30 | 长期分享 |
| 永久 | 0 | 永不过期 |

### 状态显示

| 状态 | 显示 |
|------|------|
| 有效 | 有效 (N天) |
| 即将过期 | 即将过期 (N天) |
| 已过期 | 已过期 |
| 永久 | 永久 |

### 密码指示

| 状态 | 显示 | 说明 |
|------|------|------|
| 有密码 | 🔒 | 密码保护 |
| 无密码 | - | 公开访问 |

### 确认对话框

#### 取消分享确认

```
┌─────────────────────────────────────────┐
│  ⚠️ 取消分享                             │
├─────────────────────────────────────────┤
│                                         │
│  确定要取消此分享吗？                    │
│  取消后分享链接将立即失效。              │
│                                         │
├─────────────────────────────────────────┤
│                    [取消]    [确认]      │
└─────────────────────────────────────────┘
```

---

*最后更新: 2026-02-19*
