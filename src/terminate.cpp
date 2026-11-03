#include "core.h"
#include <QElapsedTimer>
#include <QSet>
#include <QThread>
#include <poll.h>
#include <sys/syscall.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace scope {
StopPlan makeStopPlan(const Snapshot &s, const QVector<int> &roots) {
    StopPlan plan;
    QSet<int> seen;
    for (const auto &g : s.groups) {
        if (!roots.contains(g.root) || g.protectedRoot) continue;
        const auto root = s.processes.value(g.root);
        if (!root.codex || root.id.uid != getuid()) continue;
        plan.roots.append(root.id);
        for (const int pid : g.pids) {
            if (seen.contains(pid)) continue;
            const auto p = s.processes.value(pid);
            if (p.id.uid != getuid() || p.state == 'Z') continue;
            seen.insert(pid);
            plan.targets.append(p.id);
        }
        plan.affected << QString("PID %1 — %2 — %3 processes, %4 RSS").arg(g.root).arg(root.cwd).arg(g.pids.size()).arg(bytes(g.rss));
        for (const auto &session : g.sessions)
            plan.affected << QString("  %1: %2 [%3]").arg(session.id, session.title.left(120),
                                session.observed ? "observed on shared host" : "related record; liveness unknown");
    }
    return plan;
}
static int openPid(int pid) {
#ifdef SYS_pidfd_open
    return int(syscall(SYS_pidfd_open, pid, 0));
#else
    Q_UNUSED(pid)
    errno = ENOSYS;
    return -1;
#endif
}
static int sendSignal(int fd, int sig) {
#ifdef SYS_pidfd_send_signal
    return int(syscall(SYS_pidfd_send_signal, fd, sig, nullptr, 0));
#else
    Q_UNUSED(fd) Q_UNUSED(sig)
    errno = ENOSYS;
    return -1;
#endif
}
static bool exited(int fd) {
    pollfd p{fd, POLLIN, 0};
    return poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP));
}
StopResult executeStop(const StopPlan &plan, int graceMs) {
    StopResult result;
    if (plan.roots.isEmpty()) return result;
    // Re-scan/reclassify roots and ancestry before accepting ANY target. No numeric kill fallback.
    const auto fresh = scanProcesses();
    QVector<int> rootPids;
    for (const auto &id : plan.roots) {
        const auto p = fresh.processes.constFind(id.pid);
        if (p == fresh.processes.cend()) continue;
        if (!(p->id == id) || !p->codex || p->id.uid != getuid()) {
            result.errors << QString("PID %1 changed identity or is not an owned Codex host; nothing was signalled.").arg(id.pid);
            return result;
        }
        rootPids << id.pid;
    }
    const auto permitted = makeStopPlan(fresh, rootPids);
    QHash<int, Identity> allowed;
    for (const auto &id : permitted.targets) allowed.insert(id.pid, id);
    // Root already exited: descendants are only accepted while still verifiably in a selected tree.
    struct Handle { Identity id; int fd; bool forced = false; bool sent = false; };
    QVector<Handle> handles;
    for (const auto &id : plan.targets) {
        if (!allowed.contains(id.pid) || !(allowed[id.pid] == id)) {
            if (readProcess(id.pid)) result.errors << QString("PID %1 no longer belongs to the confirmed process tree; skipped.").arg(id.pid);
            continue;
        }
        const int fd = openPid(id.pid);
        if (fd < 0) {
            if (errno != ESRCH) result.errors << QString("PID %1: pidfd_open: %2").arg(id.pid).arg(QString::fromLocal8Bit(strerror(errno)));
            continue;
        }
        const auto again = readProcess(id.pid);
        if (!again || !(again->id == id)) {
            close(fd);
            if (again) result.errors << QString("PID %1 changed while opening pidfd; skipped.").arg(id.pid);
            continue;
        }
        handles.append({id, fd, false, false});
    }
    // Target set is frozen by confirmation. Newly spawned processes are never silently added.
    for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
        if (sendSignal(it->fd, SIGTERM) == 0) it->sent = true;
        else if (errno != ESRCH) result.errors << QString("PID %1: SIGTERM: %2").arg(it->id.pid).arg(QString::fromLocal8Bit(strerror(errno)));
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < qMax(0, graceMs)) {
        bool alive = false;
        for (const auto &h : handles) if (h.sent && !exited(h.fd)) { alive = true; break; }
        if (!alive) break;
        QThread::msleep(25);
    }
    for (auto &h : handles) {
        if (h.sent && !exited(h.fd)) {
            if (sendSignal(h.fd, SIGKILL) == 0) h.forced = true;
            else if (errno != ESRCH) result.errors << QString("PID %1: SIGKILL: %2").arg(h.id.pid).arg(QString::fromLocal8Bit(strerror(errno)));
        }
    }
    timer.restart();
    while (timer.elapsed() < 1000) {
        bool alive = false;
        for (const auto &h : handles) if (h.sent && !exited(h.fd)) { alive = true; break; }
        if (!alive) break;
        QThread::msleep(25);
    }
    for (const auto &h : handles) {
        if (exited(h.fd)) {
            ++result.exited;
            if (h.forced) ++result.forced;
        } else if (h.sent) result.errors << QString("PID %1 has not exited (possibly uninterruptible kernel sleep).").arg(h.id.pid);
        close(h.fd);
    }
    return result;
}
}
