#include "assets/AssetEditors.h"
#include "assets/AssetTypeMap.h"
#include "core/EditorContext.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace
{
    QString settingsKey(const QString &name) { return "assetEditors/" + name; }

    QStringList baseDirs(EditorContext *ctx)
    {
        QStringList bases;
        if (ctx && ctx->project().isValid())
            bases << QFileInfo(ctx->project().filePath).absolutePath();
        bases << QCoreApplication::applicationDirPath();
        return bases;
    }

    QStringList searchDirs(EditorContext *ctx)
    {
        QStringList dirs;
        for (const QString &base : baseDirs(ctx))
            for (const QString &relative : kToolSearchDirs)
                dirs << QDir::cleanPath(QDir(base).absoluteFilePath(relative));
        dirs.removeDuplicates();
        return dirs;
    }

    QStringList alternatives(const QString &value)
    {
        QStringList out;
        for (const QString &alt : value.split('|', Qt::SkipEmptyParts))
            if (!alt.trimmed().isEmpty())
                out << alt.trimmed();
        return out;
    }

    QString expand(QString arg, EditorContext *ctx, const QString &file)
    {
        arg.replace("{file}", QDir::toNativeSeparators(file));
        arg.replace("{content}", QDir::toNativeSeparators(ctx->projectDir()));
        if (arg.contains("{root}") && !ctx->projectDir().isEmpty())
            arg.replace("{root}", QDir::toNativeSeparators(QDir::cleanPath(ctx->projectDir() + "/..")));
        arg.replace("{project}", QDir::toNativeSeparators(ctx->project().filePath));
        return arg;
    }

    bool startDetached(const QString &program, const QStringList &args, QString *error)
    {
        QProcess process;
        process.setWorkingDirectory(QFileInfo(program).absolutePath());
#ifdef Q_OS_WIN
        // Batch files (VS Code's "code" is code.cmd) need cmd.exe; /s keeps the quoting intact.
        const QString suffix = QFileInfo(program).suffix().toLower();
        if (suffix == "cmd" || suffix == "bat")
        {
            QString line = "/d /s /c \"\"" + QDir::toNativeSeparators(program) + "\"";
            for (const QString &a : args)
                line += " \"" + a + "\"";
            line += "\"";
            process.setProgram(qEnvironmentVariable("ComSpec", "cmd.exe"));
            process.setNativeArguments(line);
            process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a)
                                                      { a->flags |= 0x08000000; /* CREATE_NO_WINDOW */ });
        }
        else
#endif
        {
            process.setProgram(program);
            process.setArguments(args);
        }
        if (process.startDetached())
            return true;
        if (error)
            *error = process.errorString();
        return false;
    }
}

namespace AssetEditors
{
    QString editorFor(const QString &path)
    {
        return kAssetEditors.value(QFileInfo(path).suffix().toLower());
    }

    QString editorLabel(const QString &path)
    {
        const QStringList alts = alternatives(editorFor(path));
        if (alts.isEmpty())
            return {};
        if (alts.first() == "@forge")
            return QStringLiteral("Forge");
        if (alts.first() == "@system")
            return QCoreApplication::translate("AssetEditors", "default app");
        const QStringList parts = QProcess::splitCommand(alts.first());
        return parts.isEmpty() ? QString() : QFileInfo(parts.first()).completeBaseName();
    }

    QString findProgram(EditorContext *ctx, const QString &name, QWidget *parent)
    {
        QSettings settings;
        const QString remembered = settings.value(settingsKey(name)).toString();
        if (!remembered.isEmpty() && QFileInfo(remembered).isFile())
            return remembered;

        if (QDir::isAbsolutePath(name))
        {
            if (QFileInfo(name).isFile())
                return name;
        }
        else if (name.contains('/') || name.contains('\\'))
        {
            for (const QString &base : baseDirs(ctx))
            {
                const QString candidate = QDir::cleanPath(QDir(base).absoluteFilePath(name));
                if (QFileInfo(candidate).isFile())
                    return candidate;
            }
        }
        else
        {
            QString found = QStandardPaths::findExecutable(name, searchDirs(ctx));
            if (found.isEmpty())
                found = QStandardPaths::findExecutable(name);
            if (!found.isEmpty())
                return found;
        }

        if (!parent)
            return {};
        const auto answer = QMessageBox::question(parent, QObject::tr("Program not found"),
                                                  QObject::tr("Forge could not find \"%1\".\n\nLocate it now? Forge remembers the choice.").arg(name));
        if (answer != QMessageBox::Yes)
            return {};
#ifdef Q_OS_WIN
        const QString filter = QObject::tr("Programs (*.exe *.cmd *.bat);;All files (*)");
#else
        const QString filter;
#endif
        const QString picked = QFileDialog::getOpenFileName(parent, QObject::tr("Locate %1").arg(name), QString(), filter);
        if (picked.isEmpty())
            return {};
        settings.setValue(settingsKey(name), picked);
        return picked;
    }

    OpenResult open(EditorContext *ctx, QWidget *parent, const QString &path)
    {
        const QString suffix = QFileInfo(path).suffix().toLower();
        const QStringList alts = alternatives(editorFor(path));
        if (alts.isEmpty())
        {
            ctx->log(QObject::tr("No editor for .%1 files. Add one to kAssetEditors in src/assets/AssetTypeMap.cpp.").arg(suffix),
                     LogLevel::Warning);
            return OpenResult::NoEditor;
        }

        QString firstProgram;
        for (const QString &alt : alts)
        {
            if (alt == "@forge")
                return OpenResult::OpenInForge;
            if (alt == "@system")
            {
                if (QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
                    return OpenResult::Launched;
                continue;
            }
            const QStringList parts = QProcess::splitCommand(alt);
            if (parts.isEmpty())
                continue;
            if (firstProgram.isEmpty())
                firstProgram = parts.first();
            const QString program = findProgram(ctx, parts.first());
            if (program.isEmpty())
                continue;

            QStringList args;
            for (int i = 1; i < parts.size(); ++i)
                args << expand(parts[i], ctx, path);
            if (!alt.contains("{file}"))
                args << QDir::toNativeSeparators(path);
            QString error;
            if (startDetached(program, args, &error))
            {
                ctx->log(QObject::tr("Opened %1 in %2").arg(QFileInfo(path).fileName(), QFileInfo(program).completeBaseName()));
                return OpenResult::Launched;
            }
            ctx->log(QObject::tr("Could not start %1: %2").arg(program, error), LogLevel::Error);
            return OpenResult::Failed;
        }

        // Nothing found on its own: let the user point at the first program in the list.
        if (!firstProgram.isEmpty())
        {
            const QString program = findProgram(ctx, firstProgram, parent);
            if (!program.isEmpty())
                return open(ctx, parent, path);
        }
        ctx->log(QObject::tr("No program for .%1 files was found (%2).").arg(suffix, editorFor(path)), LogLevel::Error);
        return OpenResult::Failed;
    }

    QStringList programs()
    {
        QStringList out;
        for (const QString &value : kAssetEditors)
            for (const QString &alt : alternatives(value))
            {
                if (alt.startsWith('@'))
                    continue;
                const QStringList parts = QProcess::splitCommand(alt);
                if (!parts.isEmpty() && !out.contains(parts.first()))
                    out << parts.first();
                break;  // later alternatives are fallbacks, not separate tools
            }
        out.sort(Qt::CaseInsensitive);
        return out;
    }

    bool launch(EditorContext *ctx, QWidget *parent, const QString &program, const QStringList &args)
    {
        const QString path = findProgram(ctx, program, parent);
        if (path.isEmpty())
            return false;
        QString error;
        if (startDetached(path, args, &error))
            return true;
        ctx->log(QObject::tr("Could not start %1: %2").arg(path, error), LogLevel::Error);
        return false;
    }
}
