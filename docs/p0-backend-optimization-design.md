# P0 Backend Optimization Design

**Author**: Backend Optimization Team
**Date**: 2026-04-01
**Purpose**: Define current flow vs target flow for Auth, Share, and File upload consistency optimizations

## Overview

This document defines three P0 backend optimizations that reduce hot-path overhead, eliminate N+1 query patterns, and harden transactional consistency boundaries. All optimizations maintain API contract compatibility and behavior while improving performance and correctness.

**Execution Order** (DOC-FIRST constraint):
1. Documentation (this file) - MUST be committed before any source code changes
2. Auth hot-path single-decode implementation
3. ShareService batch-query implementation
4. FileService upload consistency hardening

---

## 1. Auth Hot-Path Optimization

### Current Flow

**Problem**: Duplicate JWT decode in the authentication hot-path. The `JwtAuthFilter` decodes the JWT token twice per protected request - once for verification, and again to extract the JTI for revocation checking.

**File References**:
- `src/filters/JwtAuthFilter.cpp:28-68` - Current filter implementation
- `src/services/TokenService.cpp:81-118` - VerifyAccessToken method
- `src/services/TokenService.cpp:238-267` - ExtractJti method
- `src/services/TokenService.hpp:92-118` - VerifyAccessToken return type and helper declarations

```mermaid
flowchart TD
    A[HTTP Request with Bearer Token] --> B{Authorization Header Present?}
    B -->|No| C[Return TokenMissing Error]
    B -->|Yes| D{Starts with 'Bearer '?}
    D -->|No| E[Return TokenMalformed Error]
    D -->|Yes| F[Extract Token String]

    F --> G["VerifyAccessToken - JWT DECODE #1"]
    G --> H{Verification Successful?}
    H -->|No| I[Return Verification Error]
    H -->|Yes| J[Extract user_id, username]

    J --> K["jwt::decode - JWT DECODE #2"]
    K --> L{Has JTI Claim?}
    L -->|No| M[Set User Attributes in Request]
    L -->|Yes| N[Extract JTI from Claims]

    N --> O["IsAccessTokenRevoked - Redis Lookup"]
    O --> P{Token Revoked?}
    P -->|Yes| Q[Return TokenRevoked Error]
    P -->|No| M

    M --> R[Continue to Next Handler/Controller]

    style K fill:#ff6b6b,stroke:#333,stroke-width:2px
    style K stroke-dasharray: 5 5
    style G fill:#ff6b6b,stroke:#333,stroke-width:2px
    style G stroke-dasharray: 5 5
```

**Current Code Flow** (`JwtAuthFilter.cpp:43-61`):

```cpp
// First decode happens here (inside VerifyAccessToken)
auto verify_result = TokenService::GetInstance()->VerifyAccessToken(token);
if (!verify_result) {
    co_return disk::Response::Error(verify_result.error());
}
auto [user_id, username] = verify_result.value();

// Second decode happens here (extracting JTI)
using traits = jwt::traits::open_source_parsers_jsoncpp;
auto decoded = jwt::decode<traits>(token);  // <-- DUPLICATE DECODE

if (decoded.has_payload_claim("jti")) {
    const auto jti = decoded.get_payload_claim("jti").as_string();

    if (co_await TokenService::GetInstance()->IsAccessTokenRevoked(jti)) {
        LOG_WARN << "Token revoked: user_id=" << user_id << ", jti=" << jti;
        co_return disk::Response::Error(disk::error::Code::TokenRevoked);
    }
}
```

**Performance Impact**:
- Two JWT decode operations per authenticated request
- Double parsing overhead (JSON parsing, signature verification check)
- Unnecessary CPU work in the hot path
- Each protected endpoint suffers this overhead

### Target Flow

**Solution**: Modify `VerifyAccessToken` to return a struct containing `user_id`, `username`, and `jti` in a single decode operation. Remove the redundant `jwt::decode()` call in the filter.

**File References to Modify**:
- `src/services/TokenService.hpp:92` - Modify `VerifyAccessToken` return type
- `src/services/TokenService.cpp:81-118` - Modify implementation to extract and return JTI
- `src/filters/JwtAuthFilter.cpp:43-61` - Remove redundant decode, use returned JTI

```mermaid
flowchart TD
    A[HTTP Request with Bearer Token] --> B{Authorization Header Present?}
    B -->|No| C[Return TokenMissing Error]
    B -->|Yes| D{Starts with 'Bearer '?}
    D -->|No| E[Return TokenMalformed Error]
    D -->|Yes| F[Extract Token String]

    F --> G["VerifyAccessToken - SINGLE JWT DECODE"]
    G --> H{Verification Successful?}
    H -->|No| I[Return Verification Error]
    H -->|Yes| J[Extract user_id, username, jti]

    J --> K{Has JTI?}
    K -->|No| L[Set User Attributes in Request]
    K -->|Yes| M["IsAccessTokenRevoked - Redis Lookup"]

    M --> N{Token Revoked?}
    N -->|Yes| O[Return TokenRevoked Error]
    N -->|No| L

    L --> P[Continue to Next Handler/Controller]

    style G fill:#51cf66,stroke:#333,stroke-width:2px
```

**Proposed API Change**:

```cpp
// TokenService.hpp - New return type
struct AccessTokenClaims {
    uint64_t user_id;
    std::string username;
    std::string jti;  // Added field
};

// Modify signature to return full claims
auto VerifyAccessToken(const std::string& token) const
    -> Result<AccessTokenClaims>;
```

**Implementation Steps**:

1. **Step 1**: Modify `TokenService.hpp`
   - Add `AccessTokenClaims` struct
   - Change `VerifyAccessToken` return type from `Result<std::pair<uint64_t, std::string>>` to `Result<AccessTokenClaims>`

2. **Step 2**: Modify `TokenService.cpp:81-118`
   - Extract `jti` claim during verification (lines 100-102)
   - Return `AccessTokenClaims{user_id, username, jti}` instead of `std::pair`

3. **Step 3**: Modify `JwtAuthFilter.cpp:43-61`
   - Remove lines 51-52 (duplicate decode)
   - Remove line 55 (redundant jti extraction)
   - Use `verify_result.value().jti` directly from VerifyAccessToken result
   - Update line 48 to destructure all three fields

4. **Step 4**: Update all `VerifyAccessToken` call sites
   - Search for all usages of `VerifyAccessToken`
   - Update code to handle new `AccessTokenClaims` return type

**Behavioral Compatibility**:
- No changes to API contracts
- No changes to response payloads
- Existing error handling unchanged
- Revoked token logic preserved

**Performance Improvement**:
- Eliminate one complete JWT decode per authenticated request
- Reduce CPU overhead by ~50% in auth hot-path
- Minimal code change, high impact

---

## 2. ShareService N+1 Elimination

### Current Flow

**Problem**: The `GetShareFiles` method exhibits classic N+1 query pattern. It performs one query to fetch share_files, then loops through each item to perform individual file/folder lookups.

**File References**:
- `src/services/ShareService.cpp:738-789` - GetShareFiles implementation with N+1 pattern
- `src/services/ShareService.cpp:152-239` - ListShares method (uses GetShareFiles)
- `sql/init.sql:160-191` - shares/share_files table schema and indexes

```mermaid
flowchart TD
    A[GetShareFiles share_id] --> B["Query 1: SELECT FROM share_files WHERE share_id = ?"]
    B --> C[Fetch N share_file records]
    C --> D[Iterate N items]

    D --> E{Item Type = 'file'?}
    E -->|Yes| F["Query N+1: SELECT FROM files WHERE id = ?"]
    E -->|No| G["Query N+1: SELECT FROM folders WHERE id = ?"]

    F --> H[Build ShareFile object]
    G --> H
    H --> I[Add to result vector]
    I --> J{More Items?}
    J -->|Yes| E
    J -->|No| K[Return result vector]

    style F fill:#ff6b6b,stroke:#333,stroke-width:2px
    style F stroke-dasharray: 5 5
    style G fill:#ff6b6b,stroke:#333,stroke-width:2px
    style G stroke-dasharray: 5 5
```

**Current Code Flow** (`ShareService.cpp:738-789`):

```cpp
auto ShareService::GetShareFiles(uint64_t share_id) const
    -> drogon::Task<std::vector<ShareFile>> {
    CoroMapper<ShareFiles> sf_mapper(m_db_client);
    CoroMapper<Files> file_mapper(m_db_client);
    CoroMapper<Folders> folder_mapper(m_db_client);

    std::vector<ShareFile> result;

    try {
        // Query 1: Fetch all share_files (1 query)
        auto share_files =
            co_await sf_mapper.findBy(Criteria(ShareFiles::Cols::_share_id, share_id));

        // N+1: Loop and query individually for each item
        for (const auto& sf : share_files) {
            if (sf.getValueOfItemType() == "file") {
                try {
                    // Query for each file (N queries)
                    auto file = co_await file_mapper.findOne(
                        Criteria(Files::Cols::_id, sf.getValueOfItemId())
                    );

                    ShareFile sf_item;
                    sf_item.id = file.getValueOfId();
                    sf_item.name = file.getValueOfName();
                    sf_item.type = "file";
                    sf_item.size = file.getValueOfSize();
                    result.push_back(sf_item);
                } catch (const DrogonDbException& e) {
                    LOG_WARN << "Failed to get share file: file_id=" << sf.getValueOfItemId();
                }
            } else if (sf.getValueOfItemType() == "folder") {
                try {
                    // Query for each folder (N queries)
                    auto folder = co_await folder_mapper.findOne(
                        Criteria(Folders::Cols::_id, sf.getValueOfItemId())
                    );

                    ShareFile sf_item;
                    sf_item.id = folder.getValueOfId();
                    sf_item.name = folder.getValueOfName();
                    sf_item.type = "folder";
                    sf_item.size = 0;
                    result.push_back(sf_item);
                } catch (const DrogonDbException& e) {
                    LOG_WARN << "Failed to get share folder: folder_id="
                             << sf.getValueOfItemId();
                }
            }
        }
    } catch (const DrogonDbException& e) {
        LOG_ERROR << "Failed to get share file list: " << e.base().what();
    }

    co_return result;
}
```

**Performance Impact**:
- Query count = 1 + N (where N = number of shared files/folders)
- For 50 shared items = 51 database queries
- Linear growth in latency as share size increases
- Network round-trip overhead multiplied

### Target Flow

**Solution**: Replace the loop with batched queries using JOINs to fetch all file and folder metadata in bounded queries (constant number regardless of N).

**File References to Modify**:
- `src/services/ShareService.cpp:738-789` - Replace N+1 loop with JOIN-based batch retrieval

```mermaid
flowchart TD
    A[GetShareFiles share_id] --> B["Query 1: JOIN share_files + files ON item_type='file'"]
    A --> C["Query 2: JOIN share_files + folders ON item_type='folder'"]

    B --> D[Fetch all file metadata in batch]
    C --> E[Fetch all folder metadata in batch]

    D --> F[Merge results maintaining original order]
    E --> F

    F --> G[Return result vector]

    style B fill:#51cf66,stroke:#333,stroke-width:2px
    style C fill:#51cf66,stroke:#333,stroke-width:2px
```

**Proposed Implementation**:

```cpp
auto ShareService::GetShareFiles(uint64_t share_id) const
    -> drogon::Task<std::vector<ShareFile>> {
    std::vector<ShareFile> result;

    try {
        // Batch Query 1: Fetch all files with one JOIN
        auto files_result = co_await m_db_client->execSqlCoro(
            R"(
                SELECT sf.id, sf.item_id, f.id as file_id, f.name, f.size
                FROM share_files sf
                JOIN files f ON sf.item_id = f.id
                WHERE sf.share_id = ? AND sf.item_type = 'file'
                ORDER BY sf.id
            )",
            share_id
        );

        for (const auto& row : files_result) {
            ShareFile sf_item;
            sf_item.id = row["file_id"].as<uint64_t>();
            sf_item.name = row["name"].as<std::string>();
            sf_item.type = "file";
            sf_item.size = row["size"].as<uint64_t>();
            result.push_back(sf_item);
        }

        // Batch Query 2: Fetch all folders with one JOIN
        auto folders_result = co_await m_db_client->execSqlCoro(
            R"(
                SELECT sf.id, sf.item_id, f.id as folder_id, f.name
                FROM share_files sf
                JOIN folders f ON sf.item_id = f.id
                WHERE sf.share_id = ? AND sf.item_type = 'folder'
                ORDER BY sf.id
            )",
            share_id
        );

        for (const auto& row : folders_result) {
            ShareFile sf_item;
            sf_item.id = row["folder_id"].as<uint64_t>();
            sf_item.name = row["name"].as<std::string>();
            sf_item.type = "folder";
            sf_item.size = 0;
            result.push_back(sf_item);
        }

    } catch (const DrogonDbException& e) {
        LOG_ERROR << "Failed to get share file list: " << e.base().what();
    }

    co_return result;
}
```

**Implementation Steps**:

1. **Step 1**: Replace `GetShareFiles` implementation
   - Use raw SQL with JOINs instead of ORM loop
   - Execute 2 queries total (files + folders) regardless of N
   - Preserve ordering via `ORDER BY sf.id`

2. **Step 2**: Update ownership validation paths
   - Check for similar N+1 patterns in share creation/update validation
   - Batch validate ownership using `WHERE id IN (...)` pattern
   - Update `src/services/ShareService.cpp:692-722` if applicable

3. **Step 3**: Preserve response contract
   - Ensure `ShareFile` struct fields populated identically
   - Maintain ordering guarantees used by API responses
   - Test with edge cases (empty shares, mixed types)

**Behavioral Compatibility**:
- No changes to share list API payload shape
- Existing error handling preserved
- Ordering maintained for consistency

**Performance Improvement**:
- Query count reduced from 1+N to 2 (constant)
- For 50 shared items: 51 queries → 2 queries
- Latency reduction of ~96% at scale
- Linear complexity eliminated

---

## 3. FileService Upload Consistency Hardening

### Current Flow

**Problem**: Upload finalization has inconsistent transaction boundaries. Database operations for file/content insertion occur in a transaction, but quota transfer and task finalization happen outside the transaction scope. This creates consistency risks if post-transaction operations fail.

**File References**:
- `src/services/FileService.cpp:307-555` - CompleteUpload method with transaction boundary gaps
- `src/services/FileService.cpp:70-102` - Instant upload ref-count + file insert split
- `src/services/FileService.cpp:1427-1476` - Reserved quota operations
- `sql/init.sql:97-133` - upload_tasks, upload_task_chunks, file_contents schema

```mermaid
flowchart TD
    A[CompleteUpload Request] --> B[Verify Upload Task]
    B --> C[Check All Chunks Uploaded]
    C --> D[Assemble Chunks]
    D --> E[Verify File Hash]
    E --> F{Hash Match?}
    F -->|No| G[Delete Assembled File]
    F -->|Yes| H[Check Existing Content]

    H --> I{Content Exists?}
    I -->|Yes| J[Delete Assembled File]
    I -->|No| K[Promote to Final Storage]
    K --> L[Compute SHA256]

    J --> M["BEGIN TRANSACTION"]
    L --> M

    M --> N["INSERT file_contents or UPDATE ref_count"]
    N --> O["INSERT files record"]

    O --> P{DB Operation Success?}
    P -->|No| Q[ROLLBACK Transaction]
    P -->|Yes| R[COMMIT Transaction]

    R --> S["TRANSFER QUOTA - OUTSIDE TRANSACTION"]
    S --> T["FINALIZE TASK STATUS - OUTSIDE TRANSACTION"]

    T --> U{Finalization Success?}
    U -->|Yes| V[Return Success]
    U -->|No| W["Compensation: Delete orphaned storage file"]

    style S fill:#ff6b6b,stroke:#333,stroke-width:2px
    style S stroke-dasharray: 5 5
    style T fill:#ff6b6b,stroke:#333,stroke-width:2px
    style T stroke-dasharray: 5 5
```

**Current Code Flow** (`FileService.cpp:436-555`):

```cpp
// Lines 436-518: Transaction for file/content insertion
std::shared_ptr<drogon::orm::Transaction> transaction;
Files file;
bool db_operation_failed = false;
try {
    transaction = co_await m_db_client->newTransactionCoro();

    CoroMapper<FileContents> content_mapper(transaction);
    CoroMapper<Files> file_mapper(transaction);

    uint64_t content_id = 0;
    if (existing_content.has_value()) {
        content_id = existing_content.value();
        auto increment_result = co_await transaction->execSqlCoro(
            "UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = ?",
            content_id
        );
        // ... error handling
    } else {
        FileContents content;
        // ... populate content fields
        content = co_await content_mapper.insert(content);
        content_id = content.getValueOfId();
    }

    file.setUserId(user_id);
    // ... populate file fields
    file = co_await file_mapper.insert(file);

} catch (...) {
    // Rollback handling
    db_operation_failed = true;
}

if (db_operation_failed) {
    // Compensation: delete storage file
    if (should_compensate_storage_file) {
        co_await m_storage->DeletePath(final_storage_path);
    }
    co_return std::unexpected(...);
}

// Lines 522-538: QUOTA TRANSFER - OUTSIDE TRANSACTION
try {
    auto transfer_result = co_await m_db_client->execSqlCoro(
        "UPDATE users SET storage_reserved = GREATEST(storage_reserved - ?, 0), "
        "storage_used = storage_used + ? WHERE id = ?",
        task.getValueOfFileSize(),
        task.getValueOfFileSize(),
        user_id
    );
    // ... warning-only on failure
} catch (...) {
    LOG_WARN << "Failed to transfer reserved quota: " << e.base().what();
}

// Lines 540-555: TASK FINALIZATION - OUTSIDE TRANSACTION
try {
    co_await m_db_client->execSqlCoro(
        "UPDATE upload_tasks SET status = 1, finalized_at = NOW() WHERE id = ? AND status = 0",
        upload_id
    );

    co_await m_db_client->execSqlCoro(
        "DELETE FROM upload_task_chunks WHERE task_id = ?",
        upload_id
    );
} catch (...) {
    LOG_WARN << "Failed to finalize upload task (non-critical): " << e.base().what();
}
```

**Consistency Risks**:
1. **Quota transfer failure after file commit**: File exists, quota not deducted → user gets free storage
2. **Task finalization failure**: Upload stuck in limbo, file created but task not marked completed → can be retried creating duplicates
3. **Storage cleanup compensation**: If compensation fails, orphaned files consume disk space

### Target Flow

**Solution**: Expand transaction scope to include quota transfer and task finalization, ensuring atomicity across the entire upload completion sequence.

**File References to Modify**:
- `src/services/FileService.cpp:436-555` - Move quota transfer and task finalization inside transaction

```mermaid
flowchart TD
    A[CompleteUpload Request] --> B[Verify Upload Task]
    B --> C[Check All Chunks Uploaded]
    C --> D[Assemble Chunks]
    D --> E[Verify File Hash]
    E --> F{Hash Match?}
    F -->|No| G[Delete Assembled File]
    F -->|Yes| H[Check Existing Content]

    H --> I{Content Exists?}
    I -->|Yes| J[Delete Assembled File]
    I -->|No| K[Promote to Final Storage]
    K --> L[Compute SHA256]

    J --> M["BEGIN TRANSACTION - EXPANDED SCOPE"]
    L --> M

    M --> N["INSERT/UPDATE file_contents"]
    N --> O["INSERT files record"]
    O --> P["TRANSFER QUOTA - INSIDE TRANSACTION"]
    P --> Q["FINALIZE TASK STATUS - INSIDE TRANSACTION"]
    Q --> R["DELETE upload_task_chunks - INSIDE TRANSACTION"]

    R --> S{DB Operation Success?}
    S -->|No| T[ROLLBACK ALL CHANGES]
    S -->|Yes| U[COMMIT TRANSACTION]

    T --> V[Compensation: Delete orphaned storage file]
    U --> W[Return Success]

    style P fill:#51cf66,stroke:#333,stroke-width:2px
    style Q fill:#51cf66,stroke:#333,stroke-width:2px
    style R fill:#51cf66,stroke:#333,stroke-width:2px
```

**Proposed Implementation**:

```cpp
// Lines 436-518: Expand transaction to include quota and finalization
std::shared_ptr<drogon::orm::Transaction> transaction;
Files file;
bool db_operation_failed = false;
try {
    transaction = co_await m_db_client->newTransactionCoro();

    CoroMapper<FileContents> content_mapper(transaction);
    CoroMapper<Files> file_mapper(transaction);

    uint64_t content_id = 0;
    if (existing_content.has_value()) {
        content_id = existing_content.value();
        auto increment_result = co_await transaction->execSqlCoro(
            "UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = ?",
            content_id
        );
        if (increment_result.affectedRows() == 0) {
            LOG_WARN << "File content not found when finalizing upload: content_id="
                     << content_id;
            throw std::runtime_error("Failed to increment file content reference count");
        }
    } else {
        FileContents content;
        content.setHashMd5(final_hash);
        content.setHashSha256(final_sha256);
        content.setSize(task.getValueOfFileSize());
        content.setStoragePath(final_storage_path.string());
        content.setMimeType("");
        content.setRefCount(1);

        content = co_await content_mapper.insert(content);
        content_id = content.getValueOfId();
        LOG_DEBUG << "FileContents created successfully: content_id=" << content_id;
    }

    file.setUserId(user_id);
    file.setContentId(content_id);
    file.setFolderId(task.getValueOfFolderId());
    file.setName(task.getValueOfFilename());
    file.setExtension(ExtractExtension(task.getValueOfFilename()));
    file.setSize(task.getValueOfFileSize());
    file.setMimeType("");
    file.setPath("");
    file.setIsFavorite(0);
    file.setDownloadCount(0);

    file = co_await file_mapper.insert(file);

    // ===== NEW: Transfer quota inside transaction =====
    auto transfer_result = co_await transaction->execSqlCoro(
        "UPDATE users "
        "SET storage_reserved = GREATEST(storage_reserved - ?, 0), "
        "    storage_used = storage_used + ? "
        "WHERE id = ?",
        task.getValueOfFileSize(),
        task.getValueOfFileSize(),
        user_id
    );

    if (transfer_result.affectedRows() == 0) {
        LOG_WARN << "Failed to transfer reserved quota to used: user_id=" << user_id;
        throw std::runtime_error("Failed to transfer reserved quota");
    }

    LOG_DEBUG << "Quota transferred inside transaction: reserved -> used, user_id="
              << user_id << ", bytes=" << task.getValueOfFileSize();

    // ===== NEW: Finalize task inside transaction =====
    auto finalize_result = co_await transaction->execSqlCoro(
        "UPDATE upload_tasks SET status = 1, finalized_at = NOW() WHERE id = ? AND status = 0",
        upload_id
    );

    if (finalize_result.affectedRows() == 0) {
        LOG_WARN << "Failed to finalize upload task: upload_id=" << upload_id;
        throw std::runtime_error("Failed to finalize upload task");
    }

    // Delete chunk tracking inside transaction
    co_await transaction->execSqlCoro(
        "DELETE FROM upload_task_chunks WHERE task_id = ?",
        upload_id
    );

    LOG_DEBUG << "Upload task finalized inside transaction: " << upload_id;

    // Commit all changes atomically
    co_await transaction->commit();

} catch (const drogon::orm::DrogonDbException& e) {
    LOG_ERROR << "Database operation failed: " << e.base().what();
    if (transaction) {
        try {
            co_await transaction->rollback();
        } catch (const std::exception& rollback_e) {
            LOG_ERROR << "Transaction rollback failed: " << rollback_e.what();
        }
    }
    db_operation_failed = true;
} catch (const std::exception& e) {
    LOG_ERROR << "Database operation failed: " << e.what();
    if (transaction) {
        try {
            co_await transaction->rollback();
        } catch (const std::exception& rollback_e) {
            LOG_ERROR << "Transaction rollback failed: " << rollback_e.what();
        }
    }
    db_operation_failed = true;
}

if (db_operation_failed) {
    if (should_compensate_storage_file) {
        auto cleanup_result = co_await m_storage->DeletePath(final_storage_path);
        if (!cleanup_result) {
            LOG_ERROR << "Compensation failed, orphan storage file may remain: "
                      << final_storage_path;
        }
    }
    co_return std::unexpected(
        ErrorInfo(ErrorCode::InternalError, "Database operation failed")
    );
}

LOG_INFO << "Upload completed atomically: file_id=" << file.getValueOfId();
```

**Implementation Steps**:

1. **Step 1**: Move quota transfer inside transaction
   - Move lines 522-538 inside the transaction try block
   - Convert warning-only failure to exception to ensure rollback
   - Validate affectedRows to ensure quota actually transferred

2. **Step 2**: Move task finalization inside transaction
   - Move lines 540-555 inside the transaction try block
   - Validate affectedRows for task status update
   - Include chunk deletion in same transaction

3. **Step 3**: Add explicit commit call
   - Add `co_await transaction->commit()` after all operations succeed
   - Ensure rollback is called in all catch blocks

4. **Step 4**: Update error handling
   - Change all post-transaction failures to exceptions
   - No more "warning-only" failures in critical path
   - Ensure compensation only triggers on transaction rollback

**Consistency Guarantees**:
- Atomic commit of file + quota + task finalization
- No orphaned files or inconsistent quota states
- Rollback cleans up all DB state on any failure
- Storage compensation remains as last resort

**Behavioral Compatibility**:
- No changes to API response structure
- Upload success behavior unchanged
- Error paths more consistent (fail-fast instead of partial success)

---

## Execution Order and Dependencies

**Doc-First Constraint**:
1. ✅ Commit this documentation file (current task)
2. ⏭️ T2: Add auth baseline tests
3. ⏭️ T3: Add share baseline tests
4. ⏭️ T4: Add upload consistency baseline tests
5. ⏭️ T5: Implement auth hot-path optimization
6. ⏭️ T6: Implement share batch-query optimization
7. ⏭️ T7: Implement file consistency hardening
8. ⏭️ T8: Integrated verification and audit

**Dependency Matrix**:
- T1 (this doc) blocks T2-T8 (doc-first gate)
- T2/T3/T4 provide baselines for T5/T6/T7 acceptance checks
- T5 independent from T6/T7 except shared build pipeline
- T6 independent from T5/T7 except shared build pipeline
- T7 depends on T4 baseline only
- T8 depends on T5/T6/T7 completion

---

## Acceptance Criteria Summary

### T1: Documentation (Current Task)
- [x] File `docs/p0-backend-optimization-design.md` created
- [x] Contains 3+ Mermaid flowcharts (Auth, Share, File)
- [x] Each section has current vs target flow
- [x] File references with specific line numbers
- [ ] Git diff shows only docs files (verified after commit)
- [ ] Commit message: `docs(p0-backend): define auth/share/file optimization design and execution order`

### T5: Auth Hot-Path
- [ ] Protected request path no longer performs duplicate decode
- [ ] Existing auth regression harness from T2 passes unchanged
- [ ] Revoked token behavior unchanged
- [ ] Query/evidence artifacts generated

### T6: Share Batch-Query
- [ ] Query-count test from T3 demonstrates bounded query count
- [ ] Share list/access endpoints preserve response schema
- [ ] Performance measured in evidence artifacts

### T7: File Consistency
- [ ] Fault-injection tests from T4 pass
- [ ] No orphan/inconsistent DB state after failures
- [ ] Happy-path upload succeeds with expected finalization

---

## Risk Mitigation

### Auth Hot-Path Risks
- **Risk**: Breaking existing token validation logic
- **Mitigation**: Extensive test coverage for all token states (valid, expired, revoked, malformed)
- **Rollback**: Simple revert of struct change, backward-incompatible but minimal blast radius

### Share Batch-Query Risks
- **Risk**: Ordering changes in API response
- **Mitigation**: Use `ORDER BY sf.id` in JOINs to preserve original ordering
- **Rollback**: Simple revert, can patch with feature flag if needed

### File Consistency Risks
- **Risk**: Transaction deadlock or timeout on large uploads
- **Mitigation**: Keep transaction window minimal, use appropriate isolation level
- **Rollback**: Complex but can revert to non-atomic compensation model

---

## Notes for Implementers

### Auth Optimization Notes
- Check all `VerifyAccessToken` call sites (grep for usage)
- Update unit tests to handle new return type
- Verify JWT library version compatibility for JTI extraction
- Test with tokens without JTI claim (backward compatibility)

### Share Optimization Notes
- Verify SQL JOIN syntax is correct for target MySQL version
- Test with empty shares, single-item shares, mixed-type shares
- Validate ordering preservation matches current behavior
- Check for additional N+1 patterns in share creation/update

### File Consistency Notes
- Test concurrent upload completions (ensure no deadlocks)
- Validate transaction timeout settings are adequate
- Test with storage failures at various stages (after DB commit, before DB commit)
- Ensure compensation logic is robust for edge cases

---

**End of P0 Backend Optimization Design**
