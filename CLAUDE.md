## Worktree Parallel Planning

When the user asks to open worktrees for parallel development in this project, answer in a concise, copy-ready format. For each worktree, provide:

```text
wt/<short-name>

根据 <source/phase>，<one-line objective>。
目标：
- <specific outcome 1>
- <specific outcome 2>
- <specific outcome 3>
保持 <important invariants / API shape>。
补充或更新测试。
建议拆成 <N> 个 commits。
```

Keep the plan brief: list only the worktree name, source TODO/spec phase, concrete goals, invariants, test expectation, and suggested commit count. Avoid long tables unless the user specifically asks for one.
