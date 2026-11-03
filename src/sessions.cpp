#include "core.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

namespace scope {
static bool uuid(const QString &v) {
    static const QRegularExpression re("^[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$");
    return re.match(v).hasMatch();
}
QStringList rolloutFiles(int pid, const QString &home, const QString &proc) {
    QSet<QString> found;
    const QString prefix = QFileInfo(home).canonicalFilePath() + "/sessions/";
    if (prefix == "/sessions/") return {};
    QDir fds(QString("%1/%2/fd").arg(proc).arg(pid));
    for (const auto &fd : fds.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        const QString target = QFileInfo(fds.filePath(fd)).symLinkTarget();
        if (target.startsWith(prefix) && target.endsWith(".jsonl") &&
            QFileInfo(target).fileName().startsWith("rollout-")) found.insert(target);
    }
    return found.values();
}
Session readRollout(const QString &path) {
    Session s;
    s.rollout = path;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return s;
    const auto first = f.readLine(262144);
    const auto record = QJsonDocument::fromJson(first).object();
    if (record["type"].toString() == "session_meta") {
        const auto meta = record["payload"].toObject();
        s.id = meta["id"].toString();
        s.parent = meta["parent_thread_id"].toString();
        s.cwd = meta["cwd"].toString();
        s.version = meta["cli_version"].toString();
        s.provider = meta["model_provider"].toString();
        s.role = meta["agent_role"].toString();
        s.nickname = meta["agent_nickname"].toString();
        const auto src = meta["source"];
        s.source = src.isString() ? src.toString() : QString::fromUtf8(QJsonDocument(src.toObject()).toJson(QJsonDocument::Compact));
        const auto spawn = src.toObject()["subagent"].toObject()["thread_spawn"].toObject();
        if (s.parent.isEmpty()) s.parent = spawn["parent_thread_id"].toString();
    }
    // Recover the ID even when a huge session_meta exceeds the bounded first-line read.
    if (!uuid(s.id)) {
        auto match = QRegularExpression("([0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12})\\.jsonl$").match(path);
        s.id = match.captured(1);
    }
    f.seek(qMax<qint64>(0, f.size() - 65536));
    const auto tail = f.read(65536);
    for (const auto &line : tail.split('\n')) {
        const auto rec = QJsonDocument::fromJson(line).object();
        const auto v = rec["payload"].toObject();
        if (rec["type"].toString() == "turn_context") {
            s.model = v["model"].toString();
            s.effort = v["effort"].toString();
        }
        if (rec["type"].toString() == "event_msg") {
            const auto type = v["type"].toString();
            if (type == "task_started" || type == "turn_started") s.status = "Last event: working";
            if (type == "task_complete" || type == "turn_complete") s.status = "Last event: completed";
            if (type == "turn_aborted") s.status = "Last event: aborted";
        }
    }
    return s;
}
static QString envValue(int pid, const QByteArray &key) {
    const auto data = readBounded(QString("/proc/%1/environ").arg(pid), 1048576);
    const auto prefix = key + '=';
    for (const auto &entry : data.split('\0'))
        if (entry.startsWith(prefix)) return QString::fromUtf8(entry.mid(prefix.size()));
    return {};
}
static void mergeRecord(Session &s, QSqlQuery &q) {
    auto value = [&](const char *name) {
        const int index = q.record().indexOf(name);
        return index >= 0 ? q.value(index) : QVariant{};
    };
    auto string = [&](QString &out, const char *name) {
        const auto v = value(name).toString();
        if (!v.isEmpty()) out = v;
    };
    string(s.title, "title"); string(s.model, "model"); string(s.provider, "model_provider");
    string(s.effort, "reasoning_effort"); string(s.role, "agent_role"); string(s.nickname, "agent_nickname");
    string(s.cwd, "cwd"); string(s.branch, "git_branch"); string(s.source, "source");
    string(s.version, "cli_version"); string(s.rollout, "rollout_path");
    s.tokens = value("tokens_used").toLongLong();
}
void enrichSessions(Snapshot &s, const QString &defaultHome) {
    for (auto &g : s.groups) {
        QHash<QString, Session> sessions;
        QString home = defaultHome;
        const auto perProcess = envValue(g.root, "CODEX_HOME");
        if (!perProcess.isEmpty()) home = perProcess;
        home = QFileInfo(home).absoluteFilePath();
        for (const int pid : g.pids) {
            if (!s.processes.value(pid).codex) continue;
            for (const auto &path : rolloutFiles(pid, home)) {
                auto row = readRollout(path);
                if (uuid(row.id)) { row.observed = true; sessions.insert(row.id, row); }
            }
            const auto id = envValue(pid, "CODEX_THREAD_ID");
            if (uuid(id) && !sessions.contains(id)) {
                Session row;
                row.id = id; row.observed = true;
                sessions.insert(id, row);
            }
        }
        // Never scan a session archive or infer liveness from recent timestamps.
        if (sessions.isEmpty()) continue;
        const auto dbs = QDir(home).entryList({"state_*.sqlite"}, QDir::Files, QDir::Name);
        QString dbPath;
        int version = -1;
        for (const auto &file : dbs) {
            const int n = file.mid(6, file.size() - 13).toInt();
            if (n > version) { version = n; dbPath = QDir(home).filePath(file); }
        }
        if (!dbPath.isEmpty()) {
            const QString connection = QUuid::createUuid().toString();
            {
                auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
                db.setDatabaseName(dbPath);
                db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=50");
                if (db.open()) {
                    QSqlQuery pragma(db);
                    pragma.exec("PRAGMA query_only=ON");
                    const auto observed = sessions.keys();
                    for (const auto &id : observed) {
                        QSqlQuery edge(db);
                        edge.prepare("SELECT parent_thread_id,child_thread_id,status FROM thread_spawn_edges WHERE parent_thread_id=? OR child_thread_id=? LIMIT 201");
                        edge.addBindValue(id); edge.addBindValue(id);
                        int count = 0;
                        if (edge.exec()) {
                            while (edge.next() && count++ < 200) {
                                const auto parent = edge.value(0).toString();
                                const auto child = edge.value(1).toString();
                                if (sessions.contains(child)) sessions[child].parent = parent;
                                if (parent == id && uuid(child) && !sessions.contains(child)) {
                                    Session row;
                                    row.id = child; row.parent = parent;
                                    row.status = "Recorded edge: " + edge.value(2).toString() + " (liveness unknown)";
                                    sessions.insert(child, row);
                                }
                            }
                            if (count > 200) s.warnings << "Related-session display capped at 200 edges per observed thread.";
                        }
                    }
                    // Inspect the optional columns once, not once per logical session.
                    const auto cols = db.record("threads");
                    const QStringList wanted{"id","title","model","model_provider","reasoning_effort","agent_role","agent_nickname","cwd","git_branch","source","cli_version","rollout_path","tokens_used"};
                    QStringList projection;
                    for (const auto &col : wanted) if (cols.indexOf(col) >= 0) projection << col;
                    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
                        QSqlQuery q(db);
                        // Explicit metadata projection: never load prompts, previews or tool bodies.
                        q.prepare("SELECT " + projection.join(',') + " FROM threads WHERE id=?");
                        q.addBindValue(it.key());
                        if (q.exec() && q.next()) mergeRecord(it.value(), q);
                        else if (q.lastError().isValid()) s.warnings << "Codex thread metadata schema is unavailable; showing process/rollout information only.";
                    }
                    db.close();
                } else s.warnings << "Cannot open Codex state database read-only; showing process/rollout information only.";
            }
            QSqlDatabase::removeDatabase(connection);
        }
        g.sessions = sessions.values().toVector();
        std::sort(g.sessions.begin(), g.sessions.end(), [](const Session &a, const Session &b) { return a.id < b.id; });
    }
    s.warnings.removeDuplicates();
}
}
