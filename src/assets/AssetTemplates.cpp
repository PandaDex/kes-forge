#include "assets/AssetTemplates.h"
#include "core/EditorContext.h"
#include "core/MapDocument.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>

namespace
{
    const char *kMaterial = R"(Material {
    BaseColor = "1,1,1,1"
    Emissive = "0,0,0"
    Metallic = 0
    Roughness = 0.8
    AlphaCutoff = 0
    Lit = true
    Transparent = false
    DoubleSided = false
    CastShadows = true
}
)";

    const char *kUiPanel = R"(<rml>
<head>
    <title>{name}</title>
    <link type="text/rcss" href="/engine/ui/kes.rcss"/>
    <style>
        body
        {
            display: flex;
            justify-content: center;
            align-items: center;
        }
        .window
        {
            width: 380dp;
            padding: 20dp;
            background-color: #1b1e26f0;
            border: 1dp #5a6070;
            border-radius: 6dp;
        }
        .window h2 { margin: 0 0 6dp 0; color: #e0a040; }
        .window p { margin: 0 0 12dp 0; color: #ffffffb0; }
        .row
        {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10dp;
        }
        .bar { height: 8dp; background-color: #2a2f3a; border-radius: 4dp; }
        .bar .fill { width: 65%; height: 100%; background-color: #e0a040; border-radius: 4dp; }
        .buttons { display: flex; justify-content: flex-end; margin-top: 16dp; }
        .buttons button { margin-left: 8dp; }
    </style>
</head>
<!-- onclick values are UI actions registered in C++ (UiSystem::RegisterAction). -->
<body>
    <div class="window">
        <h2>{name}</h2>
        <p>Sample panel. Replace this text and the controls below with your own content.</p>
        <div class="row">
            <span>Enable option</span>
            <input type="checkbox" name="option" checked/>
        </div>
        <div class="row">
            <span>Volume</span>
            <input type="range" name="volume" min="0" max="100" step="1" value="65"/>
        </div>
        <div class="row">
            <span>Name</span>
            <input type="text" name="player_name" value="Player"/>
        </div>
        <div class="bar"><div class="fill"></div></div>
        <div class="buttons">
            <button onclick="resume">Cancel</button>
            <button onclick="resume">OK</button>
        </div>
    </div>
</body>
</rml>
)";

    const char *kUiMenu = R"(<rml>
<head>
    <title>{name}</title>
    <link type="text/rcss" href="/engine/ui/kes.rcss"/>
    <style>
        body
        {
            display: flex;
            justify-content: center;
            align-items: center;
            background-color: #000000a0;
        }
        .menu
        {
            display: flex;
            flex-direction: column;
            width: 300dp;
            padding: 24dp;
            background-color: #1b1e26f0;
            border: 1dp #5a6070;
            border-radius: 6dp;
        }
        .menu h1 { margin: 0 0 8dp 0; text-align: center; color: #e0a040; }
        .menu button { display: block; margin-top: 10dp; padding: 10dp 14dp; }
        .menu .footer { margin-top: 16dp; font-size: 12dp; text-align: center; color: #ffffff70; }
    </style>
</head>
<!-- onclick values are UI actions registered in C++ (UiSystem::RegisterAction). -->
<body>
    <div class="menu">
        <h1>{name}</h1>
        <button onclick="resume">Continue</button>
        <button onclick="toggle_fullscreen">Toggle fullscreen</button>
        <button onclick="quit">Quit</button>
        <div class="footer">Sample menu</div>
    </div>
</body>
</rml>
)";

    const char *kUiHud = R"(<rml>
<head>
    <title>{name}</title>
    <link type="text/rcss" href="/engine/ui/kes.rcss"/>
    <style>
        body { font-effect: shadow(1dp 1dp #000000c0); }
        #health
        {
            position: absolute;
            left: 16dp;
            bottom: 16dp;
            width: 220dp;
        }
        #health .label { font-size: 13dp; color: #ffffffc0; }
        #health .bar { height: 10dp; margin-top: 4dp; background-color: #00000080; border: 1dp #ffffff40; }
        #health .fill { width: 80%; height: 100%; background-color: #d04040; }
        #ammo
        {
            position: absolute;
            right: 16dp;
            bottom: 16dp;
            font-size: 28dp;
            color: #ffffffe0;
        }
        #crosshair
        {
            position: absolute;
            left: 50%;
            top: 50%;
            width: 4dp;
            height: 4dp;
            margin: -2dp 0 0 -2dp;
            background-color: #ffffffd0;
            border-radius: 2dp;
        }
    </style>
</head>
<!-- Bind live values with a data model, e.g. <body data-model="hud"> and {{ health }}. -->
<body>
    <div id="health">
        <div class="label">Health 80</div>
        <div class="bar"><div class="fill"></div></div>
    </div>
    <div id="ammo">30 / 90</div>
    <div id="crosshair"></div>
</body>
</rml>
)";

    const char *kUiStyle = R"(/* {name}: link it from a document with
   <link type="text/rcss" href="{name}.rcss"/> */

.panel
{
    padding: 16dp;
    background-color: #1b1e26f0;
    border: 1dp #5a6070;
    border-radius: 6dp;
}

.panel h2
{
    margin: 0 0 8dp 0;
    color: #e0a040;
}
)";

    QString sanitizeName(QString name)
    {
        name = name.trimmed();
        name.replace(QRegularExpression(R"([\\/:*?"<>|\s]+)"), "_");
        return name;
    }
}

const QList<AssetTemplate> &assetTemplates()
{
    static const QList<AssetTemplate> templates = {
        {"Material", "mat", "materials", kMaterial, false},
        {"Material (Shader Graph)", "mat", "materials", kMaterial, true},
        {"UI Panel", "rml", "ui", kUiPanel, false},
        {"UI Menu", "rml", "ui", kUiMenu, false},
        {"UI HUD", "rml", "ui", kUiHud, false},
        {"UI Stylesheet", "rcss", "ui", kUiStyle, false},
        {"Map", "fmap", "maps", {}, false},
    };
    return templates;
}

QString createAsset(EditorContext *ctx, QWidget *parent, const AssetTemplate &tmpl, const QString &folder)
{
    const QString content = ctx->projectDir();
    if (content.isEmpty())
    {
        ctx->log(QObject::tr("Open a project before creating assets."), LogLevel::Warning);
        return {};
    }
    const QString dir = folder.isEmpty() ? QDir(content).absoluteFilePath(tmpl.folder) : folder;

    QString suggestion = "new_" + sanitizeName(tmpl.label.section('(', 0, 0).toLower());
    while (true)
    {
        bool ok = false;
        const QString name = sanitizeName(QInputDialog::getText(parent, QObject::tr("New %1").arg(tmpl.label),
                                                                QObject::tr("Name (saved in %1):").arg(QDir(content).relativeFilePath(dir)),
                                                                QLineEdit::Normal, suggestion, &ok));
        if (!ok || name.isEmpty())
            return {};
        suggestion = name;

        const QString path = QDir(dir).absoluteFilePath(name + "." + tmpl.suffix);
        if (QFileInfo::exists(path))
        {
            QMessageBox::warning(parent, QObject::tr("New %1").arg(tmpl.label), QObject::tr("%1 already exists.").arg(QDir(content).relativeFilePath(path)));
            continue;
        }
        QDir().mkpath(dir);

        QString error;
        bool written = false;
        if (tmpl.suffix == "fmap")
        {
            MapDocument map;
            written = map.saveCopy(path, &error);
        }
        else
        {
            QFile file(path);
            written = file.open(QIODevice::WriteOnly | QIODevice::Text);
            if (written)
                written = file.write(QString(tmpl.text).replace("{name}", name).toUtf8()) >= 0;
            if (!written)
                error = file.errorString();
        }
        if (!written)
        {
            ctx->log(QObject::tr("Could not create %1: %2").arg(path, error), LogLevel::Error);
            return {};
        }
        ctx->log(QObject::tr("Created %1").arg(QDir(content).relativeFilePath(path)));
        return path;
    }
}
