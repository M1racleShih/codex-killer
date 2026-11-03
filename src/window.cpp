#include "window.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>
#include <unistd.h>

using namespace scope;
namespace {
enum { RootRole = Qt::UserRole, SessionRole, ProcessRole, KeyRole, NumberRole };
class Item : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override {
        const int col = treeWidget()->sortColumn();
        const auto a = data(col, NumberRole), b = other.data(col, NumberRole);
        if (a.isValid() && b.isValid()) return a.toDouble() < b.toDouble();
        return text(col).localeAwareCompare(other.text(col)) < 0;
    }
};
class MemoryBar : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    quint64 largest = 1;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        const auto value = index.data(NumberRole);
        if (!value.isValid()) return;
        const auto width = qMax(0, option.rect.width() - 10);
        const auto length = int(width * qMin(1.0, value.toDouble() / double(largest)));
        auto color = option.palette.highlight().color();
        if (option.state & QStyle::State_Selected) color = option.palette.highlightedText().color();
        painter->fillRect(option.rect.left() + 5, option.rect.bottom() - 3, length, 2, color);
    }
};
QString stateName(char c) {
    switch (c) {
    case 'R': return "Running"; case 'S': return "Sleeping"; case 'D': return "I/O wait";
    case 'T': case 't': return "Stopped"; case 'Z': return "Zombie"; default: return QString(c);
    }
}
void numeric(QTreeWidgetItem *i, int col, double number, const QString &text) {
    i->setData(col, NumberRole, number);
    i->setText(col, text);
    i->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
}
}
MemoryChart::MemoryChart(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(110);
    setAccessibleName("Total tracked process-tree resident memory history");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}
void MemoryChart::add(double time, quint64 value) {
    points.append({time, value});
    while (points.size() > 120) points.removeFirst();
    setAccessibleDescription("Current total RSS: " + bytes(value) + ". Shared pages may be counted more than once.");
    update();
}
void MemoryChart::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto pal = palette();
    p.fillRect(rect(), pal.base());
    p.setPen(pal.text().color());
    p.drawText(12, 22, "Process-tree RSS · last 120 samples");
    quint64 max = 1024 * 1024;
    for (const auto &point : points) max = qMax(max, point.second);
    p.drawText(QRect(12, 7, width() - 24, 20), Qt::AlignRight, bytes(max));
    const QRectF plot(12, 34, qMax(1, width() - 24), qMax(1, height() - 48));
    p.setPen(pal.mid().color());
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
    if (points.isEmpty()) return;
    const double start = points.first().first;
    const double span = qMax(1.0, points.last().first - start);
    QPainterPath path;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF pt(plot.left() + plot.width() * (points[i].first - start) / span,
                         plot.bottom() - plot.height() * double(points[i].second) / double(max));
        if (i == 0) path.moveTo(pt); else path.lineTo(pt);
    }
    p.setPen(QPen(pal.highlight().color(), 2));
    p.drawPath(path);
    if (points.size() == 1) p.drawEllipse(path.currentPosition(), 2.0, 2.0);
}
Window::Window(QString home, bool demo) : home(std::move(home)), demo(demo) {
    setWindowTitle(demo ? "Codex Killer — Demo (actions disabled)" : "Codex Killer");
    // A tiny procedural icon avoids mapping a large desktop icon-theme cache at startup.
    QPixmap icon(64, 64); icon.fill(Qt::transparent);
    {
        QPainter painter(&icon); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor("#20384b"));
        painter.drawRoundedRect(QRectF(4, 4, 56, 56), 12, 12);
        painter.setPen(QPen(Qt::white, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(16, 22, 26, 32); painter.drawLine(26, 32, 16, 42); painter.drawLine(33, 42, 48, 42);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor("#ed9276")); painter.drawEllipse(QPointF(47, 17), 6, 6);
    }
    setWindowIcon(QIcon(icon));
    resize(1220, 820);
    setMinimumSize(780, 600);
    auto central = new QWidget;
    auto layout = new QVBoxLayout(central);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    auto heading = new QLabel("Codex Killer");
    auto titleFont = heading->font(); titleFont.setPointSize(titleFont.pointSize() + 7); titleFont.setBold(true);
    heading->setFont(titleFont);
    auto subtitle = new QLabel("Know what is running. Keep memory under control.");
    layout->addWidget(heading);
    layout->addWidget(subtitle);
    summary = new QLabel("Discovering owned Codex processes…");
    summary->setObjectName("summary");
    summary->setTextFormat(Qt::PlainText);
    auto summaryFont = summary->font(); summaryFont.setBold(true); summary->setFont(summaryFont);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto controls = new QHBoxLayout;
    search = new QLineEdit;
    search->setObjectName("search");
    search->setPlaceholderText("Filter by PID, project, session or model…");
    search->setClearButtonEnabled(true);
    search->setAccessibleName("Filter Codex hosts and sessions");
    auto refreshButton = new QPushButton("Refresh");
    pause = new QCheckBox("Pause");
    pause->setToolTip("Pause periodic sampling. Refresh still works.");
    controls->addWidget(search, 1); controls->addWidget(refreshButton); controls->addWidget(pause);
    layout->addLayout(controls);
    auto actions = new QHBoxLayout;
    auto select = new QPushButton("Check visible hosts");
    auto clear = new QPushButton("Clear checks");
    stopSelected = new QPushButton("Kill checked…");
    stopAll = new QPushButton("Kill all…");
    stopSelected->setObjectName("killSelected"); stopAll->setObjectName("killAll");
    stopSelected->setToolTip("Confirm checked hosts and their child processes, then SIGTERM → SIGKILL.");
    stopAll->setToolTip("All discovered, unprotected hosts, including hosts hidden by the filter. Confirmation required.");
    selection = new QLabel("0 hosts checked");
    actions->addWidget(select); actions->addWidget(clear); actions->addWidget(selection, 1);
    actions->addWidget(stopSelected); actions->addWidget(stopAll);
    layout->addLayout(actions);
    notice = new QLabel;
    notice->setTextFormat(Qt::PlainText); notice->setWordWrap(true); notice->setObjectName("notice");
    layout->addWidget(notice);
    auto split = new QSplitter(Qt::Vertical);
    tree = new QTreeWidget;
    tree->setObjectName("processTree");
    tree->setHeaderLabels({"Host / session / process", "PID", "Own RSS", "Tree RSS", "CPU", "Elapsed", "State / evidence"});
    tree->setAccessibleName("Codex hosts, logical sessions and child processes");
    tree->setItemDelegateForColumn(3, new MemoryBar(tree));
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setSortingEnabled(true);
    tree->sortItems(3, Qt::DescendingOrder);
    tree->header()->setStretchLastSection(true);
    tree->setColumnWidth(0, 330); tree->setColumnWidth(1, 80);
    for (int col = 2; col <= 5; ++col) tree->setColumnWidth(col, 100);
    tree->setExpandsOnDoubleClick(true);
    split->addWidget(tree);
    auto tabs = new QTabWidget;
    details = new QPlainTextEdit;
    details->setObjectName("details");
    details->setReadOnly(true);
    details->setAccessibleName("Selected host or session details");
    details->setPlaceholderText("Select a host, session or child process to inspect it. Only host checkboxes choose kill targets.");
    details->setMaximumBlockCount(1000);
    auto overview = new QWidget;
    auto overviewLayout = new QHBoxLayout(overview);
    overviewLayout->setContentsMargins(0, 0, 0, 0);
    overviewLayout->addWidget(details, 3);
    chart = new MemoryChart;
    overviewLayout->addWidget(chart, 2);
    tabs->addTab(overview, "Inspector & memory");
    activity = new QPlainTextEdit;
    activity->setReadOnly(true); activity->setMaximumBlockCount(300);
    activity->setAccessibleName("Termination results");
    tabs->addTab(activity, "Action log");
    split->addWidget(tabs); split->setSizes({440, 220});
    layout->addWidget(split, 1);
    setCentralWidget(central);
    auto fileMenu = menuBar()->addMenu("&File");
    auto refreshAction = fileMenu->addAction("&Refresh", this, &Window::refresh, QKeySequence::Refresh);
    Q_UNUSED(refreshAction)
    fileMenu->addAction("&Quit", this, &QWidget::close, QKeySequence::Quit);
    auto hostsMenu = menuBar()->addMenu("&Hosts");
    hostsMenu->addAction("Check &visible hosts", select, &QPushButton::click);
    hostsMenu->addAction("&Clear checks", clear, &QPushButton::click);
    hostsMenu->addAction("Kill &checked…", stopSelected, &QPushButton::click, QKeySequence(Qt::Key_Delete));
    hostsMenu->addAction("Kill &all…", stopAll, &QPushButton::click);
    auto viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("&Filter", search, qOverload<>(&QWidget::setFocus), QKeySequence::Find);
    auto pauseAction = viewMenu->addAction("&Pause sampling");
    pauseAction->setCheckable(true);
    connect(pauseAction, &QAction::toggled, pause, &QCheckBox::setChecked);
    connect(pause, &QCheckBox::toggled, pauseAction, &QAction::setChecked);
    auto help = menuBar()->addMenu("&Help");
    help->addAction("&About", this, [this] {
        QMessageBox::about(this, "About Codex Killer", "Codex Killer 0.1.0\nNative, local-only Linux Codex monitor.\n\nMemory belongs to processes, not logical sessions.\nShared-host sessions cannot be killed independently.\nNo telemetry, daemon or network listener.\n\nhttps://github.com/M1racleShih/codex-killer");
    });
    connect(search, &QLineEdit::textChanged, this, &Window::filter);
    connect(refreshButton, &QPushButton::clicked, this, &Window::refresh);
    connect(tree, &QTreeWidget::itemSelectionChanged, this, &Window::showDetails);
    connect(tree, &QTreeWidget::itemChanged, this, [this] { updateButtons(); });
    connect(select, &QPushButton::clicked, this, [this] {
        for (int n = 0; n < tree->topLevelItemCount(); ++n) {
            auto i = tree->topLevelItem(n);
            if (!i->isHidden() && (i->flags() & Qt::ItemIsUserCheckable)) i->setCheckState(0, Qt::Checked);
        }
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        for (int n = 0; n < tree->topLevelItemCount(); ++n) {
            auto i = tree->topLevelItem(n);
            if (i->flags() & Qt::ItemIsUserCheckable) i->setCheckState(0, Qt::Unchecked);
        }
    });
    connect(stopSelected, &QPushButton::clicked, this, [this] { stop(false); });
    connect(stopAll, &QPushButton::clicked, this, [this] { stop(true); });
    connect(&timer, &QTimer::timeout, this, [this] { if (!pause->isChecked()) refresh(); });
    connect(&scanner, &QFutureWatcher<Snapshot>::finished, this, [this] { applySnapshot(scanner.result()); });
    connect(&memoryReader, &QFutureWatcher<MemorySample>::finished, this, [this] {
        const auto sample = memoryReader.result();
        selectedMemory = sample.values; memoryIdentity = sample.id; memoryAge.restart();
        showDetails();
    });
    connect(&stopper, &QFutureWatcher<StopResult>::finished, this, [this] {
        const auto result = stopper.result();
        busyStop = false;
        const QString message = QString("%1 exited; %2 required SIGKILL; %3 warnings/errors.").arg(result.exited).arg(result.forced).arg(result.errors.size());
        activity->appendPlainText(QDateTime::currentDateTime().toString(Qt::ISODate) + " — " + message);
        for (const auto &error : result.errors) activity->appendPlainText(error);
        statusBar()->showMessage(message);
        updateButtons(); refresh();
    });
    timer.setTimerType(Qt::VeryCoarseTimer);
    timer.start(2000);
    updateButtons();
    if (demo) {
        for (int n = 0; n < 7; ++n) chart->add(n - 6, quint64(360 + n * 8 + (n % 2) * 10) << 20);
        applySnapshot(demoSnapshot());
        tree->expandAll();
        tree->setCurrentItem(tree->topLevelItem(0));
    } else refresh();
}
Window::~Window() { scanner.waitForFinished(); memoryReader.waitForFinished(); stopper.waitForFinished(); }
void Window::closeEvent(QCloseEvent *event) {
    if (busyStop) { statusBar()->showMessage("Waiting for the confirmed termination operation to finish…"); event->ignore(); }
    else QMainWindow::closeEvent(event);
}
void Window::refresh() {
    timer.setInterval(isMinimized() ? 10000 : 2000);
    if (demo || scanner.isRunning() || busyStop) return;
    const auto previous = snapshot;
    const auto codexHome = home;
    // Only one scan worker exists; the GUI never waits for /proc or SQLite reads.
    scanner.setFuture(QtConcurrent::run([previous, codexHome] {
        QElapsedTimer elapsed; elapsed.start();
        auto next = scanProcesses(previous);
        enrichSessions(next, codexHome);
        next.scanMs = elapsed.nsecsElapsed() / 1e6;
        return next;
    }));
}
QVector<int> Window::checkedRoots() const {
    QVector<int> roots;
    for (int n = 0; n < tree->topLevelItemCount(); ++n) {
        const auto i = tree->topLevelItem(n);
        if (i->checkState(0) == Qt::Checked) roots << i->data(0, RootRole).toInt();
    }
    return roots;
}
void Window::applySnapshot(const Snapshot &next) {
    snapshot = next;
    auto bar = static_cast<MemoryBar *>(tree->itemDelegateForColumn(3));
    bar->largest = 1;
    for (const auto &g : snapshot.groups) bar->largest = qMax(bar->largest, g.rss);
    quint64 rss = 0; double cpu = 0; int observed = 0, related = 0, pids = 0;
    for (const auto &g : snapshot.groups) {
        rss += g.rss; cpu += g.cpu; pids += g.pids.size();
        for (const auto &s : g.sessions) s.observed ? ++observed : ++related;
    }
    summary->setText(QString("%1 hosts  ·  %2 observed sessions  ·  %3 related records  ·  %4 processes  ·  %5 tree RSS  ·  %6% CPU")
        .arg(snapshot.groups.size()).arg(observed).arg(related).arg(pids).arg(bytes(rss)).arg(cpu, 0, 'f', 1));
    QString message;
    if (demo) message = "DEMO — synthetic data. All kill actions are disabled.";
    else if (snapshot.groups.isEmpty()) message = "No running Codex hosts found for your user. Start Codex normally; this view updates every 2 seconds.";
    else message = "Check a host to choose kill targets. Sessions share host memory and termination. Related records are not proof of a running agent.";
    if (!snapshot.warnings.isEmpty()) message += "\n" + snapshot.warnings.join('\n');
    notice->setText(message);
    chart->add(snapshot.monotonic, rss);
    rebuild();
    statusBar()->showMessage(QString("Local only · scan %1 ms · %2 s refresh · RSS can double-count shared pages%3")
        .arg(snapshot.scanMs, 0, 'f', 1).arg(timer.interval() / 1000).arg(pause->isChecked() ? " · paused" : ""));
}
void Window::rebuild() {
    const auto checked = checkedRoots();
    QHash<int, quint64> identities;
    QSet<QString> expanded;
    const auto current = tree->currentItem();
    const QString key = current ? current->data(0, KeyRole).toString() : QString();
    QTreeWidgetItemIterator old(tree);
    while (*old) {
        auto i = *old;
        if (i->isExpanded()) expanded.insert(i->data(0, KeyRole).toString());
        if (!i->parent()) identities[i->data(0, RootRole).toInt()] = i->data(1, Qt::UserRole).toULongLong();
        ++old;
    }
    const int scroll = tree->verticalScrollBar()->value();
    tree->blockSignals(true); tree->setUpdatesEnabled(false); tree->setSortingEnabled(false);
    tree->clear();
    for (const auto &g : snapshot.groups) {
        const auto root = snapshot.processes.value(g.root);
        auto host = new Item(tree);
        host->setData(0, RootRole, g.root); host->setData(0, ProcessRole, g.root);
        host->setData(0, KeyRole, QString("p:%1:%2").arg(g.root).arg(root.id.start));
        host->setData(1, Qt::UserRole, root.id.start);
        host->setText(0, root.cwd.isEmpty() ? "Codex host" : root.cwd);
        host->setToolTip(0, root.id.exe + "\n" + root.cwd);
        if (!g.protectedRoot && !demo) {
            host->setFlags(host->flags() | Qt::ItemIsUserCheckable);
            host->setCheckState(0, checked.contains(g.root) && identities.value(g.root) == root.id.start ? Qt::Checked : Qt::Unchecked);
        }
        numeric(host, 1, g.root, QString::number(g.root));
        numeric(host, 2, root.rss, bytes(root.rss)); numeric(host, 3, g.rss, bytes(g.rss));
        numeric(host, 4, g.cpu, QString::number(g.cpu, 'f', 1) + "%");
        numeric(host, 5, root.age, duration(root.age));
        host->setText(6, g.protectedRoot ? "Protected: contains this app/ancestor" : stateName(root.state));
        QHash<QString, QTreeWidgetItem *> sessions;
        for (const auto &s : g.sessions) {
            auto i = new Item(host);
            i->setData(0, RootRole, g.root); i->setData(0, SessionRole, s.id);
            i->setData(0, KeyRole, "s:" + QString::number(g.root) + ":" + s.id);
            i->setText(0, !s.nickname.isEmpty() ? s.nickname : (!s.title.isEmpty() ? s.title.left(100) : s.id));
            i->setToolTip(0, s.id + "\n" + s.title);
            i->setText(1, "Shared"); i->setText(2, "Shared"); i->setText(3, "—");
            i->setText(4, "—"); i->setText(5, "—");
            i->setText(6, s.observed ? "Observed · " + (s.model.isEmpty() ? QString("model unknown") : s.model) : "Related · liveness unknown");
            sessions.insert(s.id, i);
        }
        // Reparent only acyclic edges; malformed local metadata cannot create recursive trees.
        for (const auto &s : g.sessions) {
            if (!sessions.contains(s.parent) || s.parent == s.id) continue;
            auto child = sessions[s.id], parent = sessions[s.parent];
            bool cyclic = false;
            for (auto p = parent; p; p = p->parent()) if (p == child) { cyclic = true; break; }
            if (!cyclic) { child->parent()->takeChild(child->parent()->indexOfChild(child)); parent->addChild(child); }
        }
        if (g.pids.size() > 1) {
            auto category = new Item(host, {"Child processes (included in Tree RSS)"});
            category->setData(0, RootRole, g.root);
            category->setData(0, KeyRole, "children:" + QString::number(g.root));
            QHash<int, QTreeWidgetItem *> rows;
            for (const int pid : g.pids) {
                if (pid == g.root) continue;
                const auto p = snapshot.processes.value(pid);
                auto i = new Item(category);
                i->setText(0, p.name); i->setData(0, RootRole, g.root); i->setData(0, ProcessRole, pid);
                i->setData(0, KeyRole, QString("p:%1:%2").arg(pid).arg(p.id.start));
                numeric(i, 1, pid, QString::number(pid)); numeric(i, 2, p.rss, bytes(p.rss));
                i->setText(3, "—"); numeric(i, 4, p.cpu, QString::number(p.cpu, 'f', 1) + "%");
                numeric(i, 5, p.age, duration(p.age)); i->setText(6, stateName(p.state));
                rows.insert(pid, i);
            }
            for (const int pid : rows.keys()) {
                const int ppid = snapshot.processes.value(pid).ppid;
                if (!rows.contains(ppid) || ppid == pid) continue;
                auto child = rows[pid], parent = rows[ppid];
                bool cyclic = false;
                for (auto p = parent; p; p = p->parent()) if (p == child) { cyclic = true; break; }
                if (!cyclic) { child->parent()->takeChild(child->parent()->indexOfChild(child)); parent->addChild(child); }
            }
        }
        host->setExpanded(identities.contains(g.root) ? expanded.contains(host->data(0, KeyRole).toString()) : true);
    }
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        auto i = *it;
        const auto k = i->data(0, KeyRole).toString();
        if (i->parent()) i->setExpanded(expanded.contains(k));
        if (!key.isEmpty() && k == key) tree->setCurrentItem(i);
        ++it;
    }
    tree->setSortingEnabled(true); tree->blockSignals(false); tree->setUpdatesEnabled(true);
    tree->verticalScrollBar()->setValue(scroll);
    filter(); showDetails(); updateButtons();
}
void Window::filter() {
    const auto query = search->text().trimmed();
    std::function<bool(QTreeWidgetItem *, bool)> visit = [&](QTreeWidgetItem *i, bool parentMatch) {
        QString searchable;
        for (int n = 0; n < tree->columnCount(); ++n) searchable += i->text(n) + ' ';
        searchable += i->toolTip(0);
        bool match = query.isEmpty() || searchable.contains(query, Qt::CaseInsensitive);
        bool descendant = false;
        for (int n = 0; n < i->childCount(); ++n) descendant = visit(i->child(n), parentMatch || match) || descendant;
        i->setHidden(!(parentMatch || match || descendant));
        return match || descendant;
    };
    for (int n = 0; n < tree->topLevelItemCount(); ++n) visit(tree->topLevelItem(n), false);
}
void Window::showDetails() {
    const auto item = tree->currentItem();
    if (!item) { details->clear(); return; }
    const int root = item->data(0, RootRole).toInt();
    const auto id = item->data(0, SessionRole).toString();
    QStringList text;
    if (!id.isEmpty()) {
        for (const auto &g : snapshot.groups) if (g.root == root) for (const auto &s : g.sessions) if (s.id == id) {
            text << "LOGICAL SESSION — not a separate OS process" << "Thread ID: " + s.id << "Parent: " + s.parent
                 << "Title: " + s.title << "Model: " + s.model << "Provider: " + s.provider << "Reasoning effort: " + s.effort
                 << "Role: " + s.role << "Nickname: " + s.nickname << "Working directory: " + s.cwd << "Git branch: " + s.branch
                 << "Codex version: " + s.version << "Source: " + s.source << QString("Recorded token usage: %1 (may be stale)").arg(s.tokens)
                 << "Evidence: " + (s.observed ? QString("Open rollout FD / process thread identity") : QString("Related database edge only; not proof of liveness"))
                 << "Status: " + s.status << "Rollout: " + s.rollout << ""
                 << QString("Memory is shared with host PID %1. Per-session RAM is not available.").arg(root)
                 << "To stop it, check the host. That also stops other sessions and child processes on that host.";
        }
    } else {
        const int pid = item->data(0, ProcessRole).toInt();
        const auto p = snapshot.processes.value(pid ? pid : root);
        text << QString("PROCESS %1 — %2").arg(p.id.pid).arg(p.name) << "Executable: " + p.id.exe
             << QString("Parent PID: %1   UID: %2   OS threads: %3").arg(p.ppid).arg(p.id.uid).arg(p.osThreads)
             << "Working directory: " + p.cwd << "Elapsed: " + duration(p.age) << "State: " + stateName(p.state)
             << "Own RSS: " + bytes(p.rss) << "Virtual address space: " + bytes(p.virtualBytes)
             << QString("CPU: %1% (100% = one fully busy core)").arg(p.cpu, 0, 'f', 1)
             << QString("Identity: PID %1 / start ticks %2").arg(p.id.pid).arg(p.id.start);
        if (!demo) {
            // Only the inspected identity gets a bounded, asynchronous PSS/USS read.
            if (!memoryReader.isRunning() && (!memoryAge.isValid() || !(memoryIdentity == p.id) || memoryAge.elapsed() > 5000)) {
                const auto identity = p.id;
                memoryReader.setFuture(QtConcurrent::run([identity] {
                    MemorySample result; result.id = identity;
                    const auto before = readProcess(identity.pid);
                    if (before && before->id == identity) result.values = memoryDetails(identity.pid);
                    const auto after = readProcess(identity.pid);
                    if (!after || !(after->id == identity)) result.values.clear();
                    return result;
                }));
            }
            const auto selected = memoryIdentity == p.id ? selectedMemory : QHash<QString, quint64>{};
            auto mem = [&](const QString &key) { return selected.contains(key) ? bytes(selected[key]) : QString("unavailable / loading"); };
            text << "" << "ON-DEMAND MEMORY BREAKDOWN (up to 5 seconds old)"
                 << "PSS (proportional shared memory): " + mem("Pss")
                 << "Private clean: " + mem("Private_Clean") << "Private dirty: " + mem("Private_Dirty")
                 << "Private huge pages: " + mem("Private_Hugetlb") << "Shared clean: " + mem("Shared_Clean")
                 << "Shared dirty: " + mem("Shared_Dirty") << "Anonymous RSS: " + mem("RssAnon")
                 << "File-backed RSS: " + mem("RssFile") << "Shared-memory RSS: " + mem("RssShmem")
                 << "Swap: " + mem("Swap") << "Peak RSS: " + mem("VmHWM");
            if (selected.contains("Private_Clean") && selected.contains("Private_Dirty"))
                text << "USS (private clean + dirty + hugetlb): " + bytes(selected.value("Private_Clean") + selected.value("Private_Dirty") + selected.value("Private_Hugetlb"));
        }
        text << "" << "Tree RSS sums unique PIDs in this host tree; shared pages can still be counted more than once."
             << "Logical sessions are NOT additional processes. No per-session memory estimate is invented.";
    }
    const auto scroll = details->verticalScrollBar()->value();
    details->setPlainText(text.join('\n'));
    details->verticalScrollBar()->setValue(scroll);
}
void Window::updateButtons() {
    const auto checked = checkedRoots();
    selection->setText(QString("%1 hosts checked").arg(checked.size()));
    stopSelected->setEnabled(!demo && !busyStop && !checked.isEmpty());
    bool any = false;
    for (const auto &g : snapshot.groups) if (!g.protectedRoot) any = true;
    stopAll->setEnabled(!demo && !busyStop && any);
}
void Window::stop(bool all) {
    if (demo || busyStop) return;
    QVector<int> roots = checkedRoots();
    if (all) { roots.clear(); for (const auto &g : snapshot.groups) roots << g.root; }
    const auto plan = makeStopPlan(snapshot, roots);
    if (plan.targets.isEmpty()) return;
    QMessageBox box(QMessageBox::Warning, "Kill Codex hosts?",
        QString("Stop %1 host(s) and %2 confirmed process(es)?").arg(plan.roots.size()).arg(plan.targets.size()), QMessageBox::NoButton, this);
    box.setInformativeText("All sessions sharing these hosts and their confirmed child processes will be affected. Unsaved work may be lost.\n\nSIGTERM first; remaining processes receive SIGKILL after 3 seconds. Newly spawned processes are not added to this operation.\n\n" +
        QString(all ? "Kill all includes hosts hidden by your filter." : "Checked hosts include any checked rows hidden by your filter."));
    box.setDetailedText(plan.affected.join('\n'));
    const auto kill = box.addButton("Kill hosts", QMessageBox::DestructiveRole);
    const auto cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel); box.setEscapeButton(cancel);
    // Freeze sampling during the modal confirmation; the identity-bound plan never expands.
    const bool wasPaused = pause->isChecked(); pause->setChecked(true);
    box.exec();
    pause->setChecked(wasPaused);
    if (box.clickedButton() != kill) return;
    busyStop = true; updateButtons();
    activity->appendPlainText(QDateTime::currentDateTime().toString(Qt::ISODate) + " — Confirmed:\n" + plan.affected.join('\n'));
    statusBar()->showMessage("Requesting exit; waiting up to 3 seconds before escalation…");
    stopper.setFuture(QtConcurrent::run([plan] { return executeStop(plan); }));
}
Snapshot demoSnapshot() {
    Snapshot s;
    s.monotonic = 1; s.totalMemory = 32ULL << 30; s.scanMs = 5.2;
    for (int n = 0; n < 2; ++n) {
        Process p;
        p.id = {4200 + n * 100, quint64(1000 + n), uint(getuid()), "/usr/local/bin/codex"};
        p.codex = true; p.name = "codex"; p.cwd = n ? "/work/api-service" : "/work/design-system";
        p.rss = quint64(n ? 112 : 248) << 20; p.cpu = n ? 0.1 : 12.4; p.age = n ? 312 : 2684;
        p.state = n ? 'S' : 'R'; p.osThreads = 12;
        s.processes.insert(p.id.pid, p);
        Group g; g.root = p.id.pid; g.pids = {g.root}; g.rss = p.rss; g.cpu = p.cpu;
        Session session;
        session.id = n ? "00000000-0000-0000-0000-000000000002" : "00000000-0000-0000-0000-000000000001";
        session.title = n ? "Review API changes" : "Build accessible components";
        session.model = "example-model"; session.cwd = p.cwd; session.observed = true;
        g.sessions << session;
        if (!n) {
            Session child; child.id = "00000000-0000-0000-0000-000000000003"; child.parent = session.id;
            child.title = "Check keyboard navigation"; child.nickname = "Accessibility reviewer"; child.role = "reviewer";
            child.model = "example-model"; child.observed = true; g.sessions << child;
            Process helper; helper.id = {4201, 1100, uint(getuid()), "/usr/bin/node"}; helper.name = "node";
            helper.ppid = 4200; helper.rss = 64ULL << 20; helper.state = 'S'; helper.age = 100;
            s.processes.insert(4201, helper); g.pids << 4201; g.rss += helper.rss;
        }
        s.groups << g;
    }
    return s;
}
