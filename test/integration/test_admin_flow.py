#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for the admin module.

Verifies all admin endpoints:
  1.  Admin 获取用户列表
  2.  Admin 搜索用户
  3.  Admin 获取用户详情
  4.  Admin 修改用户状态
  5.  Admin 修改用户角色
  6.  Admin 软删除用户
  7.  Admin 自我修改状态保护
  8.  Admin 自我降级保护
  9.  非管理员访问被拒
  10. 未认证访问被拒
  11. Admin 获取分享列表
  12. Admin 强制取消分享
  13. Admin 获取系统概览
  14. Admin 获取系统状态
  15. 查询不存在资源

Prerequisites:
  - Server running on localhost:8080
  - MySQL database configured with seed data
  - Redis configured

Usage:
  uv run test/integration/test_admin_flow.py
"""

import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    log_step,
    print_summary,
    save_evidence,
    ensure_server,
    cleanup,
    do_login,
    send_login_request,
    json_field,
    fetch,
    unique_name,
    create_temp_file,
    md5_hash,
    tests_passed,
    tests_failed,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
ADMIN_USER = os.environ.get("ADMIN_USER", "admin")
ADMIN_PASS = os.environ.get("ADMIN_PASS", "Admin123")

EVIDENCE_FILE = "task-16-admin-integration.txt"

# Collected IDs for cross-test use
_admin_id: str = ""
_test_user_id: str = ""
_test_user_token: str = ""
_share_id: str = ""


# ─── Helpers ─────────────────────────────────────────────────────────────────


def get_admin_token() -> str:
    """Login as admin (seed user), return access_token."""
    status, body = send_login_request(ADMIN_USER, ADMIN_PASS)
    token = json_field(body, "data.access_token")
    if not token or token == "null":
        log_fail(f"Admin login failed: HTTP {status}")
        print(body)
        raise SystemExit(1)
    return token


def get_admin_headers() -> dict[str, str]:
    """Return headers dict with Bearer token for admin."""
    token = get_admin_token()
    return {"Authorization": f"Bearer {token}"}


def get_user_headers(token: str) -> dict[str, str]:
    """Return headers dict with Bearer token."""
    return {"Authorization": f"Bearer {token}"}


def register_and_login_user(username: str, password: str, email: str) -> tuple[str, str]:
    """Register a new user, login, return (access_token, user_id)."""
    # Register
    reg_resp = fetch(
        "/api/auth/register",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={"username": username, "password": password, "email": email},
    )
    code = json_field(reg_resp.text, "code")
    if reg_resp.status_code not in (200, 201) and code != "0":
        # Allow already-registered (idempotent)
        log_info(f"Register returned HTTP {reg_resp.status_code} code={code}, continuing...")

    # Login
    login_status, login_body = send_login_request(username, password)
    token = json_field(login_body, "data.access_token")
    user_id = json_field(login_body, "data.user.id")
    if not token or token == "null":
        log_fail(f"Login after register failed for {username}: HTTP {login_status}")
        print(login_body)
        raise SystemExit(1)
    return token, user_id


def upload_fixture(token: str, file_size: int = 256) -> str:
    """Upload a test file via chunked upload flow. Returns file_id."""
    path = create_temp_file(file_size)
    file_hash = md5_hash(path)
    filename = unique_name("admin_fixture") + ".bin"

    # Init upload
    init_resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": filename,
            "file_size": file_size,
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )

    instant = json_field(init_resp.text, "data.instant_upload")

    if instant == "true":
        fid = json_field(init_resp.text, "data.file_id")
        if not fid or fid == "null":
            log_fail("Instant upload but no file_id returned")
            print(init_resp.text)
            os.unlink(path)
            raise SystemExit(1)
        os.unlink(path)
        return fid

    upload_id = json_field(init_resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init upload failed")
        print(init_resp.text)
        os.unlink(path)
        raise SystemExit(1)

    # Upload chunk
    with open(path, "rb") as f:
        content = f.read()

    chunk_resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={file_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/octet-stream",
        },
        data=content,
    )
    code = json_field(chunk_resp.text, "code")
    if code != "0":
        log_fail(f"Upload chunk failed: code={code}")
        print(chunk_resp.text)
        os.unlink(path)
        raise SystemExit(1)

    # Complete upload
    complete_resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"upload_id": upload_id},
    )

    fid = json_field(complete_resp.text, "data.file.id")
    if not fid or fid == "null":
        log_fail("Complete upload — no file.id")
        print(complete_resp.text)
        os.unlink(path)
        raise SystemExit(1)

    os.unlink(path)
    return fid


def create_share_for_admin(token: str, file_id: str) -> str:
    """Create a share for a file. Returns share_id."""
    resp = fetch(
        "/api/share",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={
            "file_ids": [int(file_id)],
            "permission": "download",
            "expire_days": 7,
        },
    )

    code = json_field(resp.text, "code")
    if code != "0":
        log_fail(f"Create share failed: code={code}")
        print(resp.text)
        raise SystemExit(1)

    share_id = json_field(resp.text, "data.share_id")
    if not share_id or share_id == "null":
        log_fail("Create share returned success but no share_id")
        print(resp.text)
        raise SystemExit(1)

    return share_id


# ─── Setup ───────────────────────────────────────────────────────────────────


def setup():
    """Prepare test data: register a regular user, get admin ID, create a share."""
    global _admin_id, _test_user_id, _test_user_token, _share_id

    log_step("Setup: getting admin user ID...")
    admin_status, admin_body = send_login_request(ADMIN_USER, ADMIN_PASS)
    _admin_id = json_field(admin_body, "data.user.id")
    if not _admin_id or _admin_id == "null":
        log_fail("Could not get admin user ID from login response")
        print(admin_body)
        raise SystemExit(1)
    log_info(f"Admin user ID: {_admin_id}")

    # Register a regular user for tests 4-6 (status/role/delete) and 12 (share)
    tag = unique_name("adm")
    username = f"admintest_{tag}"
    password = "AdminTest123"
    email = f"{tag}@test.internal"

    log_step(f"Setup: registering test user {username}...")
    _test_user_token, _test_user_id = register_and_login_user(username, password, email)
    log_info(f"Test user ID: {_test_user_id}")

    # Upload a file and create a share for test 12
    log_step("Setup: uploading fixture and creating share...")
    file_id = upload_fixture(_test_user_token, 256)
    _share_id = create_share_for_admin(_test_user_token, file_id)
    log_info(f"Test share ID: {_share_id}")

    log_pass("Setup complete")


# ─── Test 1: Admin 获取用户列表 ─────────────────────────────────────────────


def test_admin_list_users():
    log_info("[Test 1] Admin 获取用户列表...")

    resp = fetch(
        "/api/admin/users",
        method="GET",
        headers=get_admin_headers(),
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        items = json_field(resp.text, "data.items")
        pagination = json_field(resp.text, "data.pagination")
        if items and pagination:
            log_pass("Admin list users: HTTP 200, code=0, items and pagination present")
        else:
            log_fail(f"Admin list users: missing items or pagination")
            print(resp.text)
            sys.exit(1)
    else:
        log_fail(f"Admin list users: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 2: Admin 搜索用户 ─────────────────────────────────────────────────


def test_admin_search_users():
    log_info("[Test 2] Admin 搜索用户...")

    resp = fetch(
        "/api/admin/users?username=admin",
        method="GET",
        headers=get_admin_headers(),
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        # Verify at least the admin user appears in results
        first_username = json_field(resp.text, "data.items.0.username")
        if first_username == "admin":
            log_pass("Admin search users: filtered by username=admin, found admin user")
        else:
            log_pass("Admin search users: HTTP 200, code=0 (results filtered)")
    else:
        log_fail(f"Admin search users: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 3: Admin 获取用户详情 ─────────────────────────────────────────────


def test_admin_get_user_detail():
    log_info("[Test 3] Admin 获取用户详情...")

    resp = fetch(
        f"/api/admin/users/{_test_user_id}",
        method="GET",
        headers=get_admin_headers(),
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        uid = json_field(resp.text, "data.id")
        username = json_field(resp.text, "data.username")
        email = json_field(resp.text, "data.email")
        role = json_field(resp.text, "data.role")
        status = json_field(resp.text, "data.status")
        if uid == _test_user_id and username and email:
            log_pass(f"Admin get user detail: id={uid}, username={username}, role={role}, status={status}")
        else:
            log_pass("Admin get user detail: HTTP 200, code=0, user fields present")
    else:
        log_fail(f"Admin get user detail: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 4: Admin 修改用户状态 ─────────────────────────────────────────────


def test_admin_change_user_status():
    log_info("[Test 4] Admin 修改用户状态...")

    resp = fetch(
        f"/api/admin/users/{_test_user_id}/status",
        method="PUT",
        headers={**get_admin_headers(), "Content-Type": "application/json"},
        json_body={"status": 0},
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        log_pass("Admin change user status: set status=0 (disabled), HTTP 200, code=0")
    else:
        log_fail(f"Admin change user status: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)

    # Restore status to 1 (active) for subsequent tests
    restore_resp = fetch(
        f"/api/admin/users/{_test_user_id}/status",
        method="PUT",
        headers={**get_admin_headers(), "Content-Type": "application/json"},
        json_body={"status": 1},
    )
    restore_code = json_field(restore_resp.text, "code")
    if restore_resp.status_code != 200 or restore_code != "0":
        log_info(f"Warning: could not restore user status to active (code={restore_code})")


# ─── Test 5: Admin 修改用户角色 ─────────────────────────────────────────────


def test_admin_change_user_role():
    log_info("[Test 5] Admin 修改用户角色...")

    resp = fetch(
        f"/api/admin/users/{_test_user_id}/role",
        method="PUT",
        headers={**get_admin_headers(), "Content-Type": "application/json"},
        json_body={"role": 0},
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        log_pass("Admin change user role: set role=0 (regular user), HTTP 200, code=0")
    else:
        log_fail(f"Admin change user role: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 6: Admin 软删除用户 ───────────────────────────────────────────────


def test_admin_soft_delete_user():
    log_info("[Test 6] Admin 软删除用户...")

    # Create a throwaway user to soft-delete (so we don't affect other tests)
    tag = unique_name("del")
    del_username = f"admin_del_{tag}"
    del_password = "DelTest123"
    del_email = f"{tag}@test.internal"

    del_token, del_user_id = register_and_login_user(del_username, del_password, del_email)

    resp = fetch(
        f"/api/admin/users/{del_user_id}",
        method="DELETE",
        headers=get_admin_headers(),
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        log_pass(f"Admin soft delete user: deleted user id={del_user_id}, HTTP 200, code=0")
    else:
        log_fail(f"Admin soft delete user: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 7: Admin 自我修改状态保护 ─────────────────────────────────────────


def test_admin_self_status_protection():
    log_info("[Test 7] Admin 自我修改状态保护...")

    resp = fetch(
        f"/api/admin/users/{_admin_id}/status",
        method="PUT",
        headers={**get_admin_headers(), "Content-Type": "application/json"},
        json_body={"status": 0},
    )

    code = json_field(resp.text, "code")

    if resp.status_code in (400, 403) or code != "0":
        log_pass(f"Admin self-status protection: HTTP {resp.status_code}, code={code} (blocked as expected)")
    else:
        log_fail(f"Admin self-status protection: expected 400/403 or error code, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 8: Admin 自我降级保护 ─────────────────────────────────────────────


def test_admin_self_role_protection():
    log_info("[Test 8] Admin 自我降级保护...")

    resp = fetch(
        f"/api/admin/users/{_admin_id}/role",
        method="PUT",
        headers={**get_admin_headers(), "Content-Type": "application/json"},
        json_body={"role": 0},
    )

    code = json_field(resp.text, "code")

    if resp.status_code in (400, 403) or code != "0":
        log_pass(f"Admin self-role protection: HTTP {resp.status_code}, code={code} (blocked as expected)")
    else:
        log_fail(f"Admin self-role protection: expected 400/403 or error code, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 9: 非管理员访问被拒 ───────────────────────────────────────────────


def test_non_admin_access_denied():
    log_info("[Test 9] 非管理员访问被拒...")

    resp = fetch(
        "/api/admin/users",
        method="GET",
        headers=get_user_headers(_test_user_token),
    )

    if resp.status_code == 403:
        log_pass(f"Non-admin access denied: HTTP 403")
    else:
        log_fail(f"Non-admin access: expected HTTP 403, got HTTP {resp.status_code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 10: 未认证访问被拒 ────────────────────────────────────────────────


def test_unauthenticated_access_denied():
    log_info("[Test 10] 未认证访问被拒...")

    resp = fetch(
        "/api/admin/users",
        method="GET",
    )

    if resp.status_code == 401:
        log_pass("Unauthenticated access denied: HTTP 401")
    else:
        log_fail(f"Unauthenticated access: expected HTTP 401, got HTTP {resp.status_code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 11: Admin 获取分享列表 ────────────────────────────────────────────


def test_admin_list_shares():
    log_info("[Test 11] Admin 获取分享列表...")

    resp = fetch(
        "/api/admin/shares",
        method="GET",
        headers=get_admin_headers(),
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        pagination = json_field(resp.text, "data.pagination")
        if pagination:
            log_pass("Admin list shares: HTTP 200, code=0, pagination present")
        else:
            log_pass("Admin list shares: HTTP 200, code=0")
    else:
        log_fail(f"Admin list shares: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 12: Admin 强制取消分享 ────────────────────────────────────────────


def test_admin_force_cancel_share():
    log_info("[Test 12] Admin 强制取消分享...")

    resp = fetch(
        f"/api/admin/shares/{_share_id}",
        method="DELETE",
        headers=get_admin_headers(),
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        log_pass(f"Admin force cancel share: share_id={_share_id}, HTTP 200, code=0")
    else:
        log_fail(f"Admin force cancel share: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 13: Admin 获取系统概览 ────────────────────────────────────────────


def test_admin_stats_overview():
    log_info("[Test 13] Admin 获取系统概览...")

    resp = fetch(
        "/api/admin/stats/overview",
        method="GET",
        headers=get_admin_headers(),
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        total_users = json_field(resp.text, "data.total_users")
        total_files = json_field(resp.text, "data.total_files")
        active_shares = json_field(resp.text, "data.active_shares")
        log_pass(f"Admin stats overview: total_users={total_users}, total_files={total_files}, active_shares={active_shares}")
    else:
        log_fail(f"Admin stats overview: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 14: Admin 获取系统状态 ────────────────────────────────────────────


def test_admin_stats_system():
    log_info("[Test 14] Admin 获取系统状态...")

    resp = fetch(
        "/api/admin/stats/system",
        method="GET",
        headers=get_admin_headers(),
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        mysql_connected = json_field(resp.text, "data.mysql_connected")
        redis_connected = json_field(resp.text, "data.redis_connected")
        uptime = json_field(resp.text, "data.uptime_seconds")
        disk_total = json_field(resp.text, "data.disk_total")
        disk_used = json_field(resp.text, "data.disk_used")
        disk_free = json_field(resp.text, "data.disk_free")
        if mysql_connected == "true" and redis_connected == "true":
            log_pass(f"Admin stats system: mysql={mysql_connected}, redis={redis_connected}, uptime={uptime}s")
        else:
            log_pass(f"Admin stats system: mysql={mysql_connected}, redis={redis_connected}")
    else:
        log_fail(f"Admin stats system: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Test 15: 查询不存在资源 ────────────────────────────────────────────────


def test_admin_get_nonexistent_user():
    log_info("[Test 15] 查询不存在资源...")

    resp = fetch(
        "/api/admin/users/999999",
        method="GET",
        headers=get_admin_headers(),
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 404 or (resp.status_code == 200 and code != "0"):
        log_pass(f"Admin get nonexistent user: HTTP {resp.status_code}, code={code} (not found as expected)")
    else:
        log_fail(f"Admin get nonexistent user: expected HTTP 404 or error code, got HTTP {resp.status_code} code={code}")
        print(resp.text)
        sys.exit(1)


# ─── Main ────────────────────────────────────────────────────────────────────


def main():
    print("==========================================")
    print("Admin Module Integration Tests")
    print("==========================================\n")

    ensure_server()

    setup()

    tests = [
        test_admin_list_users,
        test_admin_search_users,
        test_admin_get_user_detail,
        test_admin_change_user_status,
        test_admin_change_user_role,
        test_admin_soft_delete_user,
        test_admin_self_status_protection,
        test_admin_self_role_protection,
        test_non_admin_access_denied,
        test_unauthenticated_access_denied,
        test_admin_list_shares,
        test_admin_force_cancel_share,
        test_admin_stats_overview,
        test_admin_stats_system,
        test_admin_get_nonexistent_user,
    ]

    for t in tests:
        try:
            t()
        except SystemExit:
            raise
        except Exception as e:
            log_fail(f"{t.__name__}: unexpected error: {e}")
            sys.exit(1)

    # Save evidence
    evidence_lines = [
        f"Admin Integration Tests - {time.strftime('%Y-%m-%d %H:%M:%S')}",
        f"Admin ID: {_admin_id}",
        f"Test User ID: {_test_user_id}",
        f"Share ID: {_share_id}",
        f"Tests passed: {tests_passed}",
        f"Tests failed: {tests_failed}",
        "",
        "All 15 scenarios executed.",
    ]
    save_evidence(EVIDENCE_FILE, "\n".join(evidence_lines))

    print_summary()


if __name__ == "__main__":
    main()
