# Design and safety

## Architecture

The program is one native Qt Widgets process with short-lived worker jobs. There is no server, service installation, browser engine, or persistent application database.

| Component | Inputs | Responsibility |
| --- | --- | --- |
| `src/proc.cpp` | Current-user `/proc` entries | Classify hosts, build non-overlapping process trees, compute CPU/RSS, read optional memory fields |
| `src/sessions.cpp` | Open rollout FDs, two allowlisted environment keys, read-only Codex SQLite metadata | Associate logical sessions and annotate observed vs. related-only evidence |
| `src/window.cpp` | Immutable sampling results | Present a sortable host/session/process tree, bounded memory chart, inspector, confirmations, and action results |
| `src/terminate.cpp` | Identity-bound, confirmed host/target lists | Revalidate ownership and ancestry, acquire pidfds, request exit and escalate only confirmed processes |
| `src/main.cpp` | CLI options | Start the window, read-only snapshot, or non-destructive demo |

The window uses semantic light/dark colors and lightweight painted badges/traces; see [appearance](appearance.md) for the palette, contrast checks, and previews. It keeps native widgets and adds no rendering dependency or animation timer.

The session hierarchy and the OS process hierarchy are different views. Logical sessions never contribute additional RSS. Descendants are assigned to the topmost discovered Codex host, preventing nested native/npm Codex processes from being counted twice in the global tree total.

## Sampling budget

- One non-overlapping process scan worker, normally every two seconds; ten seconds when minimized.
- One asynchronous memory-detail reader, only for the inspected identity, with a five-second cache.
- A separate termination worker keeps the window responsive during the grace period.
- Process metadata is sampled for the current user; full `smaps` files are never scanned.
- Rollout reads: at most 256 KiB for the first metadata line and 64 KiB for the tail per open session, per sample.
- No recursive enumeration of historical sessions. An unknown session remains unknown.
- Only thread metadata columns are selected from SQLite. Missing optional columns are tolerated. A database read has a short SQLite busy timeout.
- Related database edges are capped at 200 per directly observed thread, with a visible warning when capped.
- The chart retains 120 samples; the in-window action log retains 300 text blocks. Neither is written to disk.

The process scan itself is not atomic: processes can start, exit, or reparent while `/proc` is being read. Displayed values are a best-effort sample. Safety checks are repeated independently before termination.

## Discovery evidence

Strongest available evidence is used without claiming more than it proves:

| Evidence | What it proves | What it does not prove |
| --- | --- | --- |
| Owned Codex executable + process identity | A matching OS process exists at sample time | Authenticity against a malicious same-user executable |
| Open rollout FD under that host's Codex session directory | The process currently has that session file open | The model turn is actively working |
| `CODEX_THREAD_ID` on a matching Codex process | Explicit thread identity supplied in the process environment | Separate process memory for each in-process subagent |
| Related `thread_spawn_edges` entry | A recorded parent/child relationship | That the child is still loaded or running |
| Last rollout event / database token count | A historical recorded value | Current activity, live tokens, or billing |

Database paths are selected from numbered `state_*.sqlite` files. Known fields are projected dynamically so optional schema changes do not break process monitoring. Codex's storage is not a stable public API: runtime compatibility remains best effort.

Only `CODEX_HOME` and `CODEX_THREAD_ID` values are extracted from process environments. Raw environment data and command arguments are never displayed or logged. Bounded rollout excerpts are parsed only for metadata and recognized status fields; prompt/tool bodies are not retained in snapshots or displayed.

## Termination contract

1. The user chooses **host checkboxes**, not a logical-session row or arbitrary PID.
2. The current sampled identities and process list are captured in a fixed plan. Protected hosts are excluded.
3. The confirmation describes host/process counts, shared-session effects, unsaved-work risk, and whether hidden checked/all hosts are included. Expanded details enumerate hosts and known sessions. Escape/Cancel performs no operation.
4. After confirmation, the worker scans again. Every surviving root must still have the same PID, start ticks, UID, and executable, and must still classify as an owned Codex host.
5. A target must still belong to one of the permitted, unprotected roots, and its identity must match the confirmed plan.
6. A pidfd is opened, then identity is checked again. **There is no numeric `kill(pid)` fallback**, process-group signal, `pkill`, or `killall`.
7. Confirmed targets receive `SIGTERM`; targets still alive after three seconds receive `SIGKILL` through the same pidfds. Exit is observed separately, and per-process failures are reported.
8. New descendants are not added after confirmation. Detached/reparented processes may be skipped, with a warning where still visible. Shutdown is not claimed to be an atomic or transitive guarantee.

A pidfd pins the process identity for signalling even if its numeric PID is later reused. It does not make a process tree atomic, prevent a process from forking, guarantee delivery under restrictive security policies, or guarantee immediate exit from uninterruptible kernel sleep.

The GUI refuses to close while a confirmed stop worker is active. It protects any Codex host whose sampled process tree contains the monitor or one of the monitor's ancestors, so launching the monitor from within a Codex command cannot quietly kill that surrounding session.

## Why not individual in-process subagent kills?

OS signals target processes, not Codex's logical thread IDs. Killing one shared host affects all sessions hosted there. An isolated subagent-cancellation feature would require a supported, correctly scoped Codex control API and a separate safety contract; this release deliberately does not attach to or mutate a running app-server protocol.

## References

- [Linux proc_pid_stat(5)](https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html)
- [Linux proc_pid_smaps(5)](https://man7.org/linux/man-pages/man5/proc_pid_smaps.5.html)
- [Linux pidfd_open(2)](https://man7.org/linux/man-pages/man2/pidfd_open.2.html)
- [Linux pidfd_send_signal(2)](https://man7.org/linux/man-pages/man2/pidfd_send_signal.2.html)
- [Qt 5 documentation](https://doc.qt.io/archives/qt-5.15/)
- [Codex source](https://github.com/openai/codex): session metadata and state formats are implementation details, not a promised integration interface.
