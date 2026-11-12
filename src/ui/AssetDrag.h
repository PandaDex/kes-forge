#pragma once
#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QString>
#include <QStringList>
#include <QUrl>

// Content-relative path ("materials/brick.mat") of the first dragged file that lies inside
// `contentDir` and has one of `suffixes` (any suffix when empty). "" when nothing matches.
inline QString draggedAssetPath(const QMimeData *mime, const QString &contentDir, const QStringList &suffixes = {})
{
    if (!mime || !mime->hasUrls() || contentDir.isEmpty())
        return {};
    const QDir root(contentDir);
    for (const QUrl &url : mime->urls())
    {
        if (!url.isLocalFile())
            continue;
        const QFileInfo info(url.toLocalFile());
        if (!info.isFile())
            continue;
        if (!suffixes.isEmpty() && !suffixes.contains(info.suffix(), Qt::CaseInsensitive))
            continue;
        const QString relative = root.relativeFilePath(info.absoluteFilePath()).replace('\\', '/');
        if (relative.startsWith("../") || QDir::isAbsolutePath(relative))
            continue;
        return relative;
    }
    return {};
}
