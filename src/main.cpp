#include "core.h"
#include "window.h"
#include "appearance.h"
#include "branding.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cstdio>
#include <memory>

int main(int argc, char **argv) {
    bool headless = false;
    for (int n = 1; n < argc; ++n) if (QByteArray(argv[n]) == "--snapshot") headless = true;
    std::unique_ptr<QCoreApplication> app;
    if (headless) app = std::make_unique<QCoreApplication>(argc, argv);
    else {
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
        app = std::make_unique<QApplication>(argc, argv);
    }
    QCoreApplication::setApplicationName("codex-killer");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCoreApplication::setOrganizationName("Codex Killer");
    QCommandLineParser parser;
    parser.setApplicationDescription("A lightweight native Linux Codex session and memory monitor. No telemetry or background daemon.");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"codex-home", "Codex home (default: CODEX_HOME or ~/.codex; per-process CODEX_HOME is also respected).", "path"});
    parser.addOption({"snapshot", "Print one read-only JSON snapshot, without opening a window."});
    parser.addOption({"demo", "Show synthetic demonstration data. Kill actions are disabled."});
    parser.addOption({"demo-appearance", "Force light or dark appearance for synthetic demos only.", "light|dark"});
    parser.addOption({"quit-after-ms", "Exit after a duration (for smoke tests).", "milliseconds"});
    parser.addOption({"demo-screenshot", "Save a synthetic demo window screenshot; requires --demo.", "path"});
    parser.process(*app);
    QString home = parser.value("codex-home");
    if (home.isEmpty()) home = qEnvironmentVariable("CODEX_HOME", QDir::homePath() + "/.codex");
    if (headless) {
        auto s = scope::scanProcesses();
        scope::enrichSessions(s, home);
        QJsonArray groups;
        for (const auto &g : s.groups) {
            const auto p = s.processes.value(g.root);
            QJsonArray sessions;
            for (const auto &t : g.sessions)
                sessions.append(QJsonObject{{"id", t.id}, {"parent", t.parent}, {"model", t.model}, {"observed", t.observed}});
            groups.append(QJsonObject{{"pid", g.root}, {"cwd", p.cwd}, {"own_rss_bytes", double(p.rss)},
                {"tree_rss_bytes", double(g.rss)}, {"process_count", g.pids.size()}, {"protected", g.protectedRoot}, {"sessions", sessions}});
        }
        const auto json = QJsonDocument(QJsonObject{{"hosts", groups}, {"scan_ms", s.scanMs},
            {"warnings", QJsonArray::fromStringList(s.warnings)}}).toJson();
        std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
        return 0;
    }
    if (parser.isSet("demo-screenshot") && !parser.isSet("demo")) {
        std::fprintf(stderr, "--demo-screenshot requires --demo; real session data is never captured automatically.\n");
        return 2;
    }
    if (parser.isSet("demo-appearance")) {
        const auto mode = parser.value("demo-appearance");
        if (!parser.isSet("demo") || (mode != "light" && mode != "dark")) {
            std::fprintf(stderr, "--demo-appearance requires --demo and a value of light or dark.\n");
            return 2;
        }
        QApplication::setPalette(appearance::palette(mode == "dark"));
    }
    QGuiApplication::setDesktopFileName(branding::desktopId);
    QApplication::setWindowIcon(branding::icon());
    Window window(home, parser.isSet("demo"));
    window.show();
    if (parser.isSet("demo-screenshot")) {
        const auto path = parser.value("demo-screenshot");
        QTimer::singleShot(500, &window, [&window, path] {
            if (!window.grab().save(path)) std::fprintf(stderr, "Failed to save demo screenshot.\n");
        });
    }
    if (parser.isSet("quit-after-ms")) {
        bool ok;
        const int ms = parser.value("quit-after-ms").toInt(&ok);
        if (!ok || ms < 1) { std::fprintf(stderr, "--quit-after-ms must be positive.\n"); return 2; }
        QTimer::singleShot(ms, &window, &QWidget::close);
    }
    return app->exec();
}
