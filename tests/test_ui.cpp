#include "window.h"
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
private slots:
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
    void confirmationCanBeCancelled() {
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
            sawConfirmation = dialog->text().contains("host(s)") && dialog->informativeText().contains("SIGTERM");
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
