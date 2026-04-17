# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///
# test/integration/harness_smoke.py
# Smoke test: imports and exercises every module in the lib_py harness.

import os
import sys

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
    json_value,
    json_int,
    header_value,
    create_temp_file,
    md5_hash,
    unique_name,
    assert_status,
    assert_json_field,
    assert_json_array_not_empty,
    assert_header_contains,
    assert_json_field_numeric_gt,
    tests_passed,
    tests_failed,
    EVIDENCE_DIR,
)


def main() -> None:
    log_section("Harness Smoke Test")

    log_step("Verify imports work")
    log_info("All modules imported successfully")

    log_step("Test logging functions")
    log_info("info message works")
    log_pass("pass message works")
    import lib_py.common as _c

    _saved = _c.tests_failed
    log_fail("fail message works (expected in smoke test)")
    _c.tests_failed = _saved
    log_step("step message works")

    log_step("Test json_field")
    sample_json = '{"data": {"file_id": "abc123", "results": [{"status": "ok"}]}}'
    val = json_field(sample_json, "data.file_id")
    assert_status("json_field extracts string", int(val == "abc123"), 1)

    val_empty = json_field(sample_json, "data.missing")
    assert_status("json_field returns empty for missing", int(val_empty == ""), 1)

    bool_json = '{"flag": true}'
    assert_status("json_field handles bool", json_field(bool_json, "flag"), "true")

    log_step("Test json_value / json_int aliases")
    assert_status("json_value alias", json_value(sample_json, "data.file_id"), "abc123")
    assert_status("json_int alias", json_int('{"count": 42}', "count"), "42")

    log_step("Test array indexing in json_field")
    arr_json = '{"data": {"results": [{"status": "ok"}, {"status": "err"}]}}'
    assert_status("array index 0", json_field(arr_json, "data.results.0.status"), "ok")
    assert_status("array index 1", json_field(arr_json, "data.results.1.status"), "err")

    log_step("Test fixtures: create_temp_file + md5_hash")
    tmp_path = create_temp_file(256, suffix=".dat")
    assert_status("temp file created", int(os.path.isfile(tmp_path)), 1)
    hash_val = md5_hash(tmp_path)
    assert_status("md5 returns 32-char hex", int(len(hash_val)), 32)
    os.unlink(tmp_path)

    log_step("Test unique_name")
    name = unique_name("smoke")
    assert_status("unique_name has prefix", int(name.startswith("smoke_")), 1)

    log_step("Test assert_json_field")
    body = '{"code": 0, "message": "success", "data": {"id": 42}}'
    assert_json_field("field check", body, "code", "0")
    assert_json_field("nested field", body, "data.id", "42")

    log_step("Test assert_json_array_not_empty")
    arr_body = '{"items": [1, 2, 3]}'
    assert_json_array_not_empty("array not empty", arr_body, "items")

    log_step("Test assert_header_contains")
    headers = {"Content-Type": "application/json", "X-Request-Id": "abc-def"}
    assert_header_contains("header found", headers, "content-type", "application/json")
    assert_header_contains("header case-insensitive", headers, "CONTENT-TYPE", "json")

    log_step("Test assert_json_field_numeric_gt")
    num_body = '{"size": 1024}'
    assert_json_field_numeric_gt("size > 0", num_body, "size", 0)

    log_step("Test save_evidence")
    save_evidence("harness_smoke.txt", "smoke test evidence payload")

    log_section("Smoke Test Complete")
    print_summary()


if __name__ == "__main__":
    main()
