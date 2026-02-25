# 回收站页面设计

## 概述

回收站页面展示已删除的文件和文件夹，支持恢复、彻底删除和清空回收站操作。

视觉样式：默认使用目标框架的默认样式与系统配色；本文档仅约束布局与交互，不约束具体配色、字体或深浅色切换。
---

## Qt/QML 设计

### 界面布局

```
┌─────────────────────────────────────────────────────────────────────┐
│  🗑️ 回收站                                                          │
├─────────────────────────────────────────────────────────────────────┤
│  名称              原始位置           大小      删除时间    剩余天数  │
├─────────────────────────────────────────────────────────────────────┤
│  📄 报告.pdf       /文档/工作         2.3 MB   2026-01-10   25天    │
│  📁 旧项目         /项目              15 MB    2026-01-08   23天    │
│  📄 数据.xlsx      /文档              890 KB   2026-01-05   20天    │
├─────────────────────────────────────────────────────────────────────┤
│  [恢复选中]  [彻底删除]  [清空回收站]                                │
└─────────────────────────────────────────────────────────────────────┘
```

### 列表字段

| 字段 | 说明 |
|------|------|
| 名称 | 文件/文件夹名称 |
| 原始位置 | 删除前的路径 |
| 大小 | 文件大小 |
| 删除时间 | 移入回收站的时间 |
| 剩余天数 | 自动删除倒计时（30天） |

### 操作按钮

| 按钮 | 功能 | 说明 |
|------|------|------|
| 恢复选中 | 恢复选中的文件 | 恢复到原位置 |
| 彻底删除 | 永久删除选中文件 | 需确认 |
| 清空回收站 | 删除所有回收站内容 | 需二次确认 |

---

## Qt Widget 设计

### TrashView 实现

```cpp
class TrashView : public QWidget {
    Q_OBJECT
public:
    explicit TrashView(QWidget* parent = nullptr);
    void RefreshList();
    
signals:
    void RestoreRequested(const QList<qint64>& itemIds);
    void DeletePermanentlyRequested(const QList<qint64>& itemIds);
    
private slots:
    void OnRestore();
    void OnDeletePermanently();
    void OnSelectionChanged(const QItemSelection& selected);
    
private:
    QTableView* m_tableView;
    TrashModel* m_model;
    QPushButton* m_restoreBtn;
    QPushButton* m_deleteBtn;
    QPushButton* m_emptyTrashBtn;
};
```

### TrashModel 数据模型

```cpp
class TrashModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name, OriginalPath, Size, DeletedAt, DaysRemaining };
    
    void LoadTrashItems();
    void RemoveItems(const QList<qint64>& itemIds);
    
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    
private:
    QList<TrashItem> m_items;
};
```


### 彻底删除确认

```cpp
bool TrashView::ConfirmPermanentDelete(int itemCount) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(tr("彻底删除"));
    msgBox.setText(tr("确定要彻底删除选中的 %1 个项目吗？\n\n此操作无法撤销！").arg(itemCount));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    
    return msgBox.exec() == QMessageBox::Yes;
}
```

### 清空回收站确认

```cpp
void TrashView::OnEmptyTrash() {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(tr("清空回收站"));
    msgBox.setText(tr("确定要清空回收站吗？\n\n此操作将永久删除所有项目，无法撤销！"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    
    if (msgBox.exec() == QMessageBox::Yes) {
        m_apiClient->EmptyTrash([this](ApiResponse response) {
            if (response.IsSuccess()) {
                RefreshList();
                ShowNotification(tr("回收站已清空"));
            }
        });
    }
}
```

---

## TUI 设计

### 界面布局

```
┌───────────────────────────────────────────────────────────────┐
│                          回收站                               │
├───────────────────────────────────────────────────────────────┤
│   名称              大小      原始位置        删除时间   剩余  │
├───────────────────────────────────────────────────────────────┤
│ 📄 报告.pdf        2.3 MB   /文档/工作      01-10      25天  │
│ 📁 旧项目          15 MB    /项目           01-08      23天  │
│ 📄 数据.xlsx       890 KB   /文档           01-05      20天  │
├───────────────────────────────────────────────────────────────┤
│ [r 恢复] [x 彻底删除] [l 清空回收站] [q 返回]                │
└───────────────────────────────────────────────────────────────┘
```

### 列表字段

| 字段 | 类型 | 显示 | 说明 |
|------|------|------|------|
| id | integer | - | 回收站记录 ID |
| name | string | ✓ | 名称 |
| type | string | ✓ | 类型图标 |
| size | integer | ✓ | 大小 |
| original_path | string | ✓ | 原始路径 |
| deleted_at | datetime | ✓ | 删除时间 |
| expires_at | datetime | ✓ | 过期时间 |

### 专用快捷键

| 快捷键 | 功能 |
|--------|------|
| `r` | 恢复选中项 |
| `x` | 彻底删除选中项 |
| `E` | 清空回收站（需确认） |
| `q` | 返回文件列表 |


### 即将过期提示

```
┌───────────────────────────────────────────────────────────────┐
│                          回收站                               │
├───────────────────────────────────────────────────────────────┤
│   名称              大小      原始位置        删除时间   剩余  │
├───────────────────────────────────────────────────────────────┤
│ 📄 报告.pdf        2.3 MB   /文档/工作      01-10      25天  │
│ 📁 旧项目          15 MB    /项目           01-08       3天  │ ← 红色警示
│ 📄 数据.xlsx       890 KB   /文档           01-05       1天  │ ← 红色警示
├───────────────────────────────────────────────────────────────┤
│ ⚠️ 有 2 个项目将在 7 天内自动删除                             │
├───────────────────────────────────────────────────────────────┤
│ [r 恢复] [x 彻底删除] [l 清空回收站] [q 返回]                │
└───────────────────────────────────────────────────────────────┘
```

---

## 通用设计规范

### 业务规则

| 规则 | 说明 |
|------|------|
| 保留期限 | 30 天后自动彻底删除 |
| 排序 | 按删除时间倒序排列 |
| 恢复位置 | 优先恢复到原始位置，不存在则恢复到根目录 |
| 重名处理 | 存在同名文件时自动重命名 |

### 确认对话框

#### 恢复确认

```
┌─────────────────────────────────────────┐
│  ✓ 恢复文件                             │
├─────────────────────────────────────────┤
│                                         │
│  确定要恢复选中的 3 个项目吗？           │
│                                         │
├─────────────────────────────────────────┤
│                    [取消]    [确认恢复]  │
└─────────────────────────────────────────┘
```

#### 彻底删除确认

```
┌─────────────────────────────────────────┐
│  ⚠️ 彻底删除                             │
├─────────────────────────────────────────┤
│                                         │
│  确定要彻底删除选中的 2 个项目吗？       │
│  此操作无法撤销！                        │
│                                         │
├─────────────────────────────────────────┤
│                    [取消]    [确认删除]  │
└─────────────────────────────────────────┘
```

#### 清空回收站确认

```
┌─────────────────────────────────────────┐
│  ⚠️ 清空回收站                           │
├─────────────────────────────────────────┤
│                                         │
│  确定要清空回收站吗？                    │
│                                         │
│  共 15 个项目，总计 156 MB               │
│  此操作将永久删除所有项目，无法撤销！    │
│                                         │
├─────────────────────────────────────────┤
│                    [取消]    [确认清空]  │
└─────────────────────────────────────────┘
```

### 操作结果反馈

| 操作 | 成功提示 | 失败提示 |
|------|----------|----------|
| 恢复 | "已恢复 N 个项目到原位置" | "部分项目恢复失败" |
| 彻底删除 | "已永久删除 N 个项目，释放 X MB" | "删除失败" |
| 清空回收站 | "回收站已清空，释放 X MB" | "清空失败" |

---

*最后更新: 2026-02-19*
