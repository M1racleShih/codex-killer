#pragma once
#include <QColor>
#include <QPalette>

// Small semantic color set. No images, theme engines, animation timers or effects.
namespace appearance {
struct Colors {
    bool dark;
    QColor canvas, surface, text, muted, border, accent, memory, warning, danger;
    QColor header, headerText, headerMuted, selection, selectionText, dangerFill;
};
inline Colors colors(bool dark) {
    if (dark) return {true, "#17212b", "#202e3b", "#e5edf4", "#b2c3d0", "#425767",
        "#70d6ca", "#9bb3ff", "#f0c274", "#ff9ba8", "#163642", "#f2fbfb", "#b9d9dd",
        "#264d58", "#efffff", "#ad394d"};
    return {false, "#f3f7fa", "#ffffff", "#243547", "#536779", "#c8d5df",
        "#126b70", "#435db5", "#945008", "#b43b48", "#173e4c", "#f2fbfb", "#b9d9dd",
        "#d7edeb", "#184650", "#b43b48"};
}
inline Colors colors(const QPalette &p) { return colors(p.color(QPalette::Window).lightness() < 128); }
inline QColor mix(const QColor &ink, const QColor &paper, double amount) {
    return QColor::fromRgbF(ink.redF() * amount + paper.redF() * (1 - amount),
                            ink.greenF() * amount + paper.greenF() * (1 - amount),
                            ink.blueF() * amount + paper.blueF() * (1 - amount));
}
inline QPalette palette(bool dark) {
    const auto c = colors(dark);
    QPalette p;
    p.setColor(QPalette::Window, c.canvas); p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.surface); p.setColor(QPalette::AlternateBase, mix(c.accent, c.surface, 0.035));
    p.setColor(QPalette::Text, c.text); p.setColor(QPalette::Button, c.surface);
    p.setColor(QPalette::ButtonText, c.text); p.setColor(QPalette::Highlight, c.selection);
    p.setColor(QPalette::HighlightedText, c.selectionText); p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Light, c.surface); p.setColor(QPalette::Dark, c.border);
    p.setColor(QPalette::ToolTipBase, c.surface); p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::Link, c.accent); p.setColor(QPalette::PlaceholderText, c.muted);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.muted);
    return p;
}
inline QColor semantic(const Colors &c, const QString &role) {
    if (role == "running" || role == "observed") return c.accent;
    if (role == "related" || role == "warning") return c.warning;
    if (role == "danger") return c.danger;
    if (role == "sleeping") return c.memory;
    return c.muted;
}
}
