#include "window.h"
#include "appearance.h"
#include <cmath>
#include <QtTest>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

class UiTests : public QObject {
    Q_OBJECT
    QPalette originalPalette;
    static double luminance(const QColor &c) {
        auto linear = [](double n) { return n <= 0.04045 ? n / 12.92 : std::pow((n + 0.055) / 1.055, 2.4); };
        return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
    }
    static double contrast(const QColor &a, const QColor &b) {
        const double x = luminance(a), y = luminance(b);
        return (qMax(x, y) + 0.05) / (qMin(x, y) + 0.05);
    }
private slots:
    void initTestCase() { originalPalette = QApplication::palette(); }
    void cleanup() { QApplication::setPalette(originalPalette); }
    void semanticColorsRemainReadable() {
        for (const bool dark : {false, true}) {
            const auto c = appearance::colors(dark);
            const QVector<QPair<QColor, QColor>> pairs{
                {c.text, c.surface}, {c.muted, c.surface}, {c.muted, c.canvas},
                {c.accent, c.surface}, {c.memory, c.surface}, {c.warning, c.surface}, {c.danger, c.surface},
                {c.headerText, c.header}, {c.headerMuted, c.header}, {c.selectionText, c.selection},
                {Qt::white, c.dangerFill},
                {c.memory, appearance::mix(c.accent, c.surface, dark ? 0.13 : 0.065)}
            };
            for (const auto &pair : pairs) {
                const auto ratio = contrast(pair.first, pair.second);
                QVERIFY2(ratio >= 4.5, qPrintable(QString("%1 on %2: %3:1").arg(pair.first.name(), pair.second.name()).arg(ratio)));
            }
            for (const auto &ink : {c.accent, c.memory, c.warning, c.danger, c.muted}) {
                const auto fill = appearance::mix(ink, c.surface, dark ? 0.14 : 0.08);
                QVERIFY2(contrast(ink, fill) >= 4.5, qPrintable("Unreadable badge color: " + ink.name()));
            }
        }
    }
    void followsLiveSystemPaletteChanges() {
        QApplication::setPalette(appearance::palette(false));
        Window w("/nonexistent", true); w.show();
        const auto tree = w.findChild<QTreeWidget *>("processTree");
        const auto key = tree->currentItem()->data(0, Qt::UserRole).toInt();
        for (const bool dark : {true, false}) {
            QApplication::setPalette(appearance::palette(dark));
            QApplication::processEvents();
            QCOMPARE(w.palette().color(QPalette::Window), appearance::colors(dark).canvas);
            const auto fieldPalette = w.findChild<QLineEdit *>("search")->palette();
            QVERIFY(contrast(fieldPalette.color(QPalette::PlaceholderText), fieldPalette.color(QPalette::Base)) >= 4.5);
            QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toInt(), key);
            QVERIFY(w.findChild<QLabel *>("summary")->text().contains(appearance::colors(dark).memory.name()));
            QVERIFY(!w.findChild<QPushButton *>("killAll")->isEnabled());
        }
    }
    void compactLayoutKeepsActionsReachable() {
        Window w("/nonexistent", true); w.resize(780, 600); w.show();
        QApplication::processEvents();
        QCOMPARE(w.size(), QSize(780, 600));
        for (const auto &name : {"search", "killAll", "killSelected"}) {
            const auto control = w.findChild<QWidget *>(name); QVERIFY(control);
            const QRect bounds(control->mapTo(&w, QPoint()), control->size());
            QVERIFY2(w.rect().contains(bounds), name);
        }
    }
    void demoIsSafeAndFilterWorks() {
        Window w("/nonexistent", true); w.show();
        auto tree = w.findChild<QTreeWidget *>("processTree"); QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 2);
        QVERIFY(!w.findChild<QPushButton *>("killAll")->isEnabled());
        QVERIFY(!w.findChild<QPushButton *>("killSelected")->isEnabled());
        auto search = w.findChild<QLineEdit *>("search");
        search->setText("api-service");
        int visible = 0;
        for (int n = 0; n < tree->topLevelItemCount(); ++n) visible += !tree->topLevelItem(n)->isHidden();
        QCOMPARE(visible, 1);
        search->clear();
        tree->setCurrentItem(tree->topLevelItem(0));
        QVERIFY(w.findChild<QPlainTextEdit *>("details")->toPlainText().contains("Own RSS"));
    }
    void selectionSurvivesRefreshButNotPidReuse() {
        Window w("/nonexistent");
        w.findChild<QCheckBox *>()->setChecked(true);
        QTest::qWait(300); // Finish the one initial asynchronous scan before injecting synthetic snapshots.
        auto s = demoSnapshot();
        w.applySnapshot(s);
        auto tree = w.findChild<QTreeWidget *>("processTree");
        auto host = tree->topLevelItem(0); host->setCheckState(0, Qt::Checked);
        QVERIFY(w.findChild<QPushButton *>("killSelected")->isEnabled());
        const auto pid = host->data(0, Qt::UserRole).toInt();
        w.applySnapshot(s);
        int checked = 0;
        for (int n = 0; n < tree->topLevelItemCount(); ++n) checked += tree->topLevelItem(n)->checkState(0) == Qt::Checked;
        QCOMPARE(checked, 1);
        ++s.processes[pid].id.start;
        w.applySnapshot(s);
        QVERIFY(!w.findChild<QPushButton *>("killSelected")->isEnabled());
    }
    void confirmationCanBeCancelled_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void confirmationCanBeCancelled() {
        QFETCH(bool, dark);
        QApplication::setPalette(appearance::palette(dark));
        Window w("/nonexistent");
        w.findChild<QCheckBox *>()->setChecked(true);
        QTest::qWait(300);
        w.applySnapshot(demoSnapshot());
        auto tree = w.findChild<QTreeWidget *>("processTree");
        tree->topLevelItem(0)->setCheckState(0, Qt::Checked);
        bool sawConfirmation = false;
        QTimer::singleShot(50, &w, [&] {
            auto dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!dialog) return;
            sawConfirmation = dialog->text().contains("host(s)") && dialog->informativeText().contains("SIGTERM") &&
                dialog->defaultButton() == dialog->button(QMessageBox::Cancel) && dialog->findChild<QPushButton *>("confirmKill");
            dialog->button(QMessageBox::Cancel)->click();
        });
        w.findChild<QPushButton *>("killSelected")->click();
        QVERIFY(sawConfirmation);
        QVERIFY(w.findChild<QPushButton *>("killSelected")->isEnabled());
        QVERIFY(w.findChild<QPlainTextEdit *>("details"));
    }
};
QTEST_MAIN(UiTests)
#include "test_ui.moc"
