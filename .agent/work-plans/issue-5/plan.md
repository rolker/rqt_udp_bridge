# Plan: UX redesign — PR 1A (layout refactor + filters)

## Issue

https://github.com/rolker/rqt_udp_bridge/issues/5

Scope of **this plan is PR 1A only**. PR 1B (context menus + dialog split) and
Phase 2 (destructive actions, gated on
[rolker/udp_bridge#14](https://github.com/rolker/udp_bridge/issues/14)) are
separate PRs on the same branch family, planned later.

## Context

`udp_bridge_plugin.cpp` (351 lines) + `udp_bridge_plugin.ui` (212) currently
stack four browse trees (`localTopicsTreeView`, `remotesTreeView`,
`remoteTopicsTreeView`, `remoteRemotesTreeView`) in one vertical `splitter`
with a hard-coded `#3030FF` handle stylesheet. No filters; per-connection
detail is a single concatenated string pushed to `selectedRemotedetailsLabel`
via `BridgeNode::remoteDetailsUpdated(remote, connection, details)` (built at
`bridge_node.cpp:362-364`). Models are `QStandardItemModel`s owned by
`BridgeNode`, swapped onto the views with `setModel()` on node/remote change;
per-remote models are created on demand (`remoteTopicsModel(remote)`).
Staleness greying lives in the model via `TimestampRole` (`bridge_node.h:58`).

PR 1A replaces the splitter with a `QTabWidget` + persistent active-remote
header, adds a key/value detail table, and wraps each tree in a filtering proxy
— all UI-side, wrapping services that already exist.

## Approach

1. **`.ui` rebuild** — Replace the `splitter` with the layout from the issue's
   sketch: toolbar (unchanged for 1A) → header `QFrame`
   (`activeRemoteComboBox` + `headlineStatsLabel`) → `QTabWidget` with tabs
   `Local Topics` · `Remotes` · `Topics @ <remote>` · `Peers @ <remote>`.
   Per-remote tabs are `QStackedWidget`: empty-state placeholder vs content.
   Remotes tab is a horizontal `QSplitter`: tree + `selectedRemoteDetailsTable`
   (`QTableView`). Each tab gets a filter row (line-edit · Hide idle · Show
   failing · columns ▾). The `#3030FF` splitter handle stylesheet is removed
   from the `ui_.splitter->setStyleSheet(...)` call at `udp_bridge_plugin.cpp:28`
   (it is set in the `.cpp`, not the `.ui`), along with the old splitter.
2. **Structured detail accessor in `BridgeNode`** *(settled with Roland)* —
   in the same loop at `bridge_node.cpp:362-364`, keep the tooltip string
   (still used for row hover) but additionally emit structured pairs. Change
   the signal to `remoteDetailsUpdated(QString remote, QString connection,
   QList<QPair<QString,QString>> fields)`. A small `QStandardItemModel`
   (key|value) in the plugin rebuilds on that signal and backs
   `selectedRemoteDetailsTable`. `selectedRemotedetailsLabel` and the string
   form of the signal are removed. No re-parsing in the view. The **remote-remote
   (Peers @) detail pane converts too** (`selectedRemoteRemoteDetailsLabel` →
   key/value table): `remoteDetailsUpdated` is consumed by both
   `updateCurrentRemoteDetails` and `updateCurrentRemoteRemoteDetails`
   (`udp_bridge_plugin.cpp:309`), so the signature change forces reworking the
   remote-remote handler regardless — keeping it a label would leave a
   half-migrated signal. Convert both for one clean signal.
3. **Stable per-tree `QSortFilterProxyModel`** — one proxy per tree, created
   once in `initPlugin` and never rebuilt. On node/remote change call
   `proxy->setSourceModel(newModel)` (not `view->setModel(newProxy)`), so
   filter text + hidden-column state survive remote switches. Set
   `setRecursiveFilteringEnabled(true)` so a surviving leaf keeps its parent
   row visible (parent rows — remote/topic names — carry no stats).
4. **`mapToSource()` in the tree-walk helpers** — `getRemoteConnection()` and
   `getTopicRemoteConnection()` (lines 255-290) walk `parent()` chains on the
   source model. With proxies in place, `currentChanged` delivers proxy
   indices, so each helper must `mapToSource()` first. This is the highest-risk
   change in 1A — covered by tests in step 7.
5. **Filter row behavior** — line-edit → `setFilterFixedString` on column 0;
   `Hide idle` → custom `filterAcceptsRow` using `TimestampRole` staleness;
   `Show only failing` → rows with nonzero `failed`+`dropped`; columns ▾ →
   `QMenu` toggling `view->setColumnHidden`. Combine idle/failing predicates in
   a `QSortFilterProxyModel` subclass.
6. **Active-remote combo as single source of truth** — combo selection drives
   the per-remote `QStackedWidget` content and the `Topics @`/`Peers @` tab
   labels; the Remotes-tree selection updates the combo. Use `blockSignals`
   discipline so tree→combo→tabs doesn't loop. Folds in the existing
   `currentRemoteChanged` model-swap logic (lines 293-313).
7. **Tests** — add a `launch_testing`/`pytest` smoke (plugin loads, no crash)
   if feasible in CI, **plus** targeted GTest on the proxy index mapping:
   build a known source tree, wrap in the proxy, assert
   `getRemoteConnection`/`getTopicRemoteConnection` return the right
   (remote, connection) / (topic, remote, connection) tuples through the proxy
   (factor the tree-walk into a free function taking a model+index so it is
   testable without a live ROS graph).
8. **Settings persistence** — `saveSettings`/`restoreSettings` gain
   `active_tab`, `active_remote`, per-tab `filter_text`/`hide_idle`/
   `show_failing`, and hidden columns. Old settings (just `node`) must load
   without warnings (verify with the existing `node`-only round-trip).

## Files to Change

| File | Change |
|------|--------|
| `src/udp_bridge_plugin.ui` | Splitter → tabs + header strip + per-tab filter rows + detail `QTableView` (both remote and remote-remote) |
| `src/udp_bridge_plugin.cpp` | Proxy wiring, `mapToSource` in helpers, combo SSOT, filter slots, detail-table models, settings keys, remove `#3030FF` `setStyleSheet` (l.28) |
| `include/rqt_udp_bridge/udp_bridge_plugin.h` | Proxy members, detail model, filter-state fields; new slots |
| `src/bridge_node.cpp` | Emit structured detail pairs alongside tooltip string (l.362-365) |
| `include/rqt_udp_bridge/bridge_node.h` | Change `remoteDetailsUpdated` signature to carry key/value pairs |
| new: `src/topic_remote_filter_proxy.{h,cpp}` | `QSortFilterProxyModel` subclass (idle/failing predicates) |
| new: `test/test_index_mapping.cpp` + `CMakeLists.txt` | GTest for proxy index→source tree-walk |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Filters + per-row detail surface bridge state at the point of use; no behavior hidden. |
| A change includes its consequences | `remoteDetailsUpdated` signature change touches every caller (only the plugin); `udp_bridge` README screenshot will need updating post-Phase-1 (noted in PR). |
| Only what's needed | Three filters + column toggle is the issue's agreed set; no sparklines/unit-toggle (explicitly deferred). |
| Improve incrementally | 1A is the layout/proxy foundation; context menus (1B) and destructive actions (Phase 2) land separately. |
| Test what breaks | The proxy index-mapping regression is the real hazard and gets dedicated GTest; rqt Qt+ROS lifecycle limits the rest to smoke. |
| Workspace vs. project separation | All changes are project-side UI; nothing workspace-portable. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0008 — Follow ROS 2 Conventions | Marginally | Preserve `plugin.xml`, `pluginlib` export, `rqt_gui_cpp::Plugin` lifecycle; no new packages/interfaces. |
| 0002 — Worktree isolation | Yes | Landing via `feature/issue-5` worktree + draft PR. |
| 0013 — progress.md vocabulary | Yes | `## Plan Authored` entry written for this plan. |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `remoteDetailsUpdated` signature | `updateCurrentRemoteDetails` + `updateCurrentRemoteRemoteDetails` callers | Yes |
| `selectedRemotedetailsLabel` removed | `selectedRemoteRemoteDetailsLabel` → also converted to key/value table (signal change forces it) | Yes |
| `.ui` object names | any `instance_settings` keys referencing them | Yes (settings step) |
| plugin layout | `udp_bridge` README screenshot | No — PR note, follow-up |

## Open Questions

*(Both resolved during review-plan — [PR #6](https://github.com/rolker/rqt_udp_bridge/pull/6) `## Plan Review`, `76cb766`.)*

- ~~Convert the remote-remote (Peers @) detail pane in 1A, or defer?~~
  **Resolved: convert both now.** `remoteDetailsUpdated` feeds both the remote
  and remote-remote handlers, so the signature change forces touching
  `updateCurrentRemoteRemoteDetails` regardless — "keep it a label" would only
  leave a half-migrated signal. Folded into Approach step 2.
- ~~`launch_testing` smoke vs. GTest-only coverage?~~ **Resolved: timeboxed to
  implementation.** The committed GTest proxy index-mapping test is the
  guaranteed 1A coverage; add a `launch_testing` load-smoke only if it drops in
  cheaply on `jazzy` CI (no existing test dir), else skip without blocking.

## Estimated Scope

Single PR (PR 1A). Sizable view-layer rewrite but coherent; the proxy/detail
plumbing must land together. PR 1B and Phase 2 follow on the same issue.
