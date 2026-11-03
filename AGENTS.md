# Contributor instructions

- Keep the application a small, native Linux Qt Widgets utility. Do not add Electron, a browser engine, a daemon, telemetry, or network listeners.
- Default documentation language is English.
- Never infer thread liveness from a recent timestamp or a database edge. Label runtime evidence and unknown values explicitly.
- Never fabricate per-session memory or independent termination when sessions share a process.
- Keep kill operations identity-bound, owned-user-only, confirmation-gated, and pidfd-based. Never use pkill, killall, shell commands, or process-group signals.
- Do not test termination on the user's existing Codex processes. Use the dedicated test fixture only.
- Never commit real Codex state, prompts, credentials, environment dumps, runtime logs, or screenshots containing real session information.
- Validate changes with a Release build and `ctest --test-dir build --output-on-failure`. UI tests use the offscreen platform.
