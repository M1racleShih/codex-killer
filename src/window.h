#pragma once
#include "core.h"
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include <QTreeWidget>
#include <QWidget>
class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QCheckBox;

class MemoryChart : public QWidget {
public:
    explicit MemoryChart(QWidget *parent = nullptr);
    void add(double time, quint64 value);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QVector<QPair<double, quint64>> points;
};
class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(QString home, bool demo = false);
    ~Window() override;
    void applySnapshot(const scope::Snapshot &snapshot);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void applyAppearance();
    void refresh();
    void rebuild();
    void filter();
    void showDetails();
    void updateButtons();
    void stop(bool all);
    QVector<int> checkedRoots() const;
    QString home;
    bool demo = false, busyStop = false;
    scope::Snapshot snapshot;
    struct MemorySample { scope::Identity id; QHash<QString, quint64> values; };
    QHash<QString, quint64> selectedMemory;
    scope::Identity memoryIdentity;
    QElapsedTimer memoryAge;
    QFutureWatcher<MemorySample> memoryReader;
    QTreeWidget *tree;
    QLineEdit *search;
    QLabel *summary, *notice, *selection;
    QPushButton *stopSelected, *stopAll;
    QPlainTextEdit *details, *activity;
    QCheckBox *pause;
    MemoryChart *chart;
    QTimer timer;
    QFutureWatcher<scope::Snapshot> scanner;
    QFutureWatcher<scope::StopResult> stopper;
};
scope::Snapshot demoSnapshot();
