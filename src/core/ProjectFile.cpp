#include "core/ProjectFile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>

namespace
{
    QString unquote(QString value)
    {
        value = value.trimmed();
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.size() - 2);
        return value;
    }

    // Parses the first "Project { Key=Value }" block (engine text block syntax, // comments).
    bool parseProjectBlock(const QString &text, QHash<QString, QString> &out)
    {
        bool sawHeader = false, inBlock = false;
        for (QString line : text.split('\n'))
        {
            const int comment = line.indexOf("//");
            if (comment >= 0)
                line = line.left(comment);
            line = line.trimmed();
            if (line.isEmpty())
                continue;
            if (!inBlock)
            {
                if (line == "Project" || line == "Project{" || line.startsWith("Project "))
                    sawHeader = true;
                if (sawHeader && line.endsWith('{'))
                    inBlock = true;
                continue;
            }
            if (line == "}")
                return true;
            const int eq = line.indexOf('=');
            if (eq > 0)
                out.insert(line.left(eq).trimmed(), unquote(line.mid(eq + 1)));
        }
        return inBlock;
    }
}

bool KesProject::load(const QString &path, KesProject &out, QString *error)
{
    out = {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = QObject::tr("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }

    QHash<QString, QString> values;
    if (!parseProjectBlock(QTextStream(&file).readAll(), values))
    {
        if (error)
            *error = QObject::tr("%1 has no Project { ... } block.").arg(path);
        return false;
    }

    const QFileInfo info(path);
    const QDir projectDir = info.absoluteDir();
    out.filePath = info.absoluteFilePath();
    out.name = values.value("Name", info.completeBaseName());
    out.startupMap = values.value("StartupMap");
    out.assetRoot = QDir::cleanPath(projectDir.absoluteFilePath(values.value("AssetRoot", "content")));

    const QDir root(out.assetRoot);
    out.mapsDir = QDir::cleanPath(root.absoluteFilePath("maps"));
    out.materialsDir = QDir::cleanPath(root.absoluteFilePath("materials"));
    out.texturesDir = QDir::cleanPath(root.absoluteFilePath("textures"));

    if (!root.exists())
    {
        if (error)
            *error = QObject::tr("AssetRoot '%1' of %2 does not exist.").arg(out.assetRoot, path);
        out = {};
        return false;
    }
    return true;
}

QString KesProject::findUpwards(const QString &startDir)
{
    QDir dir(startDir);
    for (int depth = 0; depth < 32; ++depth)
    {
        const QStringList found = dir.entryList({"*.kproject"}, QDir::Files, QDir::Name);
        if (!found.isEmpty())
            return dir.absoluteFilePath(found.first());
        if (!dir.cdUp())
            break;
    }
    return {};
}
