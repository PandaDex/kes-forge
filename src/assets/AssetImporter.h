#pragma once
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QStringList>

class EditorContext;
class QProcess;
class QWidget;

class AssetImporter : public QObject
{
    Q_OBJECT
public:
    AssetImporter(EditorContext *ctx, QWidget *dialogParent);

    static bool canImport(const QString &path);
    static QString dialogFilter();

    // destFolder: absolute folder for every file, "" = each rule's default folder.
    void import(const QStringList &files, const QString &destFolder = {});
    void importDialog(const QString &destFolder = {});

    bool busy() const { return m_process != nullptr; }

signals:
    void imported(const QStringList &outputs);

private:
    struct Job
    {
        QString input;
        QString output;
    };
    void startNext();
    void finishJob(int exitCode, bool crashed);

    EditorContext *m_ctx;
    QWidget *m_parent;
    QList<Job> m_queue;
    Job m_current;
    QDateTime m_started;
    QProcess *m_process = nullptr;
    QString m_program;
    QStringList m_outputs;
    bool m_texturesChanged = false;
};
