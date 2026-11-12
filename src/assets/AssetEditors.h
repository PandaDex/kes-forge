#pragma once
#include <QString>
#include <QStringList>

class EditorContext;
class QWidget;

namespace AssetEditors
{
    enum class OpenResult
    {
        Launched,
        OpenInForge,
        NoEditor,
        Failed
    };

    QString editorFor(const QString &path);
    QString editorLabel(const QString &path);

    OpenResult open(EditorContext *ctx, QWidget *parent, const QString &path);

    // Full path of a program named in the tables. Order: remembered location, absolute path,
    // kToolSearchDirs (against the .kproject folder, then forge.exe), forge.exe's folder, PATH.
    // With `parent` set, asks the user to locate it when not found and remembers the answer.
    QString findProgram(EditorContext *ctx, const QString &name, QWidget *parent = nullptr);

    QStringList programs();
    bool launch(EditorContext *ctx, QWidget *parent, const QString &program, const QStringList &args = {});
}
