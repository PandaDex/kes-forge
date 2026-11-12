#pragma once
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// Editable tables live in AssetTypeMap.cpp.

extern const QMap<QString, QString> kAssetEditors;

extern const QStringList kToolSearchDirs;

// The engine's import tool (engine/tools/model-importer): model-importer <input> <output>.
extern const QString kImporterProgram;

struct ImportRule
{
    QString label;
    QStringList sources;
    QString output;
    QString folder;         // default folder under the content root
};
extern const QList<ImportRule> kImportRules;
