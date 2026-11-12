#include "assets/AssetImporter.h"
#include "assets/AssetEditors.h"
#include "assets/AssetTypeMap.h"
#include "core/EditorContext.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <algorithm>

namespace
{
    const ImportRule *ruleFor(const QString &path)
    {
        const QString suffix = QFileInfo(path).suffix().toLower();
        for (const ImportRule &rule : kImportRules)
            if (rule.sources.contains(suffix))
                return &rule;
        return nullptr;
    }

    QString patterns(const QStringList &suffixes)
    {
        QStringList out;
        for (const QString &s : suffixes)
            out << "*." + s;
        return out.join(' ');
    }
}

AssetImporter::AssetImporter(EditorContext *ctx, QWidget *dialogParent)
    : QObject(dialogParent), m_ctx(ctx), m_parent(dialogParent)
{
}

bool AssetImporter::canImport(const QString &path)
{
    return ruleFor(path) != nullptr;
}

QString AssetImporter::dialogFilter()
{
    QStringList all;
    QStringList filters;
    for (const ImportRule &rule : kImportRules)
    {
        all << rule.sources;
        filters << QString("%1 (%2)").arg(rule.label, patterns(rule.sources));
    }
    filters.prepend(tr("All importable files (%1)").arg(patterns(all)));
    return filters.join(";;");
}

void AssetImporter::importDialog(const QString &destFolder)
{
    QSettings settings;
    const QStringList files = QFileDialog::getOpenFileNames(m_parent, tr("Import Assets"),
                                                            settings.value("import/lastDir").toString(), dialogFilter());
    if (files.isEmpty())
        return;
    settings.setValue("import/lastDir", QFileInfo(files.first()).absolutePath());
    import(files, destFolder);
}

void AssetImporter::import(const QStringList &files, const QString &destFolder)
{
    const QString content = m_ctx->projectDir();
    if (content.isEmpty())
    {
        m_ctx->log(tr("Open a project before importing assets."), LogLevel::Warning);
        return;
    }

    QList<Job> jobs;
    QStringList existing;
    for (const QString &file : files)
    {
        const ImportRule *rule = ruleFor(file);
        if (!rule)
        {
            m_ctx->log(tr("Can't import %1: unsupported file type.").arg(QFileInfo(file).fileName()), LogLevel::Warning);
            continue;
        }
        const QString folder = destFolder.isEmpty() ? QDir(content).absoluteFilePath(rule->folder) : destFolder;
        QDir().mkpath(folder);
        Job job;
        job.input = QFileInfo(file).absoluteFilePath();
        job.output = QDir(folder).absoluteFilePath(QFileInfo(file).completeBaseName() + "." + rule->output);
        if (QFileInfo::exists(job.output))
            existing << QDir(content).relativeFilePath(job.output);
        jobs << job;
    }
    if (jobs.isEmpty())
        return;

    if (!existing.isEmpty())
    {
        const auto answer = QMessageBox::question(m_parent, tr("Overwrite assets"),
                                                  tr("These assets already exist:\n\n%1\n\nOverwrite them? \"No\" skips them.").arg(existing.join('\n')),
                                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel)
            return;
        if (answer == QMessageBox::No)
            jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [](const Job &j)
                                      { return QFileInfo::exists(j.output); }),
                       jobs.end());
    }

    m_queue << jobs;
    if (!busy())
        startNext();
}

void AssetImporter::startNext()
{
    if (m_queue.isEmpty())
    {
        if (m_texturesChanged)
            m_ctx->materials()->reload();
        if (!m_outputs.isEmpty())
            emit imported(m_outputs);
        m_outputs.clear();
        m_texturesChanged = false;
        m_program.clear();
        return;
    }

    if (m_program.isEmpty())
    {
        m_program = AssetEditors::findProgram(m_ctx, kImporterProgram, m_parent);
        if (m_program.isEmpty())
        {
            m_ctx->log(tr("Import cancelled: %1 was not found. Build the engine's \"%1\" target, or set it in kToolSearchDirs.")
                           .arg(kImporterProgram),
                       LogLevel::Error);
            m_queue.clear();
            return;
        }
    }

    m_current = m_queue.takeFirst();
    m_started = QDateTime::currentDateTime();
    m_ctx->log(tr("Importing %1 ...").arg(QFileInfo(m_current.input).fileName()));

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    // FBX import runs FBX2glTF, which the importer looks for next to itself.
    m_process->setWorkingDirectory(QFileInfo(m_program).absolutePath());
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status)
            { finishJob(code, status == QProcess::CrashExit); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error)
            {
        if (error == QProcess::FailedToStart)
            finishJob(-1, true); });
    m_process->start(m_program, {QDir::toNativeSeparators(m_current.input), QDir::toNativeSeparators(m_current.output)});
}

void AssetImporter::finishJob(int exitCode, bool crashed)
{
    if (!m_process)
        return;
    QProcess *process = m_process;
    m_process = nullptr;
    const QString output = QString::fromLocal8Bit(process->readAll()).trimmed();
    const QString startError = process->error() == QProcess::FailedToStart ? process->errorString() : QString();
    process->deleteLater();

    const QString content = m_ctx->projectDir();
    const bool ok = exitCode == 0 && !crashed && QFileInfo::exists(m_current.output);
    if (ok)
    {
        m_outputs << m_current.output;
        m_ctx->log(tr("Imported %1").arg(QDir(content).relativeFilePath(m_current.output)));
        if (m_current.output.endsWith(".ktex", Qt::CaseInsensitive))
            m_texturesChanged = true;

        const QFileInfo model(m_current.output);
        if (model.suffix().compare("kmodel", Qt::CaseInsensitive) == 0)
        {
            const QFileInfoList clips = model.dir().entryInfoList({model.completeBaseName() + "_*.kanim"}, QDir::Files);
            for (const QFileInfo &clip : clips)
                if (clip.lastModified() >= m_started.addSecs(-2))
                {
                    m_outputs << clip.absoluteFilePath();
                    m_ctx->log(tr("  animation %1").arg(QDir(content).relativeFilePath(clip.absoluteFilePath())));
                }
        }
    }
    else
    {
        QString reason = !startError.isEmpty() ? startError : output;
        if (reason.isEmpty())
            reason = crashed ? tr("the importer crashed") : tr("exit code %1").arg(exitCode);
        m_ctx->log(tr("Import of %1 failed: %2").arg(QFileInfo(m_current.input).fileName(), reason), LogLevel::Error);
    }
    startNext();
}
