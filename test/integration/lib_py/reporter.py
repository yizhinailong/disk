# test/integration/lib_py/reporter.py
# Markdown report generator for integration test results.

from __future__ import annotations

import os


class ReportGenerator:
    """Generate a Markdown report from integration test results.

    Args:
        title: Report title (appears as h1).
        output_path: Destination file path for the Markdown report.
    """

    def __init__(self, title: str, output_path: str) -> None:
        self.title = title
        self.output_path = output_path
        self.meta: dict = {}
        self.sections: list[dict] = []

    def add_meta(self, env_info: dict) -> None:
        """Store environment metadata.

        Args:
            env_info: Dict with keys like backend_url, timestamp, accounts.
        """
        self.meta = env_info

    def add_section(
        self,
        domain: str,
        endpoint: str,
        method: str,
        tests: list[dict],
    ) -> None:
        """Add a test result section.

        Args:
            domain: Domain name (e.g., "认证", "文件").
            endpoint: API endpoint path (e.g., "/api/auth/login").
            method: HTTP method (e.g., "POST").
            tests: List of test dicts, each with keys:
                   name (str), passed (bool), expected (str),
                   actual (str), detail (str).
        """
        self.sections.append(
            {
                "domain": domain,
                "endpoint": endpoint,
                "method": method,
                "tests": tests,
            }
        )

    def generate(self) -> str:
        """Generate the full Markdown report and write to output_path.

        Returns:
            The complete Markdown content as a string.
        """
        total = sum(len(s["tests"]) for s in self.sections)
        passed = sum(
            sum(1 for t in s["tests"] if t["passed"])
            for s in self.sections
        )
        failed = total - passed
        rate = int(passed / total * 100) if total > 0 else 0

        lines: list[str] = []
        lines.append(f"# {self.title}")
        lines.append("")

        # 测试环境
        lines.append("## 测试环境")
        backend_url = self.meta.get("backend_url", "N/A")
        timestamp = self.meta.get("timestamp", "N/A")
        accounts = self.meta.get("accounts", "N/A")
        lines.append(f"- 后端地址: {backend_url}")
        lines.append(f"- 测试时间: {timestamp}")
        lines.append(f"- 测试账户: {accounts}")
        lines.append("")

        # 测试总览
        lines.append("## 测试总览")
        lines.append("| 指标 | 值 |")
        lines.append("|------|-----|")
        lines.append(f"| 总测试数 | {total} |")
        lines.append(f"| 通过 ✅ | {passed} |")
        lines.append(f"| 失败 ❌ | {failed} |")
        lines.append(f"| 通过率 | {rate}% |")
        lines.append("")

        # 按域分类结果 — group sections by domain
        lines.append("## 按域分类结果")
        domain_order: list[str] = []
        domain_sections: dict[str, list[dict]] = {}
        for section in self.sections:
            domain = section["domain"]
            if domain not in domain_sections:
                domain_order.append(domain)
                domain_sections[domain] = []
            domain_sections[domain].append(section)

        for domain in domain_order:
            sections = domain_sections[domain]
            endpoint_count = len(sections)
            lines.append(f"### {domain} 域 ({endpoint_count} 端点)")
            lines.append("")
            lines.append("| 端点 | 方法 | 测试结果 |")
            lines.append("|------|------|---------|")
            for section in sections:
                endpoint = section["endpoint"]
                method = section["method"]
                tests = section["tests"]
                n_passed = sum(1 for t in tests if t["passed"])
                n_total = len(tests)
                status = f"✅ {n_passed}/{n_total}" if n_passed == n_total else f"❌ {n_passed}/{n_total}"
                lines.append(f"| {endpoint} | {method} | {status} |")
            lines.append("")

        # 失败详情
        lines.append("## 失败详情")
        has_failures = any(not t["passed"] for s in self.sections for t in s["tests"])
        if not has_failures:
            lines.append("*无失败测试*")
        else:
            for section in self.sections:
                for test in section["tests"]:
                    if not test["passed"]:
                        lines.append(f"### [{test['name']}]")
                        lines.append(f"- **端点**: {section['method']} {section['endpoint']}")
                        lines.append(f"- **期望**: {test['expected']}")
                        lines.append(f"- **实际**: {test['actual']}")
                        lines.append(f"- **详情**: {test['detail']}")
                        lines.append("")
        lines.append("")

        # 附录：测试证据
        lines.append("## 附录：测试证据")
        evidence_dir = ".sisyphus/evidence"
        if os.path.isdir(evidence_dir):
            files = sorted(os.listdir(evidence_dir))
            if files:
                for fname in files:
                    lines.append(f"- {fname}")
            else:
                lines.append("- 无证据文件")
        else:
            lines.append("- 无证据目录")
        lines.append("")

        content = "\n".join(lines)

        # Auto-create parent directory
        parent = os.path.dirname(self.output_path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(self.output_path, "w", encoding="utf-8") as f:
            f.write(content)

        return content