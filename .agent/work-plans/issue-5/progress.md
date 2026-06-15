---
issue: 5
---

# Issue #5 — UX redesign: tab layout, filters, context-menu actions

## Plan Authored
**Status**: complete
**When**: 2026-06-14 23:34 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-5/plan.md` at `ae2371d`
**PR**: https://github.com/rolker/rqt_udp_bridge/pull/6 (`[PLAN]` prefix)
**Phases**: PR 1A planned here; 1B + Phase 2 are separate PRs on this issue

### Open questions
- [x] Convert the remote-remote (Peers @) detail pane to a key/value table in 1A too? → **Yes, convert both** (signal-signature change forces touching the remote-remote handler; resolved in `## Plan Review`).
- [x] Is `launch_testing` smoke feasible in this repo's CI? → **Timeboxed to implementation**; GTest proxy index-mapping is the guaranteed coverage either way.

## Plan Review
**Status**: complete
**When**: 2026-06-15 03:52 +0000
**By**: Claude Code Agent (Claude Opus 4.8) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-5/plan.md` at `ae2371d`
**PR**: https://github.com/rolker/rqt_udp_bridge/pull/6 (`[PLAN]` prefix)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) `#3030FF` removal mis-attributed to the `.ui` file — the stylesheet is set in `ui_.splitter->setStyleSheet(...)` at `udp_bridge_plugin.cpp:28`, not in the `.ui`. The `.cpp` is already in scope so it gets removed, but fix the file attribution. — `plan.md:84`
- [ ] (suggestion) The Peers @ "table vs label" open question is partly forced: `remoteDetailsUpdated` is emitted to BOTH `updateCurrentRemoteDetails` and `updateCurrentRemoteRemoteDetails` (the latter wired at `udp_bridge_plugin.cpp:309`). Changing the signal signature requires reworking the remote-remote handler regardless, so "keep it a label this PR" is not a no-touch option. Recommend resolving to the plan's own lean (convert both) to avoid a half-migrated signal. — `plan.md:116`
- [ ] (suggestion) Timebox the `launch_testing`-feasibility open question during implementation rather than leaving it open; the committed GTest index-mapping test is the guaranteed 1A coverage either way. — `plan.md:127`
- [ ] (suggestion) Issue alignment and any `review-issue` comments could not be independently verified — no `gh` auth in this review environment. File targeting and line refs were verified directly against source instead. — `plan.md:3`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-15 01:41 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-5 at `1ec0b67`
**Mode**: pre-push
**Depth**: Deep (reason: ~1150 LOC C++ Qt model/proxy/signal/lifecycle rewrite)
**Must-fix**: 0 | **Suggestions**: 1 (acted on)

### Findings
- [x] (acted) Register `DetailFields` metatype for the `remoteDetailsUpdated` signal — defensive future-proofing for any queued connection (current emit is same-thread/direct). Fixed in `1ec0b67`. — `bridge_node.h` / `bridge_node.cpp`
- [ ] (false positive) Dangling per-remote source models on node change — handled: `onNodeChanged` nulls proxy sources via `setActiveRemote("")` before `setTopicsPrefix` deletes children; all GUI-thread serial. — `udp_bridge_plugin.cpp`
- [ ] (false positive) `filterAcceptsRow` nullptr source deref — guarded by `if(!model) return true` before any source access. — `topic_remote_filter_proxy.cpp`
- [ ] (false positive) Column-menu-built flag / failure-column bounds — topics model has 7 columns (cols 4/5 valid); columns structurally identical across remotes; out-of-range index reads return empty (safe). — `udp_bridge_plugin.cpp`, `topic_remote_filter_proxy.cpp`
- [ ] (dropped) 186 cpplint whitespace/line-length nits — repo has no `ament_lint` gate; compact style matches the existing file. — repo-wide
