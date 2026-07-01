# test/integration/lib_py/__init__.py
# Re-exports all public helpers for convenient import:
#   from lib_py import log_info, fetch, do_login, assert_status, ...
#
# This package is the shared harness for all Python integration tests.

# common
from .common import (
    log_info,
    log_pass,
    log_fail,
    log_step,
    log_section,
    save_evidence,
    save_raw_evidence,
    print_summary,
    tests_passed,
    tests_failed,
    EVIDENCE_DIR,
)

# http
from .http import (
    json_field,
    json_value,
    json_int,
    fetch,
    header_value,
    redis_delete_pattern,
    redis_delete_key,
    BASE_URL,
)

# auth
from .auth import (
    server_ready,
    check_server,
    ensure_server,
    cleanup,
    send_login_request,
    do_login,
)

# assert
from .assertion import (
    assert_status,
    assert_json_field,
    assert_json_field_numeric_gt,
    assert_json_array_not_empty,
    assert_header_contains,
)

# fixtures
from .fixtures import (
    create_temp_file,
    md5_hash,
    unique_name,
)

# db
from .db import (
    DatabaseDependencyError,
    DatabaseQueryError,
    database_config,
    db_connection,
    execute,
    execute_many,
    query_all,
    query_one,
    require_row,
    require_scalar,
    scalar,
)

# storage
from .storage import (
    assembled_temp_path,
    configured_chunk_size,
    configured_storage_base_path,
    configured_temp_upload_path,
    disk_config,
    final_blob_path,
    load_disk_config,
    md5_bytes,
    sha256_bytes,
    upload_chunk_path,
    upload_temp_dir,
)

# invariants
from .invariants import (
    assert_db_row_absent,
    assert_db_row_exists,
    assert_db_scalar,
    assert_equal,
    assert_numeric_delta,
    assert_path_absent,
    assert_path_exists,
    assert_true,
)

# reporter
from .reporter import (
    ReportGenerator,
)
