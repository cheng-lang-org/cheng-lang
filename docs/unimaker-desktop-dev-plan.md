# Unimaker Desktop (Cheng) Development Plan

## Scope (production baseline)

The Cheng desktop shell targets a production-ready local UX that mirrors the mobile app domains:

- Social: thread list, DM/voice/video placeholders, activity log.
- Media Publishing: local draft/publish flow, tags, file counts.
- Commerce: catalog list, inventory and order placeholders.
- DEX: pairs list, price/change/liquidity placeholders.
- Desktop layout: sidebar navigation + top status bar + list/detail workspace + right rail.

The baseline is **fully offline** but includes durable identity + state so that real network
integrations can be wired in without reworking UI or data handling.

## Implemented baseline (this repo)

- Persistent peerId (`examples/unimaker_desktop/identity.cheng`), with a roadmap to bind it to a hardware fingerprint.
- Local state store with search/filter, selection, add/delete, and activity log
  (`examples/unimaker_desktop/state.cheng`).
- Desktop UI layout with sidebar navigation, top status bar, list/detail workspace, and right rail
  (`examples/unimaker_desktop/ui.cheng`).
- Event loop and keyboard navigation (`examples/unimaker_desktop/main.cheng`).
- Local P2P presence + DM bus (file-backed peers + message log) with periodic sync ticks.
- Media publishing pipeline states (Local -> Queued -> Publishing -> Published) with visibility/target toggles,
  size/checksum metadata, progress tracking, and failure retry surface.
- Commerce cart + totals (shipping/tax/discount), inventory checks, restock action, and order lifecycle
  transitions (Placed -> Paid -> Shipped -> Delivered).
- Offline DEX price ticks with persisted mini-history for chart summaries, plus order entry with side/type,
  min/max validation, fee accounting, partial fills, and cancel support.

## Durability & safety

- State writes go through a temp file, attempt atomic rename, and keep a `.bak` backup.
- Autosave runs periodically when the state is dirty (default 10s).
- State file carries a version marker for future migrations.
- State file includes a checksum header; corrupted files fall back to `.tmp`/`.bak` or defaults.
- Parser enforces line and item caps to prevent oversized state from exhausting memory.
- Loader records whether state came from primary, temp, or backup files to aid recovery audits.

## Data model

State is stored locally in `~/.unimaker/desktop_state.txt` using a deterministic, line-based format
with sections:

- `[SOCIAL]` threads
- `[PUBLISH]` posts
- `[COMMERCE]` products
- `[DEX]` pairs
- `[LOG]` activity log

Top-level settings include active section, view mode, search query, and selection indices.

## UX rules

- Status updates are non-blocking and displayed in the top bar/right rail.
- Search mode is explicit (`/` to enter, `Esc` to exit).
- All actions (add/delete/detail toggle) are keyboard-accessible.
- Activity log records only significant actions to avoid noise.

## Identity requirements

- Production target: peerId is **hardware-bound and stable**, not just stored locally.
- Current implementation persists a local peerId and will be upgraded to a hardware fingerprint-backed ID.

## Production-ready integration points

The desktop shell is designed to plug into real services without UI redesign:

1. **libp2p networking**: replace placeholder thread/post/product lists with live data streams.
2. **DEX routing**: connect pair cards to real price feeds and order placement.
3. **Media ingestion**: wire file pickers and upload pipelines to the publishing section.
4. **Commerce**: bind inventory/checkout to on-chain settlement and P2P delivery.

## IDE integration (Cheng IDE)

- [ ] Stage 0: in-process embedding (preferred, default path) with module boundary and in-memory bridge; fallback to Stage 1 on failure.
- [ ] Stage 0.1: desktop IDE host lifecycle (start/stop/recover) + in-memory bridge with file mirror for debug.
- [x] Stage 1: external process launch with env bridge (`CHENG_IDE_ROOT`/`CHENG_IDE_ROOTS`/`CHENG_IDE_OPEN`/`CHENG_IDE_RESOURCE_ROOT`) as fallback.
- [x] Stage 2: bundle IDE binary + resources with desktop builds; remove hardcoded paths (`src/tooling/package_unimaker_desktop.sh`).
- [x] Stage 3: workspace/state bridge (open file, recent files, tasks, diagnostics) and UX entry points (`build/ide/desktop_bridge.txt`).
- [x] Stage 3.1: bridge timestamp (epoch ms) + desktop status (Live/Stale) for freshness.

## IDE integration (production-grade closure)

- Source of truth for IDE-side checklist: `doc/cheng-ide-dev-plan.md` (Production-grade closure section).
- [x] Launch + env bridge with workspace and resource roots.
- [x] Bundle IDE binary/resources with desktop packaging.
- [x] Bridge status with Live/Stale freshness.
- [x] Version handshake + schema compatibility for bridge file.
- [x] IDE health watchdog with restart/backoff.
- [x] Desktop-to-IDE actions (open/locate/build/run/diag via command file + ack).
- [x] Crash log capture and user-visible recovery (capture stdout/stderr + `build/ide/recovery/`).
- [x] Update/rollback path for bundled IDE (keep previous bundle and manifest; launcher supports `ide/previous/main_local` and `*_IDE_BIN_FALLBACK`, manifest at `ide/manifest.txt`).
- [x] Workspace/IO allowlist and path traversal guards (path normalize + root check).
- [ ] In-process isolation with fallback to external process (default in-process).
- Bridge spec: `doc/cheng-ide-bridge-schema.md`.
- Command spec: `doc/cheng-ide-command-schema.md`.
- [x] Observability: startup time, bridge freshness, command success rate/ack latency, and memory peak visible in IDE panel (memory sampled via `ps`, gated by `CHENG_IDE_DEBUG_STARTUP`/`CHENG_IDE_PERF`).
- [x] Runbook: troubleshooting steps, log paths, and manual recovery flow documented (`doc/cheng-ide-desktop-runbook.md`).

### Evidence and verification

- Runbook: `doc/cheng-ide-desktop-runbook.md`.
- Artifacts: `build/ide/desktop_bridge.txt`, `build/ide/desktop_command.txt`, `build/ide/desktop_command_ack.txt`, `build/ide/recovery/`, `build/ide/workspace_state.txt`, `ide/manifest.txt`.
- Logs: `~/.unimaker/logs/ide/ide_stdout.log`, `~/.unimaker/logs/ide/ide_stderr.log`.
- Validation script: `scripts/verify_ide_bridge_schema.sh`.
- Observability toggles: `CHENG_IDE_DEBUG_STARTUP=1`, `CHENG_IDE_PERF=1`, `CHENG_IDE_PERF_LOG_MS`, `CHENG_IDE_PERF_SLOW_MS`.
- Memory sampling: `ps -o rss= -p <pid>` (POSIX only; memory line omitted if unavailable).

Suggested budgets (P95, adjust per target hardware):

| Metric | Target |
| --- | --- |
| Cold start to first frame | <= 6 s |
| Large workspace load (10k files) | <= 15 s |
| Bridge freshness | <= 10 s (stale threshold 30 s) |
| Command ack latency | <= 2 s |
| Memory peak | <= 800 MB |

### Acceptance criteria (DoD)

- **Bridge compatibility**: desktop validates `bridge_schema` and `ide_version`, downgrades gracefully on mismatch.
- **Health + recovery**: watchdog detects stalls, triggers restart with backoff, and exposes status in UI.
- **Action loop**: open/locate/build/run/diag commands return status + error codes for retry.
- **Crash handling**: crash logs are persisted; users can relaunch IDE from desktop.
- **Update/rollback**: IDE bundle version syncs with desktop release; rollback is available on failure.
- **Security**: workspace/resource access is allowlisted; path traversal is blocked.
- **Performance**: cold start, memory, and large workspace load budgets are defined and tracked.
- **Isolation**: IDE module faults can be recovered without taking down desktop, or switched to fallback.

## Next milestones

- Replace local P2P bus with real discovery/DM transport.
- Add real file picker + streaming upload for media assets (avoid full file reads).
- Introduce adjustable order size/price entry and real DEX execution plumbing.
- Expand commerce fulfillment status mapping to real delivery hooks and on-chain settlement.
