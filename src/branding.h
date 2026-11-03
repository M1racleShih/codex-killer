#pragma once
#include <QIcon>

namespace branding {
inline constexpr const char *desktopId = "io.github.M1racleShih.CodexKiller";
inline QIcon icon() {
    QIcon result;
    // Embedded PNGs work from any cwd, without QtSvg or desktop icon-theme lookups.
    for (const int size : {16, 24, 32, 48, 64, 128, 256})
        result.addFile(QString(":/branding/icon-%1.png").arg(size), QSize(size, size));
    return result;
}
}
