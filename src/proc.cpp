#include "core.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <unistd.h>
#include <time.h>

namespace scope {
QByteArray readBounded(const QString &path, qint64 limit) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.read(limit);
}
static double monotonicNow() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static QVector<QByteArray> fields(const QByteArray &line) {
    QVector<QByteArray> result;
    for (const auto &f : line.split(' ')) if (!f.isEmpty()) result.append(f);
    return result;
}
bool isCodexExecutable(const QString &exe, const QByteArray &cmdline) {
    QString name = QFileInfo(exe).fileName();
    if (name.endsWith(" (deleted)")) name.chop(10);
    if (name == "codex" || name == "codex-app-server" ||
        QRegularExpression("^codex-(x86_64|aarch64)-(unknown-linux-(gnu|musl))$").match(name).hasMatch())
        return true;
    // npm's JS launcher, not arbitrary commands that mention Codex in a prompt.
    if (name == "node" || name == "nodejs") {
        const auto args = cmdline.split('\0');
        return args.size() > 1 && args[1].endsWith("/@openai/codex/bin/codex.js");
    }
    return false;
}
std::optional<Process> readProcess(int pid, const QString &proc) {
    const QString base = proc + "/" + QString::number(pid);
    QFileInfo info(base);
    if (!info.exists()) return {};
    auto raw = readBounded(base + "/stat", 8192);
    int close = raw.lastIndexOf(')');
    int open = raw.indexOf('(');
    if (open < 0 || close <= open) return {};
    const auto f = fields(raw.mid(close + 2).trimmed());
    if (f.size() < 22) return {};
    Process p;
    p.id.pid = pid;
    p.id.uid = info.ownerId();
    p.id.start = f[19].toULongLong(); // field 22; state is field 3
    p.id.exe = QFileInfo(base + "/exe").symLinkTarget();
    p.name = QString::fromUtf8(raw.mid(open + 1, close - open - 1));
    p.ppid = f[1].toInt();
    p.state = f[0].isEmpty() ? '?' : f[0][0];
    p.ticks = f[11].toULongLong() + f[12].toULongLong();
    p.osThreads = f[17].toInt();
    p.virtualBytes = f[20].toULongLong();
    p.rss = qMax<qint64>(0, f[21].toLongLong()) * quint64(sysconf(_SC_PAGESIZE));
    // CPU accounting/elapsed uses boot-based /proc uptime, including suspend.
    const double uptime = readBounded(proc + "/uptime", 128).split(' ').value(0).toDouble();
    p.age = qMax(0.0, uptime - double(p.id.start) / sysconf(_SC_CLK_TCK));
    if (p.name.startsWith("codex") || p.name == "node" || p.name == "nodejs")
        p.codex = isCodexExecutable(p.id.exe, readBounded(base + "/cmdline"));
    return p;
}
Snapshot scanProcesses(const Snapshot &previous, const QString &proc) {
    QElapsedTimer timer;
    timer.start();
    Snapshot s;
    s.monotonic = monotonicNow();
    const double delta = s.monotonic - previous.monotonic;
    QDir directory(proc);
    if (!directory.exists() || !directory.isReadable()) {
        s.warnings << "Cannot read /proc; process discovery is unavailable.";
        return s;
    }
    const uint uid = getuid();
    for (const auto &name : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        int pid = name.toInt(&ok);
        if (!ok || QFileInfo(proc + "/" + name).ownerId() != uid) continue;
        auto p = readProcess(pid, proc);
        if (!p) continue;
        const auto old = previous.processes.constFind(pid);
        if (old != previous.processes.cend() && old->id.start == p->id.start && delta > 0 &&
            p->ticks >= old->ticks)
            p->cpu = 100.0 * double(p->ticks - old->ticks) / sysconf(_SC_CLK_TCK) / delta;
        s.processes.insert(pid, *p);
    }
    QHash<int, QVector<int>> children;
    for (const auto &p : s.processes) children[p.ppid].append(p.id.pid);
    QSet<int> protectedPids;
    int current = getpid();
    while (current > 1 && !protectedPids.contains(current)) {
        protectedPids.insert(current);
        current = s.processes.value(current).ppid;
    }
    for (auto &p : s.processes) {
        if (!p.codex || p.state == 'Z') continue;
        bool nested = false;
        QSet<int> seen;
        int parent = p.ppid;
        while (s.processes.contains(parent) && !seen.contains(parent)) {
            seen.insert(parent);
            if (s.processes[parent].codex) { nested = true; break; }
            parent = s.processes[parent].ppid;
        }
        if (nested) continue;
        Group g;
        g.root = p.id.pid;
        QVector<int> pending{g.root};
        QSet<int> included;
        while (!pending.isEmpty()) {
            const int pid = pending.takeLast();
            if (included.contains(pid)) continue;
            included.insert(pid);
            g.pids.append(pid);
            g.rss += s.processes[pid].rss;
            g.cpu += s.processes[pid].cpu;
            if (protectedPids.contains(pid)) g.protectedRoot = true;
            pending += children.value(pid);
        }
        p.cwd = QFileInfo(proc + "/" + QString::number(g.root) + "/cwd").symLinkTarget();
        s.groups.append(g);
    }
    std::sort(s.groups.begin(), s.groups.end(), [](const Group &a, const Group &b) { return a.rss > b.rss; });
    const auto mem = memoryDetails(0, proc);
    s.totalMemory = mem.value("MemTotal");
    s.scanMs = timer.nsecsElapsed() / 1e6;
    return s;
}
QHash<QString, quint64> memoryDetails(int pid, const QString &proc) {
    QHash<QString, quint64> result;
    QStringList files = pid ? QStringList{QString("%1/%2/status").arg(proc).arg(pid),
                                           QString("%1/%2/smaps_rollup").arg(proc).arg(pid)}
                            : QStringList{proc + "/meminfo"};
    for (const auto &path : files) {
        for (const auto &line : readBounded(path).split('\n')) {
            const auto colon = line.indexOf(':');
            if (colon < 0) continue;
            const auto v = line.mid(colon + 1).simplified().split(' ');
            if (v.size() >= 2 && v[1] == "kB")
                result[QString::fromLatin1(line.left(colon))] = v[0].toULongLong() * 1024;
        }
    }
    return result;
}
QString bytes(quint64 value) {
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = double(value);
    int unit = 0;
    while (v >= 1024 && unit < 4) { v /= 1024; ++unit; }
    return QString::number(v, 'f', unit ? 1 : 0) + " " + units[unit];
}
QString duration(double seconds) {
    const qint64 n = qMax<qint64>(0, qint64(seconds));
    if (n >= 86400) return QString("%1d %2h").arg(n / 86400).arg(n / 3600 % 24);
    return QString("%1:%2:%3").arg(n / 3600).arg(n / 60 % 60, 2, 10, QChar('0')).arg(n % 60, 2, 10, QChar('0'));
}
}
