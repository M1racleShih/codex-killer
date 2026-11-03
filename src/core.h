#pragma once
#include <QHash>
#include <QStringList>
#include <QVector>
#include <optional>

namespace scope {
struct Identity {
    int pid = 0;
    quint64 start = 0;
    uint uid = 0;
    QString exe;
    bool operator==(const Identity &o) const {
        return pid == o.pid && start == o.start && uid == o.uid && exe == o.exe;
    }
};
struct Process {
    Identity id;
    int ppid = 0, osThreads = 0;
    char state = '?';
    quint64 rss = 0, virtualBytes = 0, ticks = 0;
    double cpu = 0, age = 0;
    QString name, cwd;
    bool codex = false;
};
struct Session {
    QString id, parent, title, model, provider, effort, role, nickname, cwd, branch;
    QString source, version, rollout, status;
    qint64 tokens = 0;
    bool observed = false; // Open rollout FD / explicit process thread identity, NOT mtime.
};
struct Group {
    int root = 0;
    QVector<int> pids;
    QVector<Session> sessions;
    quint64 rss = 0;
    double cpu = 0;
    bool protectedRoot = false;
};
struct Snapshot {
    QHash<int, Process> processes;
    QVector<Group> groups;
    QStringList warnings;
    double monotonic = 0;
    quint64 totalMemory = 0;
    double scanMs = 0;
};
QByteArray readBounded(const QString &path, qint64 limit = 65536);
std::optional<Process> readProcess(int pid, const QString &proc = "/proc");
bool isCodexExecutable(const QString &exe, const QByteArray &cmdline);
Snapshot scanProcesses(const Snapshot &previous = {}, const QString &proc = "/proc");
QStringList rolloutFiles(int pid, const QString &home, const QString &proc = "/proc");
Session readRollout(const QString &path);
void enrichSessions(Snapshot &snapshot, const QString &home);
QHash<QString, quint64> memoryDetails(int pid, const QString &proc = "/proc");
QString bytes(quint64 value);
QString duration(double seconds);

struct StopPlan {
    QVector<Identity> roots;
    QVector<Identity> targets;
    QStringList affected;
};
StopPlan makeStopPlan(const Snapshot &snapshot, const QVector<int> &roots);
struct StopResult {
    int exited = 0, forced = 0;
    QStringList errors;
};
StopResult executeStop(const StopPlan &plan, int graceMs = 3000);
}
