#pragma once
#include <QString>

// .kproject, the same file the engine reads (engine/project_file.h). Map asset paths are
// relative to AssetRoot; maps, materials and textures live in fixed folders under it.
struct KesProject
{
    QString filePath;      // absolute path of the .kproject ("" = no project)
    QString name;
    QString assetRoot;
    QString startupMap;    // content-relative, as written in the file
    QString mapsDir;
    QString materialsDir;
    QString texturesDir;

    bool isValid() const { return !filePath.isEmpty(); }

    static bool load(const QString &path, KesProject &out, QString *error = nullptr);
    static QString findUpwards(const QString &startDir);
};
