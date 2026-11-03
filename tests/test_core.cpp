#include "core.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <memory>
#include <unistd.h>
using namespace scope;
namespace {
void put(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(data), qint64(data.size()));
}
std::unique_ptr<QProcess> fixture(const QString &home, QStringList args = {}) {
    auto p = std::make_unique<QProcess>();
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("CODEX_HOME", home); env.remove("CODEX_THREAD_ID");
    p->setProcessEnvironment(env); p->start(FIXTURE_PATH, args);
    if (!p->waitForStarted() || !p->waitForReadyRead()) return {};
    p->readAllStandardOutput();
    return p;
}
StopPlan planFor(QProcess &p) {
    return makeStopPlan(scanProcesses(), {int(p.processId())});
}
}
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void executableDetection() {
        QVERIFY(isCodexExecutable("/opt/codex", {}));
        QVERIFY(isCodexExecutable("/opt/codex-x86_64-unknown-linux-gnu", {}));
        QVERIFY(isCodexExecutable("/usr/bin/node", QByteArray("node") + '\0' + "/opt/node_modules/@openai/codex/bin/codex.js" + '\0'));
        QVERIFY(isCodexExecutable("/opt/codex (deleted)", {}));
        QVERIFY(!isCodexExecutable("/usr/bin/bash", "bash -c codex"));
        QVERIFY(!isCodexExecutable("/usr/bin/node", QByteArray("node") + '\0' + "/work/my-codex.js" + '\0'));
        QVERIFY(!isCodexExecutable("/opt/codex-killer", {}));
        QVERIFY(!isCodexExecutable("/opt/not-codex", {}));
    }
    void formatValues() {
        QCOMPARE(bytes(1024), QString("1.0 KiB"));
        QCOMPARE(bytes(1ULL << 30), QString("1.0 GiB"));
        QCOMPARE(duration(3661), QString("1:01:01"));
    }
    void procParsing() {
        QTemporaryDir temp;
        auto path = temp.path();
        put(path + "/uptime", "100.0 0.0\n");
        // comm includes parentheses/spaces. Field numbers match proc_pid_stat(5).
        QByteArray stat = "42 (code (x) name) S 1 0 0 0 0 0 0 0 0 0 10 20 0 0 0 0 3 0 100 4096 2 0\n";
        put(path + "/42/stat", stat);
        auto p = readProcess(42, path);
        QVERIFY(p.has_value()); QCOMPARE(p->name, QString("code (x) name"));
        QCOMPARE(p->ticks, quint64(30)); QCOMPARE(p->osThreads, 3); QCOMPARE(p->id.start, quint64(100));
        QCOMPARE(p->rss, quint64(2 * sysconf(_SC_PAGESIZE))); QCOMPARE(p->virtualBytes, quint64(4096));
        put(path + "/42/stat", "bad stat"); QVERIFY(!readProcess(42, path));
    }
    void detailedMemory() {
        QTemporaryDir temp;
        put(temp.path() + "/42/status", "RssAnon:\t20 kB\nVmHWM: 30 kB\nThreads: 12\n");
        put(temp.path() + "/42/smaps_rollup", "Pss: 14 kB\nPrivate_Dirty: 4 kB\n");
        const auto m = memoryDetails(42, temp.path());
        QCOMPARE(m.value("Pss"), quint64(14 * 1024)); QCOMPARE(m.value("RssAnon"), quint64(20 * 1024));
        QVERIFY(!m.contains("Threads"));
    }
    void discoveryAndSelection() {
        QTemporaryDir temp;
        auto a = fixture(temp.path()); auto b = fixture(temp.path());
        QVERIFY(a && b);
        const auto s = scanProcesses();
        QVERIFY(s.processes.value(int(a->processId())).codex);
        auto plan = makeStopPlan(s, {int(a->processId())});
        QCOMPARE(plan.roots.size(), 1); QCOMPARE(plan.targets.size(), 1);
        QCOMPARE(plan.targets.first().pid, int(a->processId()));
        auto result = executeStop(plan, 100);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(result.exited, 1); QCOMPARE(result.forced, 0);
        QVERIFY(a->waitForFinished()); QVERIFY(readProcess(int(b->processId())).has_value());
        b->kill(); b->waitForFinished();
    }
    void allConfirmedHostsExit() {
        QTemporaryDir temp;
        auto a = fixture(temp.path()); auto b = fixture(temp.path()); QVERIFY(a && b);
        // Explicitly choose only our fixtures: never act on other discovered Codex hosts.
        auto plan = makeStopPlan(scanProcesses(), {int(a->processId()), int(b->processId())});
        QCOMPARE(plan.roots.size(), 2);
        auto result = executeStop(plan, 100);
        QCOMPARE(result.exited, 2); QCOMPARE(result.forced, 0); QVERIFY(result.errors.isEmpty());
        a->waitForFinished(); b->waitForFinished();
    }
    void forcedExit() {
        QTemporaryDir temp; auto p = fixture(temp.path(), {"--ignore-term"}); QVERIFY(p);
        auto result = executeStop(planFor(*p), 80);
        QCOMPARE(result.exited, 1); QCOMPARE(result.forced, 1); QVERIFY(result.errors.isEmpty());
        QVERIFY(p->waitForFinished());
    }
    void childTree() {
        QTemporaryDir temp; auto p = fixture(temp.path(), {"--fork-child"}); QVERIFY(p);
        QTest::qWait(50);
        auto plan = planFor(*p); QCOMPARE(plan.targets.size(), 2);
        auto result = executeStop(plan, 100);
        QCOMPARE(result.exited, 2); QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        p->waitForFinished();
    }
    void staleIdentityRefused() {
        QTemporaryDir temp; auto p = fixture(temp.path()); QVERIFY(p);
        auto plan = planFor(*p); ++plan.roots[0].start;
        const auto r = executeStop(plan, 10);
        QCOMPARE(r.exited, 0); QVERIFY(!r.errors.isEmpty()); QVERIFY(readProcess(int(p->processId())));
        p->kill(); p->waitForFinished();
    }
    void unrelatedProcessRefused() {
        QProcess p; p.start("/usr/bin/sleep", {"120"}); QVERIFY(p.waitForStarted());
        const auto process = readProcess(int(p.processId())); QVERIFY(process);
        StopPlan plan; plan.roots = {process->id}; plan.targets = {process->id};
        const auto r = executeStop(plan, 10);
        QCOMPARE(r.exited, 0); QVERIFY(!r.errors.isEmpty()); QVERIFY(readProcess(int(p.processId())));
        p.kill(); p.waitForFinished();
    }
    void unrelatedTargetRefused() {
        QTemporaryDir temp; auto p = fixture(temp.path()); QVERIFY(p);
        QProcess other; other.start("/usr/bin/sleep", {"120"}); QVERIFY(other.waitForStarted());
        auto plan = planFor(*p); plan.targets << readProcess(int(other.processId()))->id;
        auto r = executeStop(plan, 50);
        QCOMPARE(r.exited, 1); QVERIFY(!r.errors.isEmpty()); QVERIFY(readProcess(int(other.processId())));
        p->waitForFinished(); other.kill(); other.waitForFinished();
    }
    void protectedRootExcluded() {
        Snapshot s; Process p; p.id = {123, 4, uint(getuid()), "/opt/codex"}; p.codex = true;
        s.processes.insert(123, p); Group g; g.root = 123; g.pids = {123}; g.protectedRoot = true; s.groups << g;
        QVERIFY(makeStopPlan(s, {123}).roots.isEmpty());
    }
    void openRolloutAndReadonlyDatabase() {
        QTemporaryDir temp;
        const QString id = "00000000-0000-0000-0000-000000000001";
        const QString child = "00000000-0000-0000-0000-000000000002";
        const auto path = temp.path() + "/sessions/2026/01/01/rollout-2026-01-01-" + id + ".jsonl";
        put(path, ("{\"type\":\"session_meta\",\"payload\":{\"id\":\"" + id + "\",\"cwd\":\"/example\",\"cli_version\":\"test\"}}\n"
             "{\"type\":\"turn_context\",\"payload\":{\"model\":\"example-model\",\"effort\":\"high\"}}\n").toUtf8());
        const auto dbPath = temp.path() + "/state_12.sqlite";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "test"); db.setDatabaseName(dbPath); QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE threads(id TEXT PRIMARY KEY,title TEXT,model TEXT,tokens_used INTEGER)"));
            QVERIFY(q.exec("CREATE TABLE thread_spawn_edges(parent_thread_id TEXT,child_thread_id TEXT,status TEXT)"));
            q.prepare("INSERT INTO threads VALUES(?,?,?,?)"); q.addBindValue(id); q.addBindValue("Example task");
            q.addBindValue("database-model"); q.addBindValue(42); QVERIFY(q.exec());
            q.prepare("INSERT INTO thread_spawn_edges VALUES(?,?,?)"); q.addBindValue(id); q.addBindValue(child); q.addBindValue("open"); QVERIFY(q.exec());
            db.close();
        }
        QSqlDatabase::removeDatabase("test");
        const auto before = QCryptographicHash::hash(readBounded(dbPath, 1048576), QCryptographicHash::Sha256);
        auto p = fixture(temp.path(), {"--rollout", path}); QVERIFY(p);
        QVERIFY(rolloutFiles(int(p->processId()), temp.path()).contains(path));
        auto s = scanProcesses(); enrichSessions(s, temp.path());
        QVector<Session> rows;
        for (const auto &g : s.groups) if (g.root == p->processId()) rows = g.sessions;
        QCOMPARE(rows.size(), 2);
        for (const auto &r : rows) {
            if (r.id == id) { QVERIFY(r.observed); QCOMPARE(r.model, QString("database-model")); QCOMPARE(r.tokens, qint64(42)); }
            if (r.id == child) { QVERIFY(!r.observed); QCOMPARE(r.parent, id); QVERIFY(r.status.contains("liveness unknown")); }
        }
        QCOMPARE(QCryptographicHash::hash(readBounded(dbPath, 1048576), QCryptographicHash::Sha256), before);
        p->kill(); p->waitForFinished();
    }
    void noHistoryAsLive() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/sessions/rollout-00000000-0000-0000-0000-000000000001.jsonl";
        put(path, "{\"type\":\"session_meta\",\"payload\":{\"id\":\"00000000-0000-0000-0000-000000000001\"}}\n");
        auto p = fixture(temp.path()); QVERIFY(p);
        auto s = scanProcesses(); enrichSessions(s, temp.path());
        for (const auto &g : s.groups) if (g.root == p->processId()) QVERIFY(g.sessions.isEmpty());
        p->kill(); p->waitForFinished();
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
