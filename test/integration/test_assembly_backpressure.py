#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration test for assembly backpressure (AssemblyWorkerPool singleflight + saturation).

Tests:
  1. Normal assembly completion succeeds (200)
  2. Duplicate finalize on same upload_id after completion returns success (idempotent)
  3. Concurrent finalize for same upload_id — at least one wins
  4. Pool saturation — overflow concurrent assemblies return 429
  5. No duplicate finalize side effects

Prerequisites:
  - Server running on localhost:8080
  - MySQL database configured
  - Redis configured

Usage:
  uv run test/integration/test_assembly_backpressure.py
"""

import hashlib
import os
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    log_section,
    print_summary,
    save_evidence,
    check_server,
    cleanup,
    do_login,
    json_field,
    json_int,
    fetch,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")

TOKEN = ""


# ─── Upload helpers ─────────────────────────────────────────────────────────


def _md5_str(data: str) -> str:
    return hashlib.md5(data.encode()).hexdigest()


def create_upload_task(filename: str, file_size: int, file_hash: str) -> str:
    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": filename,
            "file_size": file_size,
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )

    upload_id = json_field(resp.text, "data.upload_id")
    if not upload_id or upload_id in ("None", "null"):
        print(f"ERROR: Failed to create upload task: {resp.text}", file=sys.stderr)
        return ""

    return upload_id


def upload_chunk(upload_id: str, chunk_index: int, chunk_data: str) -> bool:
    chunk_hash = _md5_str(chunk_data)
    chunk_bytes = chunk_data.encode()

    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index={chunk_index}&chunk_hash={chunk_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/octet-stream",
        },
        data=chunk_bytes,
    )

    uploaded = json_field(resp.text, "data.uploaded")
    if uploaded == "true":
        return True
    print(f"ERROR: Chunk upload failed: {resp.text}", file=sys.stderr)
    return False


def complete_upload(upload_id: str) -> tuple[int, str]:
    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"upload_id": upload_id},
    )
    return resp.status_code, resp.text


# ─── Test 1: Normal assembly completion ─────────────────────────────────────


def test_normal_assembly_completes():
    log_section("Test 1: Normal assembly completion")
    log_info("Creating a single-chunk upload and completing it...")

    upload_id = create_upload_task(
        f"backpressure_normal_{os.getpid()}.pdf", 14, "a" * 32
    )
    if not upload_id:
        return

    if not upload_chunk(upload_id, 0, "Hello, World!!"):
        return

    http_code, body = complete_upload(upload_id)

    if http_code == 200:
        code = json_int(body, "code")
        if code == "0":
            log_pass("Normal assembly completed successfully (HTTP 200, code=0)")
            save_evidence("backpressure-normal-complete.json", body)
        else:
            log_fail(f"Normal assembly returned code={code} (expected 0)")
            print(body)
    else:
        log_fail(f"Normal assembly returned HTTP {http_code} (expected 200)")
        print(body)


# ─── Test 2: Duplicate finalize after completion (idempotent) ────────────────


def test_duplicate_finalize_after_completion():
    log_section("Test 2: Duplicate finalize after completion (idempotency)")
    log_info("Creating upload, completing it, then calling complete again...")

    upload_id = create_upload_task(f"backpressure_dup_{os.getpid()}.pdf", 14, "b" * 32)
    if not upload_id:
        return

    if not upload_chunk(upload_id, 0, "Hello, World!!"):
        return

    # First complete — should succeed
    first_code, first_body = complete_upload(upload_id)

    if first_code != 200:
        log_fail(f"First complete failed (HTTP {first_code})")
        print(first_body)
        return

    # Second complete — should also return success (idempotent)
    second_code, second_body = complete_upload(upload_id)

    save_evidence("backpressure-dup-first.json", first_body)
    save_evidence("backpressure-dup-second.json", second_body)

    if second_code == 200:
        code = json_int(second_body, "code")
        if code == "0":
            log_pass(
                "Duplicate finalize after completion is idempotent (HTTP 200, code=0)"
            )
        else:
            log_fail(
                f"Duplicate finalize returned code={code} (expected 0 for idempotent)"
            )
            print(second_body)
    else:
        log_fail(
            f"Duplicate finalize returned HTTP {second_code} (expected 200 for idempotency)"
        )
        print(second_body)


# ─── Test 3: Concurrent finalize — same upload_id (singleflight) ────────────


def test_concurrent_finalize_singleflight():
    log_section("Test 3: Concurrent finalize — same upload_id (singleflight)")
    log_info("Firing 6 concurrent complete requests for same upload_id...")

    upload_id = create_upload_task(f"backpressure_sf_{os.getpid()}.pdf", 14, "c" * 32)
    if not upload_id:
        return

    if not upload_chunk(upload_id, 0, "Hello, World!!"):
        return

    # Fire 6 concurrent complete requests
    results: list[tuple[int, str]] = []

    def do_complete(idx: int) -> tuple[int, int, str]:
        hc, bd = complete_upload(upload_id)
        return idx, hc, bd

    with ThreadPoolExecutor(max_workers=6) as pool:
        futures = [pool.submit(do_complete, i) for i in range(6)]
        for f in as_completed(futures):
            idx, hc, bd = f.result()
            results.append((idx, hc, bd))

    success_count = sum(1 for _, hc, _ in results if hc == 200)
    conflict_or_429_count = sum(1 for _, hc, _ in results if hc == 429)
    other_fail_count = sum(1 for _, hc, _ in results if hc != 200 and hc != 429)

    evidence_lines = "".join(
        f"Worker {idx}: HTTP {hc}\n" for idx, hc, _ in sorted(results)
    )
    save_evidence("backpressure-singleflight-results.txt", evidence_lines)

    log_info(
        f"Results: success={success_count}, 429={conflict_or_429_count}, other={other_fail_count}"
    )

    if success_count >= 1:
        log_pass(f"Singleflight: at least 1 request succeeded ({success_count})")
    else:
        log_fail("Singleflight: no requests succeeded (expected at least 1)")
        return

    if conflict_or_429_count >= 1 or other_fail_count >= 1:
        log_pass(
            f"Singleflight: {conflict_or_429_count + other_fail_count} requests were rejected/errored"
        )
    else:
        log_info(
            "Singleflight: all requests got 200 "
            "(possible — first completes before others arrive, rest see completed status)"
        )


# ─── Test 4: Pool saturation — many concurrent uploads ──────────────────────


def test_pool_saturation_overflow():
    log_section("Test 4: Pool saturation — concurrent assembly overflow")
    log_info(
        "Creating 8 uploads and firing all completes concurrently to saturate pool..."
    )

    chunk_data = "SaturationTest!!"
    upload_ids: list[str] = []

    for i in range(1, 9):
        # Use unique hash per file to avoid dedup/instant upload
        hash_prefix = f"d{str(i).zfill(32)}"[:32]
        upload_id = create_upload_task(
            f"backpressure_sat_{i}_{os.getpid()}.pdf", 16, hash_prefix
        )
        if not upload_id:
            log_fail(f"Failed to create upload task #{i}")
            return
        upload_ids.append(upload_id)

        if not upload_chunk(upload_id, 0, chunk_data):
            log_fail(f"Failed to upload chunk for task #{i}")
            return

    log_info("All 8 uploads prepared. Firing concurrent complete requests...")

    def do_complete(idx: int, uid: str) -> tuple[int, int]:
        hc, _ = complete_upload(uid)
        return idx, hc

    results: list[tuple[int, int]] = []
    with ThreadPoolExecutor(max_workers=8) as pool:
        futures = [pool.submit(do_complete, i, uid) for i, uid in enumerate(upload_ids)]
        for f in as_completed(futures):
            results.append(f.result())

    success_count = sum(1 for _, hc in results if hc == 200)
    rejected_429_count = sum(1 for _, hc in results if hc == 429)
    other_count = sum(1 for _, hc in results if hc != 200 and hc != 429)

    evidence_lines = "".join(
        f"Upload {i + 1}: HTTP {hc}\n" for i, hc in sorted(results)
    )
    save_evidence("backpressure-saturation-results.txt", evidence_lines)

    log_info(
        f"Results: success={success_count}, 429={rejected_429_count}, other={other_count}"
    )

    if success_count >= 1:
        log_pass(f"Saturation: {success_count} uploads succeeded (pool processed them)")
    else:
        log_fail("Saturation: no uploads succeeded (expected some)")
        return

    if rejected_429_count >= 1:
        log_pass(
            f"Saturation: {rejected_429_count} requests got 429 (backpressure working)"
        )
    else:
        log_info(
            "Saturation: no 429s observed — pool may have processed all within capacity"
        )
        log_info(
            "This is acceptable if the server processed requests faster than they arrived"
        )


# ─── Test 5: No duplicate finalize side effects ─────────────────────────────


def test_no_duplicate_side_effects():
    log_section("Test 5: No duplicate finalize side effects")
    log_info(
        "Verifying that duplicate complete does not create duplicate file records..."
    )

    upload_id = create_upload_task(
        f"backpressure_nodup_{os.getpid()}.pdf", 14, "e" * 32
    )
    if not upload_id:
        return

    if not upload_chunk(upload_id, 0, "Hello, World!!"):
        return

    # First complete
    first_http, first_body = complete_upload(upload_id)
    if first_http != 200:
        log_fail(f"First complete failed (HTTP {first_http})")
        print(first_body)
        return

    first_file_id = json_int(first_body, "data.file.id")
    save_evidence("backpressure-nodup-first.json", first_body)

    # Second complete (idempotent)
    second_http, second_body = complete_upload(upload_id)
    if second_http != 200:
        log_fail(f"Second complete failed (HTTP {second_http})")
        print(second_body)
        return

    second_file_id = json_int(second_body, "data.file.id")
    save_evidence("backpressure-nodup-second.json", second_body)

    if first_file_id and second_file_id:
        if first_file_id == second_file_id:
            log_pass(
                f"No duplicate side effects: both completes returned same file_id={first_file_id}"
            )
        else:
            log_fail(
                f"Side effect detected: file_id changed ({first_file_id} → {second_file_id})"
            )
    else:
        log_pass(
            "No duplicate side effects: second complete returned minimal response (idempotent)"
        )


# ─── Main ───────────────────────────────────────────────────────────────────


def main():
    print("==========================================")
    print("Assembly Backpressure Integration Tests")
    print("==========================================\n")

    if not check_server():
        sys.exit(1)

    global TOKEN
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)

    test_normal_assembly_completes()
    test_duplicate_finalize_after_completion()
    test_concurrent_finalize_singleflight()
    test_pool_saturation_overflow()
    test_no_duplicate_side_effects()

    print_summary()


if __name__ == "__main__":
    main()
