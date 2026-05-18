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

# reporter
from .reporter import (
    ReportGenerator,
)
