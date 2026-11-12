#include "core/EditorContext.h"
#include "tools/BuiltinTools.h"
#include "tools/MaterialTool.h"
#include <QFileInfo>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

EditorContext::EditorContext(QObject *parent) : QObject(parent)
{
    m_doc = new MapDocument(this);
    m_registry = new EntityRegistry(this);
    m_tools = new ToolManager(this);
    m_materials = new MaterialLibrary(this);

    m_tools->add(new SelectTool(this));
    m_tools->add(new EntityTool(this));
    m_tools->add(new BlockTool(this));
    m_tools->add(new ClipTool(this));
    m_tools->add(new VertexTool(this));
    m_tools->add(new MaterialTool(this));
    m_tools->setCurrent("select");

    connect(m_registry, &EntityRegistry::message, this, [this](const QString &t, int l)
            { emit logMessage(t, l); });
    connect(m_registry, &EntityRegistry::changed, this, [this]
            { m_doc->syncEntityBounds(*m_registry); });

    const QString exeDir = QCoreApplication::applicationDirPath();
    m_registry->addSearchPath(exeDir + "/entities");
#ifdef FORGE_SOURCE_DIR
    m_registry->addSearchPath(QString(FORGE_SOURCE_DIR) + "/entities");
#endif
    QDir().mkpath(userEntityDir());
    m_registry->addSearchPath(userEntityDir());
    m_registry->reloadAll();

    if (m_entityClass.isEmpty())
    {
        const auto pts = m_registry->classes(ClassKind::Point);
        if (!pts.isEmpty())
            m_entityClass = pts.first()->name;
    }

    // Project: the last .kproject opened. Without one, fall back to a content directory:
    // the last one chosen, else the KES game content next to this tool, else bundled content.
    const QString lastProject = QSettings().value("project").toString();
    if (!lastProject.isEmpty() && QFileInfo::exists(lastProject) && loadProject(lastProject))
        return;

    QStringList candidates;
    const QString saved = QSettings().value("contentDir").toString();
    if (!saved.isEmpty())
        candidates << saved;
#ifdef FORGE_SOURCE_DIR
    candidates << QDir::cleanPath(QString(FORGE_SOURCE_DIR) + "/../../game/content");
#endif
    candidates << exeDir + "/content";
#ifdef FORGE_SOURCE_DIR
    candidates << QString(FORGE_SOURCE_DIR) + "/content";
#endif
    candidates << QDir::homePath();
    for (const QString &c : candidates)
    {
        if (QDir(c).exists())
        {
            m_projectDir = QDir::cleanPath(c);
            break;
        }
    }
    m_materials->setRoot(m_projectDir);
}

bool EditorContext::loadProject(const QString &kprojectPath, QString *error, bool remember)
{
    KesProject project;
    if (!KesProject::load(kprojectPath, project, error))
        return false;
    m_project = project;
    m_projectDir = project.assetRoot;
    m_materials->setRoot(project.assetRoot, project.materialsDir, project.texturesDir);
    if (remember)
        QSettings().setValue("project", project.filePath);
    emit projectDirChanged();
    emit projectChanged();
    return true;
}

QString EditorContext::mapsDir() const
{
    if (m_project.isValid() && QDir(m_project.mapsDir).exists())
        return m_project.mapsDir;
    const QString maps = QDir(m_projectDir).filePath("maps");
    return QDir(maps).exists() ? maps : m_projectDir;
}

void EditorContext::setCurrentMaterial(const QString &path)
{
    if (path == m_currentMaterial)
        return;
    m_currentMaterial = path;
    emit currentMaterialChanged();
}

QString EditorContext::userEntityDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/entities";
}

void EditorContext::setGridSize(int g)
{
    g = std::clamp(g, 1, 1024);
    if (g == m_grid)
        return;
    m_grid = g;
    emit gridChanged();
}

void EditorContext::setSnapEnabled(bool s)
{
    if (s == m_snap)
        return;
    m_snap = s;
    emit snapChanged();
}

void EditorContext::setEntityClass(const QString &c)
{
    if (c == m_entityClass)
        return;
    m_entityClass = c;
    emit entityClassChanged();
}

void EditorContext::setProjectDir(const QString &d)
{
    if (d == m_projectDir)
        return;
    m_projectDir = d;
    m_project = {};
    QSettings().setValue("contentDir", d);
    QSettings().remove("project");
    m_materials->setRoot(d);
    emit projectDirChanged();
    emit projectChanged();
}

void EditorContext::setActiveViewport(Viewport *v)
{
    if (v == m_active)
        return;
    m_active = v;
    emit activeViewportChanged();
}
