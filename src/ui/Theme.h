#pragma once
#include <QIcon>
#include <QString>

class QApplication;

namespace Theme
{
    void apply(QApplication &app);
    QIcon toolIcon(const QString &toolId);
    extern const char *kAccent;
}
