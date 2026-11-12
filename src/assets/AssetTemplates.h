#pragma once
#include <QList>
#include <QString>

class EditorContext;
class QWidget;

// "New asset" templates shown in Assets > New and the Asset Browser. Edit them in AssetTemplates.cpp.
struct AssetTemplate
{
    QString label;
    QString suffix;
    QString folder;
    QString text;                   // file contents, {name} = asset name; empty for maps
    bool openAfterCreate = false;
};

const QList<AssetTemplate> &assetTemplates();

// Asks for a name and writes the asset into `folder` (absolute; "" = the template's folder).
QString createAsset(EditorContext *ctx, QWidget *parent, const AssetTemplate &tmpl, const QString &folder = {});
