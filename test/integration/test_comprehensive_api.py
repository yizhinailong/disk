#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Comprehensive backend API test — covers all 50 endpoints across 10 domains.

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL + Redis configured
  - test001/Test123 user exists
  - admin/Admin123 user exists

Usage:
  uv run test/integration/test_comprehensive_api.py
"""

from __future__ import annotations

import atexit
import hashlib
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_info,
    log_pass,
    log_fail,
    log_step,
    log_section,
    save_evidence,
    print_summary,
    json_field,
    fetch,
    header_value,
    redis_delete_pattern,
    check_server,
    cleanup,
    do_login,
    send_login_request,
    assert_status,
    assert_json_field,
    assert_json_array_not_empty,
    create_temp_file,
    md5_hash,
    unique_name,
)
from lib_py.reporter import ReportGenerator

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")

ALL_RESULTS: list[tuple[str, str, str, list[dict]]] = []


def auth_hdr(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}", "Content-Type": "application/json"}


def share_hdr(token: str) -> dict[str, str]:
    return {"X-Share-Token": token}


def _record(domain: str, endpoint: str, method: str, tests: list[dict]) -> None:
    ALL_RESULTS.append((domain, endpoint, method, tests))


def _test(name: str, ok: bool, expected: str = "", actual: str = "", detail: str = "") -> dict:
    if ok:
        log_pass(name)
    else:
        log_fail(name)
    return {"name": name, "passed": ok, "expected": expected, "actual": actual, "detail": detail}


# ─── Helpers ──────────────────────────────────────────────────────────────────

def register_user(username: str, password: str, email: str = "") -> dict | None:
    resp = fetch("/api/auth/register", method="POST",
                 headers={"Content-Type": "application/json"},
                 json_body={"username": username, "password": password, "email": email or f"{username}@test.com"})
    code = json_field(resp.text, "code")
    if code == "0":
        s, b = send_login_request(username, password)
        return {
            "access_token": json_field(b, "data.access_token"),
            "refresh_token": json_field(b, "data.refresh_token"),
            "username": username,
            "password": password,
        }
    return None


def upload_file(token: str, filename: str, parent_id: int | str = 0) -> dict | None:
    tmp = create_temp_file(size_bytes=256 * 1024)
    file_md5 = md5_hash(tmp)
    file_size = 256 * 1024
    resp = fetch("/api/file/upload/init", method="POST", headers=auth_hdr(token),
                 json_body={"filename": filename, "file_size": file_size,
                            "file_hash": file_md5, "parent_id": int(parent_id)})
    if json_field(resp.text, "code") != "0":
        return None
    upload_id = json_field(resp.text, "data.upload_id")
    instant = json_field(resp.text, "data.instant_upload")
    file_id = json_field(resp.text, "data.file.id")

    if instant == "true" and file_id and file_id != "null" and file_id != "0":
        os.unlink(tmp)
        return {"file_id": file_id, "upload_id": upload_id, "md5": file_md5, "tmp_path": None}

    with open(tmp, "rb") as f:
        chunk = f.read()
    chunk_hash_val = hashlib.md5(chunk).hexdigest()
    resp = fetch(f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={chunk_hash_val}",
                 method="POST",
                 headers={"Authorization": f"Bearer {token}", "Content-Type": "application/octet-stream"},
                 data=chunk)
    if resp.status_code != 200:
        os.unlink(tmp)
        return None
    time.sleep(0.5)

    resp = fetch("/api/file/upload/complete", method="POST", headers=auth_hdr(token),
                 json_body={"upload_id": upload_id})
    file_id = json_field(resp.text, "data.file.id")
    os.unlink(tmp)
    time.sleep(0.5)
    return {"file_id": file_id, "upload_id": upload_id, "md5": file_md5, "tmp_path": None}


# ─── Auth Domain (4 endpoints) ────────────────────────────────────────────────

def test_auth_domain() -> list[tuple[str, str, str, list[dict]]]:
    log_section("Auth Domain (4 endpoints)")
    results: list[tuple[str, str, str, list[dict]]] = []

    # 1. POST /api/auth/register
    tests: list[dict] = []
    uname = unique_name("reg")
    info = register_user(uname, "RegPass123")
    tests.append(_test("register_happy", info is not None, "code=0", "ok" if info else "fail"))
    dup = register_user(uname, "RegPass123")
    tests.append(_test("register_duplicate_fails", dup is None, "code!=0", "ok" if dup is None else "fail"))
    bad = register_user("ab", "short")
    tests.append(_test("register_invalid_username", bad is None, "code!=0", "ok" if bad is None else "fail"))
    bad_pw = register_user("goodname99", "short")
    tests.append(_test("register_invalid_password", bad_pw is None, "code!=0", "ok" if bad_pw is None else "fail"))
    results.append(("Auth", "/api/auth/register", "POST", tests))

    # 2. POST /api/auth/login
    tests = []
    s, b = send_login_request("test001", "Test1234")
    tok = json_field(b, "data.access_token")
    tests.append(_test("login_user", s == 200 and tok and tok != "null", "200+token", f"{s}"))
    s2, b2 = send_login_request("admin", "Admin123")
    atok = json_field(b2, "data.access_token")
    tests.append(_test("login_admin", s2 == 200 and atok and atok != "null", "200+token", f"{s2}"))
    s3, _ = send_login_request("test001", "WrongPass1")
    tests.append(_test("login_wrong_pass", s3 != 200 or json_field(_, "code") != "0", "error", f"{s3}"))
    s4, _ = send_login_request("nouser_xyz_999", "NoPass123")
    tests.append(_test("login_no_user", s4 != 200 or json_field(_, "code") != "0", "error", f"{s4}"))
    resp = fetch("/api/auth/login", method="POST", headers={"Content-Type": "application/json"}, json_body={})
    tests.append(_test("login_empty_body", resp.status_code != 200 or json_field(resp.text, "code") != "0", "error", f"{resp.status_code}"))
    results.append(("Auth", "/api/auth/login", "POST", tests))

    # 3. POST /api/auth/refresh
    tests = []
    rt = json_field(b, "data.refresh_token")
    resp = fetch("/api/auth/refresh", method="POST", headers={"Content-Type": "application/json"},
                 json_body={"refresh_token": rt})
    new_tok = json_field(resp.text, "data.access_token")
    tests.append(_test("refresh_ok", json_field(resp.text, "code") == "0" and new_tok and new_tok != "null",
                        "code=0+token", json_field(resp.text, "code")))
    resp2 = fetch("/api/auth/refresh", method="POST", headers={"Content-Type": "application/json"},
                  json_body={"refresh_token": "invalid.refresh.token"})
    tests.append(_test("refresh_invalid", json_field(resp2.text, "code") != "0", "code!=0", json_field(resp2.text, "code")))
    results.append(("Auth", "/api/auth/refresh", "POST", tests))

    # 4. POST /api/auth/logout
    tests = []
    s_lg, b_lg = send_login_request("test001", "Test1234")
    logout_tok = json_field(b_lg, "data.access_token")
    if logout_tok and logout_tok != "null":
        resp = fetch("/api/auth/logout", method="POST", headers=auth_hdr(logout_tok))
        tests.append(_test("logout_ok", resp.status_code == 200, "200", f"{resp.status_code}"))
        resp3 = fetch("/api/user/profile", method="GET", headers=auth_hdr(logout_tok))
        tests.append(_test("logout_token_invalid", resp3.status_code == 401, "401", f"{resp3.status_code}"))
    else:
        tests.append(_test("logout_ok", False, "200", "no token"))
        tests.append(_test("logout_token_invalid", False, "401", "no token"))
    results.append(("Auth", "/api/auth/logout", "POST", tests))

    for r in results:
        _record(*r)
    return results


# ─── User Domain (4 endpoints) ────────────────────────────────────────────────

def test_user_domain(user_token: str, temp_user: dict) -> list[tuple[str, str, str, list[dict]]]:
    log_section("User Domain (4 endpoints)")
    results: list[tuple[str, str, str, list[dict]]] = []

    # 5. GET /api/user/profile
    tests = []
    resp = fetch("/api/user/profile", method="GET", headers=auth_hdr(user_token))
    code = json_field(resp.text, "code")
    uname = json_field(resp.text, "data.user.username")
    tests.append(_test("profile_ok", code == "0" and uname and uname != "null", "code=0+username", f"{code},{uname}"))
    resp2 = fetch("/api/user/profile", method="GET", headers={})
    tests.append(_test("profile_no_auth", resp2.status_code == 401, "401", f"{resp2.status_code}"))
    results.append(("User", "/api/user/profile", "GET", tests))

    # 6. PATCH /api/user/profile
    tests = []
    new_nick = unique_name("nick")
    resp = fetch("/api/user/profile", method="PATCH", headers=auth_hdr(user_token),
                 json_body={"nickname": new_nick})
    tests.append(_test("update_profile_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("User", "/api/user/profile", "PATCH", tests))

    # 7. PUT /api/user/password — use throwaway user
    tests = []
    t_tok = temp_user["access_token"]
    new_pw = "NewPass456"
    resp = fetch("/api/user/password", method="PUT", headers=auth_hdr(t_tok),
                 json_body={"old_password": temp_user["password"], "new_password": new_pw})
    tests.append(_test("change_pw_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp_bad = fetch("/api/user/password", method="PUT", headers=auth_hdr(t_tok),
                     json_body={"old_password": "WrongOld123", "new_password": "Xx12345678"})
    tests.append(_test("change_pw_wrong_old", json_field(resp_bad.text, "code") != "0", "code!=0", json_field(resp_bad.text, "code")))
    resp_fmt = fetch("/api/user/password", method="PUT", headers=auth_hdr(t_tok),
                     json_body={"old_password": new_pw, "new_password": "short"})
    tests.append(_test("change_pw_bad_format", json_field(resp_fmt.text, "code") != "0", "code!=0", json_field(resp_fmt.text, "code")))
    s, b = send_login_request(temp_user["username"], new_pw)
    tests.append(_test("change_pw_new_works", s == 200 and json_field(b, "data.access_token") != "", "200+token", f"{s}"))
    temp_user["access_token"] = json_field(b, "data.access_token")
    temp_user["password"] = new_pw
    results.append(("User", "/api/user/password", "PUT", tests))

    # 8. GET /api/user/storage
    tests = []
    resp = fetch("/api/user/storage", method="GET", headers=auth_hdr(user_token))
    code = json_field(resp.text, "code")
    su = json_field(resp.text, "data.storage_used")
    tests.append(_test("storage_ok", code == "0", "code=0", f"{code}"))
    resp2 = fetch("/api/user/storage", method="GET", headers={})
    tests.append(_test("storage_no_auth", resp2.status_code == 401, "401", f"{resp2.status_code}"))
    results.append(("User", "/api/user/storage", "GET", tests))

    for r in results:
        _record(*r)
    return results


# ─── Folder Domain (3 endpoints) ──────────────────────────────────────────────

def test_folder_domain(user_token: str) -> tuple[list, str, str]:
    log_section("Folder Domain (3 endpoints)")
    results: list[tuple[str, str, str, list[dict]]] = []
    folder_id = "0"
    sub_folder_id = "0"

    # 22. POST /api/folder/create
    tests = []
    fname = unique_name("folder")
    resp = fetch("/api/folder/create", method="POST", headers=auth_hdr(user_token),
                 json_body={"name": fname, "parent_id": 0})
    code = json_field(resp.text, "code")
    folder_id = json_field(resp.text, "data.id")
    tests.append(_test("create_folder_root", code == "0" and folder_id and folder_id != "0", "code=0+id", f"{code},{folder_id}"))
    resp_dup = fetch("/api/folder/create", method="POST", headers=auth_hdr(user_token),
                     json_body={"name": fname, "parent_id": 0})
    tests.append(_test("create_folder_dup", json_field(resp_dup.text, "code") != "0", "code!=0", json_field(resp_dup.text, "code")))
    resp_inv = fetch("/api/folder/create", method="POST", headers=auth_hdr(user_token),
                     json_body={"name": "bad/name", "parent_id": 0})
    tests.append(_test("create_folder_invalid", json_field(resp_inv.text, "code") != "0", "code!=0", json_field(resp_inv.text, "code")))
    sname = unique_name("sub")
    resp_sub = fetch("/api/folder/create", method="POST", headers=auth_hdr(user_token),
                     json_body={"name": sname, "parent_id": int(folder_id)})
    sub_folder_id = json_field(resp_sub.text, "data.id")
    tests.append(_test("create_subfolder", json_field(resp_sub.text, "code") == "0" and sub_folder_id and sub_folder_id != "0",
                        "code=0+id", json_field(resp_sub.text, "code")))
    results.append(("Folder", "/api/folder/create", "POST", tests))

    # 23. GET /api/folder/tree
    tests = []
    resp = fetch("/api/folder/tree", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("folder_tree_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp2 = fetch("/api/folder/tree", method="GET", headers={})
    tests.append(_test("folder_tree_no_auth", resp2.status_code == 401, "401", f"{resp2.status_code}"))
    results.append(("Folder", "/api/folder/tree", "GET", tests))

    # 24. GET /api/folder/{folder_id}/breadcrumb
    tests = []
    resp = fetch(f"/api/folder/{folder_id}/breadcrumb", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("breadcrumb_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp2 = fetch("/api/folder/99999999/breadcrumb", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("breadcrumb_invalid", json_field(resp2.text, "code") != "0", "code!=0", json_field(resp2.text, "code")))
    results.append(("Folder", "/api/folder/{folder_id}/breadcrumb", "GET", tests))

    for r in results:
        _record(*r)
    return results, folder_id, sub_folder_id


# ─── File Domain (13 endpoints) ───────────────────────────────────────────────

def test_file_domain(user_token: str, folder_id: str) -> list[tuple[str, str, str, list[dict]]]:
    log_section("File Domain (13 endpoints)")
    results: list[tuple[str, str, str, list[dict]]] = []
    file_id = ""
    copy_file_id = ""
    upload_md5 = ""

    # Upload flow (9-12)
    tmp = create_temp_file(size_bytes=256 * 1024)
    file_md5 = md5_hash(tmp)
    file_size = 256 * 1024

    # 9. POST /api/file/upload/init
    tests = []
    resp = fetch("/api/file/upload/init", method="POST", headers=auth_hdr(user_token),
                 json_body={"filename": f"{unique_name('file')}.dat", "file_size": file_size,
                            "file_hash": file_md5, "parent_id": 0})
    code = json_field(resp.text, "code")
    upload_id = json_field(resp.text, "data.upload_id")
    tests.append(_test("upload_init_ok", code == "0" and upload_id, "code=0+id", f"{code}"))
    resp_inv = fetch("/api/file/upload/init", method="POST", headers=auth_hdr(user_token), json_body={})
    tests.append(_test("upload_init_missing", json_field(resp_inv.text, "code") != "0", "code!=0", json_field(resp_inv.text, "code")))
    results.append(("File", "/api/file/upload/init", "POST", tests))
    time.sleep(0.5)

    # 10. POST /api/file/upload/chunk
    tests = []
    with open(tmp, "rb") as f:
        chunk = f.read()
    chunk_hash_val = hashlib.md5(chunk).hexdigest()
    resp = fetch(f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={chunk_hash_val}",
                 method="POST",
                 headers={"Authorization": f"Bearer {user_token}", "Content-Type": "application/octet-stream"},
                 data=chunk)
    tests.append(_test("upload_chunk_ok", resp.status_code == 200, "200", f"{resp.status_code}"))
    resp_inv = fetch("/api/file/upload/chunk?upload_id=invalid_id&chunk_index=0&chunk_hash=00000000000000000000000000000000",
                     method="POST",
                     headers={"Authorization": f"Bearer {user_token}", "Content-Type": "application/octet-stream"},
                     data=chunk)
    tests.append(_test("upload_chunk_bad_id", resp_inv.status_code != 200 or json_field(resp_inv.text, "code") != "0",
                        "error", f"{resp_inv.status_code}"))
    results.append(("File", "/api/file/upload/chunk", "POST", tests))
    time.sleep(0.5)

    # 11. POST /api/file/upload/complete
    tests = []
    resp = fetch("/api/file/upload/complete", method="POST", headers=auth_hdr(user_token),
                 json_body={"upload_id": upload_id})
    code = json_field(resp.text, "code")
    file_id = json_field(resp.text, "data.file.id")
    tests.append(_test("upload_complete_ok", code == "0" and file_id, "code=0+id", f"{code},{file_id}"))
    upload_md5 = file_md5
    results.append(("File", "/api/file/upload/complete", "POST", tests))
    time.sleep(0.5)

    # 12. DELETE /api/file/upload/{upload_id} — cancel a second upload
    tests = []
    resp2 = fetch("/api/file/upload/init", method="POST", headers=auth_hdr(user_token),
                  json_body={"filename": f"{unique_name('cancel')}.dat", "file_size": 1024,
                             "file_hash": "d41d8cd98f00b204e9800998ecf8427e", "parent_id": 0})
    cancel_id = json_field(resp2.text, "data.upload_id")
    if cancel_id:
        resp3 = fetch(f"/api/file/upload/{cancel_id}", method="DELETE", headers=auth_hdr(user_token))
        tests.append(_test("upload_cancel_ok", json_field(resp3.text, "code") == "0", "code=0", json_field(resp3.text, "code")))
    else:
        tests.append(_test("upload_cancel_ok", False, "code=0", "no upload_id"))
    results.append(("File", "/api/file/upload/{upload_id}", "DELETE", tests))
    time.sleep(0.5)

    # 13. GET /api/file/list
    tests = []
    resp = fetch("/api/file/list?parent_id=0&page=1&page_size=20", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("file_list_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("File", "/api/file/list", "GET", tests))

    # 14. GET /api/file/{file_id}
    tests = []
    resp = fetch(f"/api/file/{file_id}", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("file_detail_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp2 = fetch("/api/file/99999999", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("file_detail_notfound", json_field(resp2.text, "code") != "0", "code!=0", json_field(resp2.text, "code")))
    results.append(("File", "/api/file/{file_id}", "GET", tests))

    # 15. GET /api/file/download/{file_id}/info
    tests = []
    resp = fetch(f"/api/file/download/{file_id}/info", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("download_info_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("File", "/api/file/download/{file_id}/info", "GET", tests))

    # 16. GET /api/file/download/{file_id}
    tests = []
    resp = fetch(f"/api/file/download/{file_id}", method="GET", headers={"Authorization": f"Bearer {user_token}"})
    tests.append(_test("download_ok", resp.status_code == 200, "200", f"{resp.status_code}"))
    resp_range = fetch(f"/api/file/download/{file_id}", method="GET",
                       headers={"Authorization": f"Bearer {user_token}", "Range": "bytes=0-1023"})
    tests.append(_test("download_range_206", resp_range.status_code == 206, "206", f"{resp_range.status_code}"))
    resp_bad_range = fetch(f"/api/file/download/{file_id}", method="GET",
                           headers={"Authorization": f"Bearer {user_token}", "Range": "bytes=invalid"})
    tests.append(_test("download_bad_range", resp_bad_range.status_code in (400, 416), "400/416", f"{resp_bad_range.status_code}"))
    results.append(("File", "/api/file/download/{file_id}", "GET", tests))

    # 17. PUT /api/file/{file_id}/rename
    tests = []
    new_name = unique_name("ren") + ".dat"
    resp = fetch(f"/api/file/{file_id}/rename", method="PUT", headers=auth_hdr(user_token),
                 json_body={"new_name": new_name})
    tests.append(_test("rename_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp_inv = fetch(f"/api/file/{file_id}/rename", method="PUT", headers=auth_hdr(user_token),
                     json_body={"new_name": "bad/name"})
    tests.append(_test("rename_invalid", json_field(resp_inv.text, "code") != "0", "code!=0", json_field(resp_inv.text, "code")))
    results.append(("File", "/api/file/{file_id}/rename", "PUT", tests))

    # 18. PUT /api/file/move
    tests = []
    resp = fetch("/api/file/move", method="PUT", headers=auth_hdr(user_token),
                 json_body={"file_ids": [int(file_id)], "target_folder_id": int(folder_id)})
    tests.append(_test("move_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp_empty = fetch("/api/file/move", method="PUT", headers=auth_hdr(user_token),
                       json_body={"file_ids": []})
    tests.append(_test("move_empty", json_field(resp_empty.text, "code") != "0", "code!=0", json_field(resp_empty.text, "code")))
    results.append(("File", "/api/file/move", "PUT", tests))

    # 19. POST /api/file/copy
    tests = []
    resp = fetch("/api/file/copy", method="POST", headers=auth_hdr(user_token),
                 json_body={"file_ids": [int(file_id)], "target_folder_id": 0})
    code = json_field(resp.text, "code")
    copy_file_id = json_field(resp.text, "data.new_files.0.new_id")
    tests.append(_test("copy_ok", code == "0", "code=0", f"{code}"))
    results.append(("File", "/api/file/copy", "POST", tests))

    # 20. DELETE /api/file — soft delete copy
    tests = []
    resp = fetch("/api/file", method="DELETE", headers=auth_hdr(user_token),
                 json_body={"file_ids": [int(copy_file_id)]})
    tests.append(_test("soft_delete_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("File", "/api/file", "DELETE", tests))

    # 21. GET /api/file/search
    tests = []
    resp = fetch("/api/file/search?keyword=test&page=1&page_size=10", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("search_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp_special = fetch("/api/file/search?keyword=%25%27%22&page=1&page_size=10", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("search_special_chars", resp_special.status_code in (200, 400), "200/400", f"{resp_special.status_code}"))
    results.append(("File", "/api/file/search", "GET", tests))

    os.unlink(tmp)
    for r in results:
        _record(*r)
    return results, file_id


# ─── Share Domain (8 endpoints) ───────────────────────────────────────────────

def test_share_domain(user_token: str, file_id: str) -> list[tuple[str, str, str, list[dict]]]:
    log_section("Share Domain (8 endpoints)")
    results: list[tuple[str, str, str, list[dict]]] = []
    share_id_no_pw = ""
    share_id_pw = ""

    # 25. POST /api/share — create two shares (no pw + with pw)
    tests = []
    resp = fetch("/api/share", method="POST", headers=auth_hdr(user_token),
                 json_body={"file_ids": [int(file_id)], "expire_days": 7})
    code = json_field(resp.text, "code")
    share_id_no_pw = json_field(resp.text, "data.share_id")
    tests.append(_test("share_create_no_pw", code == "0" and share_id_no_pw, "code=0+id", f"{code}"))
    resp2 = fetch("/api/share", method="POST", headers=auth_hdr(user_token),
                  json_body={"file_ids": [int(file_id)], "password": "abcd1234", "expire_days": 7})
    share_id_pw = json_field(resp2.text, "data.share_id")
    tests.append(_test("share_create_with_pw", json_field(resp2.text, "code") == "0", "code=0", json_field(resp2.text, "code")))
    resp3 = fetch("/api/share", method="POST", headers=auth_hdr(user_token),
                  json_body={"file_ids": [99999999]})
    tests.append(_test("share_create_invalid_file", json_field(resp3.text, "code") != "0", "code!=0", json_field(resp3.text, "code")))
    results.append(("Share", "/api/share", "POST", tests))

    # 26. GET /api/share — my shares
    tests = []
    resp = fetch("/api/share?page=1&page_size=20", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("share_list_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("Share", "/api/share", "GET", tests))

    # 27. GET /api/share/{share_id}
    tests = []
    resp = fetch(f"/api/share/{share_id_no_pw}", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("share_detail_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("Share", "/api/share/{share_id}", "GET", tests))

    # 28. PUT /api/share/{share_id}
    tests = []
    resp = fetch(f"/api/share/{share_id_no_pw}", method="PUT", headers=auth_hdr(user_token),
                 json_body={"expire_days": 30})
    tests.append(_test("share_update_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp2 = fetch("/api/share/99999999", method="PUT", headers=auth_hdr(user_token),
                  json_body={"expire_days": 30})
    tests.append(_test("share_update_invalid", json_field(resp2.text, "code") != "0", "code!=0", json_field(resp2.text, "code")))
    results.append(("Share", "/api/share/{share_id}", "PUT", tests))

    # 30. POST /api/share/access/{share_id} — public access
    tests = []
    resp = fetch(f"/api/share/access/{share_id_no_pw}", method="POST",
                 headers={"Content-Type": "application/json"}, json_body={})
    share_token = json_field(resp.text, "data.share_token")
    tests.append(_test("share_access_no_pw", json_field(resp.text, "code") == "0" and share_token, "code=0+token",
                        json_field(resp.text, "code")))
    resp2 = fetch(f"/api/share/access/{share_id_pw}", method="POST",
                  headers={"Content-Type": "application/json"}, json_body={"password": "wrong"})
    tests.append(_test("share_access_wrong_pw", json_field(resp2.text, "code") != "0", "code!=0", json_field(resp2.text, "code")))
    resp3 = fetch(f"/api/share/access/{share_id_pw}", method="POST",
                  headers={"Content-Type": "application/json"}, json_body={"password": "abcd1234"})
    pw_token = json_field(resp3.text, "data.share_token")
    tests.append(_test("share_access_correct_pw", json_field(resp3.text, "code") == "0" and pw_token, "code=0+token",
                        json_field(resp3.text, "code")))
    results.append(("Share", "/api/share/access/{share_id}", "POST", tests))

    # 31. GET /api/share/browse/{share_id}
    tests = []
    resp = fetch(f"/api/share/browse/{share_id_no_pw}", method="GET",
                 headers={**share_hdr(share_token), "Content-Type": "application/json"})
    tests.append(_test("share_browse_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp2 = fetch(f"/api/share/browse/{share_id_no_pw}", method="GET",
                  headers={"Content-Type": "application/json"})
    tests.append(_test("share_browse_no_token", resp2.status_code == 401, "401", f"{resp2.status_code}"))
    results.append(("Share", "/api/share/browse/{share_id}", "GET", tests))

    # 32. GET /api/share/download/{share_id}/{file_id}
    tests = []
    resp = fetch(f"/api/share/download/{share_id_no_pw}/{file_id}", method="GET",
                 headers=share_hdr(share_token))
    tests.append(_test("share_download_ok", resp.status_code == 200, "200", f"{resp.status_code}"))
    results.append(("Share", "/api/share/download/{share_id}/{file_id}", "GET", tests))

    # 29. DELETE /api/share — cancel shares (last, so other tests can use them)
    tests = []
    resp = fetch("/api/share", method="DELETE", headers=auth_hdr(user_token),
                 json_body={"share_ids": [share_id_no_pw, share_id_pw]})
    tests.append(_test("share_cancel_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("Share", "/api/share", "DELETE", tests))

    for r in results:
        _record(*r)
    return results


# ─── Trash Domain (4 endpoints) ───────────────────────────────────────────────

def test_trash_domain(user_token: str, file_id: str) -> list[tuple[str, str, str, list[dict]]]:
    log_section("Trash Domain (4 endpoints)")
    results: list[tuple[str, str, str, list[dict]]] = []
    trash_file_id = ""

    # Upload a file specifically for trash testing
    up = upload_file(user_token, unique_name("trash") + ".dat")
    trash_file_id = up["file_id"] if up else file_id

    # Soft delete it
    fetch("/api/file", method="DELETE", headers=auth_hdr(user_token),
          json_body={"file_ids": [int(trash_file_id)]})
    time.sleep(0.3)

    # 33. GET /api/trash
    tests = []
    resp = fetch("/api/trash?page=1&page_size=20", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("trash_list_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp2 = fetch("/api/trash?page=1&page_size=20", method="GET", headers={})
    tests.append(_test("trash_list_no_auth", resp2.status_code == 401, "401", f"{resp2.status_code}"))
    results.append(("Trash", "/api/trash", "GET", tests))
    time.sleep(0.3)

    # 34. POST /api/trash/restore
    tests = []
    resp = fetch("/api/trash/restore", method="POST", headers=auth_hdr(user_token),
                 json_body={"trash_ids": [int(trash_file_id)]})
    tests.append(_test("trash_restore_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("Trash", "/api/trash/restore", "POST", tests))
    time.sleep(0.3)

    # Delete again and permanently delete
    fetch("/api/file", method="DELETE", headers=auth_hdr(user_token),
          json_body={"file_ids": [int(trash_file_id)]})
    time.sleep(0.3)

    # 35. DELETE /api/trash — permanent delete
    tests = []
    resp = fetch("/api/trash", method="DELETE", headers=auth_hdr(user_token),
                 json_body={"trash_ids": [int(trash_file_id)]})
    tests.append(_test("trash_permanent_delete_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp_empty = fetch("/api/trash", method="DELETE", headers=auth_hdr(user_token),
                       json_body={"trash_ids": []})
    tests.append(_test("trash_delete_empty", json_field(resp_empty.text, "code") != "0", "code!=0", json_field(resp_empty.text, "code")))
    results.append(("Trash", "/api/trash", "DELETE", tests))
    time.sleep(0.3)

    # 36. DELETE /api/trash/all
    tests = []
    resp = fetch("/api/trash/all", method="DELETE", headers=auth_hdr(user_token))
    tests.append(_test("trash_clear_all_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("Trash", "/api/trash/all", "DELETE", tests))

    for r in results:
        _record(*r)
    return results


# ─── System + Health + Logs (3 endpoints) ─────────────────────────────────────

def test_system_health_logs(user_token: str) -> list[tuple[str, str, str, list[dict]]]:
    log_section("System + Health + Logs (3 endpoints)")
    results: list[tuple[str, str, str, list[dict]]] = []

    # 37. GET /api/system/info
    tests = []
    resp = fetch("/api/system/info", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("system_info_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp2 = fetch("/api/system/info", method="GET", headers={})
    tests.append(_test("system_info_no_auth", resp2.status_code == 401, "401", f"{resp2.status_code}"))
    results.append(("System", "/api/system/info", "GET", tests))

    # 38. GET /api/health — PUBLIC
    tests = []
    resp = fetch("/api/health", method="GET", headers={})
    code = json_field(resp.text, "code")
    tests.append(_test("health_ok", resp.status_code == 200 and code == "0", "200+code=0", f"{resp.status_code},{code}"))
    results.append(("Health", "/api/health", "GET", tests))

    # 39. GET /api/logs
    tests = []
    resp = fetch("/api/logs?page=1&page_size=10", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("logs_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp2 = fetch("/api/logs?page=1&page_size=10", method="GET", headers={})
    tests.append(_test("logs_no_auth", resp2.status_code == 401, "401", f"{resp2.status_code}"))
    results.append(("Logs", "/api/logs", "GET", tests))

    for r in results:
        _record(*r)
    return results


# ─── Admin Domain (11 endpoints) ──────────────────────────────────────────────

def test_admin_domain(admin_token: str, user_token: str, temp_user_id: str) -> list[tuple[str, str, str, list[dict]]]:
    log_section("Admin Domain (11 endpoints)")
    results: list[tuple[str, str, str, list[dict]]] = []

    # 40. GET /api/admin/users
    tests = []
    resp = fetch("/api/admin/users?page=1&page_size=20", method="GET", headers=auth_hdr(admin_token))
    tests.append(_test("admin_users_ok", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    resp2 = fetch("/api/admin/users?page=1&page_size=20", method="GET", headers=auth_hdr(user_token))
    tests.append(_test("admin_users_forbidden", resp2.status_code == 403, "403", f"{resp2.status_code}"))
    results.append(("Admin", "/api/admin/users", "GET", tests))

    # 41. GET /api/admin/users/{id}
    tests = []
    resp = fetch(f"/api/admin/users/{temp_user_id}", method="GET", headers=auth_hdr(admin_token))
    tests.append(_test("admin_user_detail_ok",
        json_field(resp.text, "code") == "0",
        "code=0", json_field(resp.text, "code")))
    resp2 = fetch("/api/admin/users/99999999", method="GET", headers=auth_hdr(admin_token))
    tests.append(_test("admin_user_detail_invalid", json_field(resp2.text, "code") != "0", "code!=0", json_field(resp2.text, "code")))
    results.append(("Admin", "/api/admin/users/{id}", "GET", tests))

    # 42. PUT /api/admin/users/{id}/status
    tests = []
    resp = fetch(f"/api/admin/users/{temp_user_id}/status", method="PUT", headers=auth_hdr(admin_token),
                 json_body={"status": 0})
    tests.append(_test("admin_disable_user",
        json_field(resp.text, "code") == "0",
        "code=0", json_field(resp.text, "code")))
    resp2 = fetch(f"/api/admin/users/{temp_user_id}/status", method="PUT", headers=auth_hdr(admin_token),
                   json_body={"status": 1})
    tests.append(_test("admin_enable_user",
        json_field(resp2.text, "code") == "0",
        "code=0", json_field(resp2.text, "code")))
    results.append(("Admin", "/api/admin/users/{id}/status", "PUT", tests))

    # 43. PUT /api/admin/users/{id}/role
    tests = []
    resp = fetch(f"/api/admin/users/{temp_user_id}/role", method="PUT", headers=auth_hdr(admin_token),
                 json_body={"role": 0})
    tests.append(_test("admin_change_role",
        json_field(resp.text, "code") == "0",
        "code=0", json_field(resp.text, "code")))
    results.append(("Admin", "/api/admin/users/{id}/role", "PUT", tests))

    # 44. DELETE /api/admin/users/{id} — delete throwaway
    tests = []
    resp = fetch(f"/api/admin/users/{temp_user_id}", method="DELETE", headers=auth_hdr(admin_token))
    tests.append(_test("admin_delete_user",
        json_field(resp.text, "code") == "0",
        "code=0", json_field(resp.text, "code")))
    results.append(("Admin", "/api/admin/users/{id}", "DELETE", tests))

    # 45. GET /api/admin/storage/stats
    tests = []
    resp = fetch("/api/admin/storage/stats", method="GET", headers=auth_hdr(admin_token))
    tests.append(_test("admin_storage_stats", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("Admin", "/api/admin/storage/stats", "GET", tests))

    # Create a share for admin share management tests
    up = upload_file(admin_token, unique_name("adminshare") + ".dat")
    admin_share_id = ""
    admin_share_db_id = ""
    if up:
        resp = fetch("/api/share", method="POST", headers=auth_hdr(admin_token),
                     json_body={"file_ids": [int(up["file_id"])], "expire_days": 7})
        admin_share_id = json_field(resp.text, "data.share_id")
        # Admin endpoints use integer DB id, not share_code
        if admin_share_id:
            list_resp = fetch("/api/admin/shares?page=1&page_size=50", method="GET", headers=auth_hdr(admin_token))
            try:
                shares_data = json.loads(list_resp.text)
                for s in shares_data.get("data", {}).get("items", []):
                    if str(s.get("share_code", "")) == admin_share_id:
                        admin_share_db_id = str(s["id"])
                        break
            except Exception:
                pass

    # 46. GET /api/admin/shares
    tests = []
    resp = fetch("/api/admin/shares?page=1&page_size=20", method="GET", headers=auth_hdr(admin_token))
    tests.append(_test("admin_shares_list", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("Admin", "/api/admin/shares", "GET", tests))

    # 47. GET /api/admin/shares/{id}
    tests = []
    if admin_share_db_id:
        resp = fetch(f"/api/admin/shares/{admin_share_db_id}", method="GET", headers=auth_hdr(admin_token))
        tests.append(_test("admin_share_detail",
            json_field(resp.text, "code") == "0",
            "code=0", json_field(resp.text, "code")))
    else:
        tests.append(_test("admin_share_detail", False, "code=0", "no share db id"))
    results.append(("Admin", "/api/admin/shares/{id}", "GET", tests))

    # 48. DELETE /api/admin/shares/{id}
    tests = []
    if admin_share_db_id:
        resp = fetch(f"/api/admin/shares/{admin_share_db_id}", method="DELETE", headers=auth_hdr(admin_token))
        tests.append(_test("admin_force_cancel_share",
            json_field(resp.text, "code") == "0",
            "code=0", json_field(resp.text, "code")))
    else:
        tests.append(_test("admin_force_cancel_share", False, "code=0", "no share db id"))
    results.append(("Admin", "/api/admin/shares/{id}", "DELETE", tests))

    # 49. GET /api/admin/stats/overview
    tests = []
    resp = fetch("/api/admin/stats/overview", method="GET", headers=auth_hdr(admin_token))
    tests.append(_test("admin_stats_overview", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("Admin", "/api/admin/stats/overview", "GET", tests))

    # 50. GET /api/admin/stats/system
    tests = []
    resp = fetch("/api/admin/stats/system", method="GET", headers=auth_hdr(admin_token))
    tests.append(_test("admin_stats_system", json_field(resp.text, "code") == "0", "code=0", json_field(resp.text, "code")))
    results.append(("Admin", "/api/admin/stats/system", "GET", tests))

    for r in results:
        _record(*r)
    return results


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    print("=" * 60)
    print("Comprehensive Backend API Test")
    print("=" * 60)
    print()

    atexit.register(cleanup)
    check_server()

    try:
        redis_delete_pattern("rate:*")
    except Exception:
        log_info("Redis cleanup skipped (not directly accessible)")

    log_step("Logging in as test001 and admin...")
    user_token = do_login("test001", "Test1234")
    if not user_token:
        log_fail("Cannot login as test001")
        sys.exit(1)
    admin_token = do_login("admin", "Admin123")
    if not admin_token:
        log_fail("Cannot login as admin")
        sys.exit(1)

    log_step("Registering throwaway user for destructive tests...")
    temp_uname = unique_name("temp")
    temp_info = register_user(temp_uname, "TempPass123")
    if not temp_info:
        log_fail("Cannot register throwaway user")
        sys.exit(1)
    temp_user_id = ""
    s_t, b_t = send_login_request(temp_uname, "TempPass123")
    temp_user_id = json_field(b_t, "data.user.id")
    if not temp_user_id or temp_user_id == "null" or temp_user_id == "":
        log_fail("Cannot get temp user ID from login response")
        sys.exit(1)
    log_pass(f"Throwaway user: {temp_uname} (id={temp_user_id})")

    # Run all domain tests in order
    test_auth_domain()
    test_user_domain(user_token, temp_info)

    folder_results, folder_id, sub_folder_id = test_folder_domain(user_token)
    file_results, file_id = test_file_domain(user_token, folder_id)

    test_share_domain(user_token, file_id)
    test_trash_domain(user_token, file_id)
    test_system_health_logs(user_token)
    test_admin_domain(admin_token, user_token, temp_user_id)

    # Generate report
    log_section("Generating Report")
    report = ReportGenerator("后端 API 全面测试报告", ".sisyphus/output/api-test-report.md")
    report.add_meta({
        "backend_url": BASE_URL,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "accounts": "test001 (普通用户), admin (管理员)",
    })
    for domain, endpoint, method, tests in ALL_RESULTS:
        report.add_section(domain, endpoint, method, tests)
    content = report.generate()
    log_pass(f"Report written to .sisyphus/output/api-test-report.md")

    # Save evidence
    total = sum(len(t) for _, _, _, t in ALL_RESULTS)
    passed = sum(1 for _, _, _, tests in ALL_RESULTS for t in tests if t["passed"])
    failed = total - passed
    evidence = {
        "total": total, "passed": passed, "failed": failed,
        "rate": f"{int(passed/total*100) if total > 0 else 0}%",
        "domains": len(set(d for d, _, _, _ in ALL_RESULTS)),
        "endpoints": len(ALL_RESULTS),
    }
    save_evidence("comp-api-summary.json", json.dumps(evidence, indent=2, ensure_ascii=False))

    # Print summary
    print_summary()
    print()
    print(f"Report: .sisyphus/output/api-test-report.md")
    print(f"Endpoints tested: {len(ALL_RESULTS)}")
    print(f"Total tests: {total} | Passed: {passed} | Failed: {failed}")

    if failed > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
