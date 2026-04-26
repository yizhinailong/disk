# Integration Test Migration Map

**Status**: Frozen Contract
**Created**: 2026-04-17
**Contract**: All bash scripts migrate 1:1 to Python with UV execution

## Execution Contract

- **Runtime**: `uv run test/integration/test_xxx.py`
- **Python Version**: >=3.10 (enforced via PEP 723 inline metadata)
- **Test Names**: Preserve existing CTest names (no changes)
- **Working Directory**: `${CMAKE_SOURCE_DIR}` (repo root)
- **Execution Mode**: Serial (RUN_SERIAL TRUE)
- **Timeout**: 120 seconds per test

## 1:1 Mapping Table

| CTest Name | Bash Script | Python Script | Status |
|------------|-------------|---------------|--------|
| LoginRateLimitIntegration | test_login_rate_limit.sh | test_login_rate_limit.py | 🔧 Proof of concept |
| UserProfileStorageIntegration | test_user_profile_storage.sh | test_user_profile_storage.py | ⏳ Pending |
| AssemblyBackpressureIntegration | test_assembly_backpressure.sh | test_assembly_backpressure.py | ⏳ Pending |
| AuthFlowIntegration | test_auth_flow.sh | test_auth_flow.py | ⏳ Pending |
| AuthLifecycleIntegration | test_auth_lifecycle.sh | test_auth_lifecycle.py | ⏳ Pending |
| CopyDeleteAtomicityIntegration | test_copy_delete_atomicity.sh | test_copy_delete_atomicity.py | ⏳ Pending |
| DownloadFlowIntegration | test_download_flow.sh | test_download_flow.py | ⏳ Pending |
| FileMetadataQueryIntegration | test_file_metadata_query.sh | test_file_metadata_query.py | ⏳ Pending |
| FileMutationOpsIntegration | test_file_mutation_ops.sh | test_file_mutation_ops.py | ⏳ Pending |
| FolderLifecycleIntegration | test_folder_lifecycle.sh | test_folder_lifecycle.py | ⏳ Pending |
| HealthAndLogsIntegration | test_health_and_logs.sh | test_health_and_logs.py | ⏳ Pending |
| PasswordUpdateIntegration | test_password_update.sh | test_password_update.py | ⏳ Pending |
| RefreshTokenIntegration | test_refresh_token.sh | test_refresh_token.py | ⏳ Pending |
| ShareBrowseIntegration | test_share_browse.sh | test_share_browse.py | ⏳ Pending |
| ShareManagementIntegration | test_share_management.sh | test_share_management.py | ⏳ Pending |
| SystemInfoIntegration | test_system_info.sh | test_system_info.py | ⏳ Pending |
| TrashLifecycleIntegration | test_trash_lifecycle.sh | test_trash_lifecycle.py | ⏳ Pending |
| UploadFlowIntegration | test_upload_flow.sh | test_upload_flow.py | ⏳ Pending |
| UserProfileUpdateIntegration | test_user_profile_update.sh | test_user_profile_update.py | ⏳ Pending |

## CMakeLists.txt Migration Pattern

**Before**:
```cmake
add_test(
    NAME LoginRateLimitIntegration
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_login_rate_limit.sh"
)
```

**After**:
```cmake
add_test(
    NAME LoginRateLimitIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_login_rate_limit.py"
)
```

**Preserve Properties**:
```cmake
set_tests_properties(
    LoginRateLimitIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
```

## Python Entrypoint Template

All Python test scripts must include PEP 723 inline metadata:

```python
#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

import sys

if __name__ == "__main__":
    print("test ok")
    sys.exit(0)
```

## Validation Criteria

- ✅ `uv run test/integration/test_xxx.py` works from repo root
- ✅ `ctest -R XxxIntegration` passes with Python script
- ✅ All dependencies declared in PEP 723 metadata
- ✅ Exit code 0 on success, non-zero on failure
- ✅ Working directory is repo root (`${CMAKE_SOURCE_DIR}`)
- ✅ No venv required (uv handles isolation)
