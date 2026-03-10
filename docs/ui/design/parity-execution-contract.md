# Parity Execution Contract

**Created:** 2026-03-10  
**Version:** 1.0  
**Status:** FROZEN (MVP Scope)

---

## 1. Purpose

This document establishes a binding contract between claimed capabilities in `parity-matrix.md` and actual implementation code paths. It defines the **exact scope** for this execution cycle and provides pass/fail verification criteria for downstream tasks.

**CRITICAL:** This contract is FROZEN. No scope additions are permitted without orchestrator approval.

---

## 2. Scope Freeze Declaration

### 2.1 IN-SCOPE Items (MVP)

| Domain | Gap Description | Task ID(s) | Priority |
|--------|-----------------|------------|----------|
| **Upload Entry** | `TransfersViewModel.startUpload()` exists but has NO QML caller | T2, T3 | HIGH |
| **User Flows** | `UserApi` implemented but NOT wired to service/viewmodel/QML | T5, T6, T7 | HIGH |
| **Share Edit** | `ShareApi::Update()` exists but service/viewmodel only expose create/list/cancel | T8, T9 | MEDIUM |
| **Platform Behavior** | Settings toggles (`auto_start`, `minimize_to_tray`, `show_notifications`) only persist, no runtime integration | T4, T10 | MEDIUM |
| **Responsive Mode** | `MainWindowView.qml` is static layout, no Compact/Medium/Expanded modes | T11 | LOW |
| **Documentation** | Update parity-matrix.md to reflect actual state | T12 | LOW |

### 2.2 OUT-SCOPE / EXCLUDE List

The following items are **EXPLICITLY EXCLUDED** from this execution cycle:

| Category | Excluded Item | Reason |
|----------|---------------|--------|
| **Backend** | Schema changes, DB migrations | Backend is stable; QML-only scope |
| **Backend** | New API endpoints | All required APIs exist |
| **Backend** | API contract modifications | Breaking changes require full review |
| **Feature** | Avatar upload feature | Not in parity-matrix claims |
| **Architecture** | Global event bus / plugin framework | Over-engineering for MVP |
| **Architecture** | New singleton patterns | Existing patterns sufficient |
| **QA** | Manual-only acceptance checks | Must have automated verification |
| **Settings** | `confirm_delete` runtime integration | Already functional (dialog-level) |

**EXCLUSION RULE:** Any task that requires changes to items in this list **Must NOT** proceed without orchestrator escalation.

---

## 3. Parity Claims → Code Path Mapping

### 3.1 Verified Implementations (CLAIM ✓ CODE)

| Parity Claim | Matrix Section | Code Path | Lines | Status |
|--------------|----------------|-----------|-------|--------|
| 登录/注册 | Section 1 | `LoginViewModel`, `RegisterViewModel`, `AuthApi`, `AuthService` | main.cpp:68-69, api/AuthApi.hpp | ✓ COMPLETE |
| 文件列表 | Section 3 | `FilesPage.qml`, `FileListViewModel`, `FileService` | FilesPage.qml:1-745 | ✓ COMPLETE |
| 下载功能 | Section 4 | `TransfersViewModel.startDownload()` | TransfersViewModel.hpp:117 | ✓ COMPLETE |
| 分享列表 | Section 5 | `SharePage.qml`, `ShareViewModel`, `ShareService` | ShareViewModel.hpp:1-189 | ✓ COMPLETE |
| 分享创建 | Section 5 | `ShareViewModel.createShare()` | ShareViewModel.hpp:111-116 | ✓ COMPLETE |
| 分享取消 | Section 5 | `ShareViewModel.cancelSelected()` | ShareViewModel.hpp:119 | ✓ COMPLETE |
| 回收站 | Section 6 | `TrashPage.qml`, `TrashViewModel`, `TrashService` | main.cpp:95 | ✓ COMPLETE |
| 设置持久化 | Section 7 | `SettingsViewModel`, `ConfigStore` | SettingsViewModel.hpp:193-195 | ✓ COMPLETE |
| 最近文件 | Section 2 | `FileListViewModel.loadRecentFiles()` | HomePage.qml:324, 545 | ✓ COMPLETE |

### 3.2 Gap Analysis (CLAIM ✗ NO_CODE)

| Parity Claim | Matrix Section | Expected Code Path | Actual State | Gap Type |
|--------------|----------------|-------------------|--------------|----------|
| 上传文件按钮 | Section 2, Line 40 | QML calling `TransfersViewModel.startUpload()` | **NO QML CALLER** - Method exists at TransfersViewModel.hpp:110 but grep found 0 QML references | **MISSING_WIRING** |
| 编辑分享设置 | Section 5, Line 104 | `ShareViewModel.updateShare()` → `ShareService.Update()` → `ShareApi.Update()` | **NO SERVICE LAYER** - ShareApi::Update() exists at ShareApi.hpp:104 but ShareService/ShareViewModel have no Update method | **MISSING_LAYER** |
| 开机自启动 | Section 7, Line 128 | Runtime effect on app startup | **PERSIST_ONLY** - ConfigStore.SetAutoStart() persists but no Qt/OS integration | **NO_RUNTIME** |
| 最小化到托盘 | Section 7, Line 129 | System tray icon + minimize behavior | **PERSIST_ONLY** - ConfigStore.SetMinimizeToTray() persists but no QSystemTrayIcon | **NO_RUNTIME** |
| 显示系统通知 | Section 7, Line 130 | QSystemTrayIcon::showMessage() calls | **PERSIST_ONLY** - ConfigStore.SetShowNotifications() persists but no notification integration | **NO_RUNTIME** |
| Compact 模式 | Section 1, Lines 32-34 | `MainWindowView.qml` responsive layout switching | **STATIC_LAYOUT** - Fixed 200px sidebar, no width-based mode switching | **NOT_IMPLEMENTED** |
| 用户资料管理 | Section 7 (implied) | `UserService`, `UserViewModel` wiring | **UNWIRED_API** - UserApi exists at UserApi.hpp but not instantiated in main.cpp | **MISSING_WIRING** |

---

## 4. Task-to-Capability Mapping

### Wave 1 (Parallel)

| Task ID | Task Name | Primary Gap | Deliverable | Verification |
|---------|-----------|-------------|-------------|--------------|
| **T2** | Upload Entry Point | startUpload() no caller | FileDialog + QML button in FilesPage/HomePage | `grep startUpload *.qml` returns matches |
| **T3** | Upload Flow Completion | Full upload UX | Progress dialog, error handling | Upload test passes |
| **T5** | User Domain Wiring | UserApi not wired | UserService + UserViewModel + main.cpp wiring | UserApi instance in main.cpp |
| **T8** | Share Edit Service | ShareService.Update missing | Add Update() to ShareService | ShareService has Update method |

### Wave 2 (Sequential)

| Task ID | Task Name | Primary Gap | Deliverable | Verification |
|---------|-----------|-------------|-------------|--------------|
| **T4** | Platform Integration | Settings no runtime effect | auto_start/minimize_to_tray/show_notifications runtime | Settings change has visible effect |
| **T6** | User Profile Page | No profile UI | ProfilePage.qml + navigation | Profile page accessible |
| **T7** | User Settings Page | No user settings UI | UserSettingsPage.qml | User settings accessible |
| **T9** | Share Edit Dialog | No edit share UI | EditShareDialog.qml | Edit share dialog works |
| **T10** | Notification Integration | No notification system | QSystemTrayIcon integration | Notifications appear |

### Wave 3 (Polish)

| Task ID | Task Name | Primary Gap | Deliverable | Verification |
|---------|-----------|-------------|-------------|--------------|
| **T11** | Responsive Layout | Static layout | Compact/Medium/Expanded modes in MainWindowView | Width breakpoints work |
| **T12** | Documentation Sync | Matrix out of sync | Updated parity-matrix.md | Matrix matches code |

---

## 5. Pass/Fail Verification Checklist

Each downstream task MUST verify against these criteria:

### T2/T3: Upload Entry
- [ ] `grep -rn "startUpload" ui/diskqml/qml/` returns ≥1 match
- [ ] FileDialog opens from FilesPage toolbar upload button
- [ ] FileDialog opens from HomePage upload button
- [ ] Upload task appears in UploadPage queue

### T5/T6/T7: User Domain
- [ ] `grep -n "UserApi" ui/diskqml/src/main.cpp` returns match
- [ ] `grep -rn "UserService" ui/diskqml/src/` returns service file
- [ ] `grep -rn "UserViewModel" ui/diskqml/src/` returns viewmodel file
- [ ] Profile page shows user info from API

### T8/T9: Share Edit
- [ ] `grep -n "Update" ui/diskqml/src/services/ShareService.hpp` returns method signature
- [ ] `grep -n "updateShare" ui/diskqml/src/viewmodels/ShareViewModel.hpp` returns method
- [ ] Edit share dialog modifies expire/password/permission
- [ ] Changes persist after refresh

### T4/T10: Platform Behavior
- [ ] Enabling `auto_start` adds app to OS startup (platform-specific)
- [ ] Enabling `minimize_to_tray` shows system tray icon
- [ ] Clicking tray icon restores window
- [ ] Enabling `show_notifications` + transfer complete shows notification

### T11: Responsive Layout
- [ ] Window width < 800px: Compact mode (sidebar collapsed)
- [ ] Window width 800-1200px: Medium mode (normal layout)
- [ ] Window width > 1200px: Expanded mode (three-column if applicable)
- [ ] Mode switches on resize without restart

### T12: Documentation
- [ ] All verified implementations marked ✓ in parity-matrix.md
- [ ] All gaps marked with correct status (NOT_IMPLEMENTED / PARTIAL)
- [ ] Last updated date changed to execution date

---

## 6. Code Evidence References

### 6.1 Upload Gap Evidence
```
File: ui/diskqml/src/viewmodels/TransfersViewModel.hpp
Line 110: Q_INVOKABLE void startUpload(const QList<QUrl>& fileUrls, qint64 targetFolderId);

grep result: startUpload in *.qml = 0 matches (GAP CONFIRMED)
```

### 6.2 User Domain Gap Evidence
```
File: ui/diskqml/src/api/UserApi.hpp
Lines 37-105: UserApi class with GetProfile, GetStorage, UpdateProfile, ChangePassword

File: ui/diskqml/src/main.cpp
grep result: UserApi = 0 matches (NOT WIRED - GAP CONFIRMED)
```

### 6.3 Share Edit Gap Evidence
```
File: ui/diskqml/src/api/ShareApi.hpp
Lines 104-111: virtual auto Update(const QString& shareId, int expireDays, ...)

File: ui/diskqml/src/services/ShareService.hpp
Methods: CreateShare, ListShares, CancelShares
grep result: Update = 0 matches in ShareService (GAP CONFIRMED)
```

### 6.4 Platform Behavior Gap Evidence
```
File: ui/diskqml/src/viewmodels/SettingsViewModel.hpp
Lines 193-195: bool m_auto_start{ false }, m_minimize_to_tray{ false }, m_show_notifications{ true }

File: ui/diskqml/src/viewmodels/SettingsViewModel.cpp
Lines 204-206: SetAutoStart/SetMinimizeToTray/SetShowNotifications (persist only)
grep result: QSystemTrayIcon = 0 matches (NO RUNTIME - GAP CONFIRMED)
```

### 6.5 Responsive Layout Gap Evidence
```
File: ui/diskqml/qml/views/MainWindowView.qml
Lines 78-181: Static RowLayout with fixed 200px sidebar
grep result: "Compact|Medium|Expanded|responsive" = 0 matches (GAP CONFIRMED)
```

---

## 7. Dependency Graph

```
T1 (Contract) ─┬─→ T2 (Upload Entry) ─→ T3 (Upload Flow)
               │
               ├─→ T5 (User Wiring) ─┬─→ T6 (Profile Page)
               │                     └─→ T7 (User Settings)
               │
               ├─→ T8 (Share Edit Service) ─→ T9 (Share Edit Dialog)
               │
               ├─→ T4 (Platform) ─→ T10 (Notifications)
               │
               └─→ T11 (Responsive) ─→ T12 (Documentation)
```

---

## 8. Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Scope creep | EXCLUDE list enforced; any additions require orchestrator sign-off |
| False positives in grep | Manual code review before marking complete |
| Platform-specific code | T4/T10 must handle Windows + Linux |
| Breaking existing flows | Each task must run existing tests before merge |

---

## 9. Acceptance Criteria

This contract is accepted when:

1. All IN-SCOPE tasks (T2-T12) have corresponding pass/fail checklist entries
2. All OUT-SCOPE items are documented with "Must NOT" labels
3. grep evidence files exist at `.sisyphus/evidence/task-1-*.txt`
4. Learnings appended to `.sisyphus/notepads/qml-client-completion-plan/learnings.md`

---

## 10. Appendix: Exclusion Verification Commands

```bash
# Verify exclusions are documented
grep -n "EXCLUDE\|Must NOT" docs/ui/design/parity-execution-contract.md

# Verify UserApi not wired (should return 0)
grep -n "UserApi" ui/diskqml/src/main.cpp

# Verify no responsive mode (should return 0)
grep -rn "Compact.*模式\|responsive" ui/diskqml/qml/views/MainWindowView.qml
```

---

*Contract frozen by: Sisyphus-Junior*  
*Review required: Any scope modification*
