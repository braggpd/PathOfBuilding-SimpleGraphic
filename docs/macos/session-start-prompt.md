# Session Start Prompt — macOS Port

Copy the block below into Claude Code or a new Cursor chat to bootstrap a session.

---

```
You are picking up work on the macOS Apple Silicon (arm64) port of PathOfBuilding-SimpleGraphic.

## Step 1: Pull latest and orient

Run these commands to get current:

```bash
cd ~/GitHub\ -\ Personal/PathOfBuilding-SimpleGraphic
git fetch origin
git checkout macos/issue-8-sync-smoke-merge
git pull --rebase origin macos/issue-8-sync-smoke-merge
```

## Step 2: Read project context

Read these files (in this order) to understand the project, architecture, conventions, and current state:

1. `CLAUDE.md` — project overview, status snapshot, key decisions, build commands
2. `MACOS_PORT.md` — full plan, phase tracking, engine fixes table, root causes, session workflow
3. `.cursor/rules/macos-port.mdc` — always-on rules for the port

## Step 3: Review recent progress

Run:
```bash
git log --oneline -20
git log -1 --format=fuller
```

Check which GitHub issues are open vs closed:
```bash
gh issue list -l macos-port --state all
```

## Step 3b: Current blocker (Data.lua PLoad tail / `data.misc`)

Read **`docs/macos/claude-handoff-data-misc-tail.md`** and use the **Session prompt** block there.

Active bug: root tail `lua_pcall` for `Data.lua` SIGILLs at line 171 (`data.misc = {`). Misc inline + preamble-only tail OK. Engine changes likely **uncommitted** on `macos/issue-8-sync-smoke-merge`.

Patrick may pivot away from slice-and-patch — confirm approach before implementing.

## Step 3c: Older blocker (nested LoadModule — partially addressed)

If still relevant: **`docs/macos/claude-handoff-plm-nested-load.md`**

## Step 4: Evaluate and propose

Based on your reading, produce a brief status report:

1. **What's done** — summarize completed phases/milestones
2. **What's open** — list open issues and known remaining work
3. **Recommended next steps** — prioritized list of what to tackle next, with rationale
4. **Blockers or risks** — anything that might slow progress

Format as a concise table + bullet list. Ask me which direction I'd like to go before starting any work.
```
