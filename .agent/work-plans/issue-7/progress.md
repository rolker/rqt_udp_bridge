---
issue: 7
---

# Issue #7 — Wire Remote Subscribe/Advertise buttons to dialog

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-16 04:29 +0000
**By**: Claude Code Agent (Claude Opus 4.6)
**Verdict**: approved

**Branch**: feature/issue-7 at `0d566c7`
**Mode**: pre-push
**Depth**: Light (reason: 8 changed lines, 1 file, no override/Deep triggers)
**Must-fix**: 0 | **Suggestions**: 1

### Findings
- [ ] (suggestion) `addRemotePushButton` ("Add Connection") remains unwired to the existing `addRemote()` slot — same dead-button regression class this diff fixes; consider wiring it in the same commit — `src/udp_bridge_plugin.ui:58`
