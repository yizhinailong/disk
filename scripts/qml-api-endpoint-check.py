#!/usr/bin/env python3
"""
Script to compare documented API endpoints in QML documentation
against implemented endpoints in C++ API headers.

Usage:
    python scripts/qml-api-endpoint-check.py

Output:
    JSON report saved to .sisyphus/evidence/task-7-endpoint-diff.json
"""

import json
import re
from pathlib import Path
from typing import Dict, List, Set, Tuple


def extract_doc_endpoints(doc_path: Path) -> Set[Tuple[str, str]]:
    """
    Extract endpoint patterns from QML API documentation.

    Returns:
        Set of (method, path) tuples, e.g., {('POST', '/api/auth/register')}
    """
    content = doc_path.read_text()
    endpoints = set()

    # Pattern to match "METHOD /api/path" in the documentation
    # This matches lines like "POST | `/api/auth/register` | ..."
    pattern = r"(GET|POST|PUT|PATCH|DELETE)\s+\|\s+`(/api/[^`]+)`"

    for match in re.finditer(pattern, content):
        method = match.group(1)
        path = match.group(2)
        endpoints.add((method, path))

    return endpoints


def extract_code_endpoints(api_dir: Path) -> Set[Tuple[str, str]]:
    """
    Extract endpoint patterns from C++ API header files.

    Returns:
        Set of (method, path) tuples, e.g., {('POST', '/api/auth/register')}
    """
    endpoints = set()

    # Find all API header files
    api_files = sorted(api_dir.glob("*Api.hpp"))

    # Skip ApiClient.hpp as it doesn't contain endpoint definitions
    api_files = [f for f in api_files if f.name != "ApiClient.hpp"]

    for header_file in api_files:
        content = header_file.read_text()

        # Pattern to match "METHOD /api/path" in comments
        # This matches lines like " * @brief POST /api/auth/register — ..."
        pattern = r"(GET|POST|PUT|PATCH|DELETE)\s+(/api/[^\s]+)"

        for match in re.finditer(pattern, content):
            method = match.group(1)
            path = match.group(2)
            endpoints.add((method, path))

    return endpoints


def normalize_endpoint(method: str, path: str) -> Tuple[str, str]:
    """
    Normalize endpoint for comparison.

    - Standardize method to uppercase
    - Remove trailing slashes from path
    - Ensure path starts with /

    Returns:
        Normalized (method, path) tuple
    """
    method = method.upper()
    path = path.rstrip("/")
    if not path.startswith("/"):
        path = "/" + path
    return (method, path)


def compare_endpoints(
    doc_endpoints: Set[Tuple[str, str]], code_endpoints: Set[Tuple[str, str]]
) -> Dict:
    """
    Compare documented and implemented endpoints.

    Returns:
        Dictionary containing comparison results
    """
    # Normalize all endpoints
    doc_normalized = {normalize_endpoint(m, p) for m, p in doc_endpoints}
    code_normalized = {normalize_endpoint(m, p) for m, p in code_endpoints}

    # Find differences
    missing_in_code = doc_normalized - code_normalized
    extra_in_code = code_normalized - doc_normalized

    return {
        "total_doc": len(doc_normalized),
        "total_code": len(code_normalized),
        "missing_in_code": sorted([f"{m} {p}" for m, p in missing_in_code]),
        "extra_in_code": sorted([f"{m} {p}" for m, p in extra_in_code]),
        "documented_endpoints": sorted([f"{m} {p}" for m, p in doc_normalized]),
        "implemented_endpoints": sorted([f"{m} {p}" for m, p in code_normalized]),
    }


def main():
    # Define paths
    project_root = Path(__file__).parent.parent
    doc_path = project_root / "docs" / "ui" / "qml" / "04-API客户端设计.md"
    api_dir = project_root / "ui" / "diskqml" / "src" / "api"
    evidence_dir = project_root / ".sisyphus" / "evidence"
    output_path = evidence_dir / "task-7-endpoint-diff.json"

    # Create evidence directory if it doesn't exist
    evidence_dir.mkdir(parents=True, exist_ok=True)

    # Extract endpoints
    print(f"Extracting endpoints from documentation: {doc_path}")
    doc_endpoints = extract_doc_endpoints(doc_path)

    print(f"Extracting endpoints from code: {api_dir}")
    code_endpoints = extract_code_endpoints(api_dir)

    # Compare
    print("Comparing endpoints...")
    result = compare_endpoints(doc_endpoints, code_endpoints)

    # Save to JSON
    print(f"Saving results to: {output_path}")
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, ensure_ascii=False)

    # Print summary
    print("\n=== SUMMARY ===")
    print(f"Total documented endpoints: {result['total_doc']}")
    print(f"Total implemented endpoints: {result['total_code']}")
    print(f"Missing in code: {len(result['missing_in_code'])}")
    print(f"Extra in code: {len(result['extra_in_code'])}")

    if result["missing_in_code"]:
        print("\nMissing in code:")
        for endpoint in result["missing_in_code"]:
            print(f"  - {endpoint}")

    if result["extra_in_code"]:
        print("\nExtra in code:")
        for endpoint in result["extra_in_code"]:
            print(f"  - {endpoint}")

    print(f"\nFull report saved to: {output_path}")


if __name__ == "__main__":
    main()
