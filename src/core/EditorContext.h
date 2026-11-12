#pragma once
#include "core/MapDocument.h"
#include "core/MaterialLibrary.h"
#include "core/ProjectFile.h"
#include "entities/EntityDefs.h"
#include "tools/Tool.h"
#include <QList>
#include <QObject>

class Viewport;

enum class LogLevel
{
    Info = 0,
    Warning = 1,
    Error = 2
};

class EditorContext : public QObject
{
    Q_OBJECT
public:
    explicit EditorContext(QObject *parent = nullptr);

    MapDocument *doc() const { return m_doc; }
    EntityRegistry *registry() const { return m_registry; }
    ToolManager *tools() const { return m_tools; }
    MaterialLibrary *materials() const { return m_materials; }

    const QString &currentMaterial() const { return m_currentMaterial; }
    void setCurrentMaterial(const QString &path);

    int gridSize() const { return m_grid; }
    bool snapEnabled() const { return m_snap; }
    const QString &entityClass() const { return m_entityClass; }
    // Content directory (the project's AssetRoot); asset paths in maps are relative to it.
    const QString &projectDir() const { return m_projectDir; }
    const KesProject &project() const { return m_project; }
    // remember: store as the project to reopen on the next start (off for headless compiles).
    bool loadProject(const QString &kprojectPath, QString *error = nullptr, bool remember = true);
    // Folder for map dialogs: the project's MapsDir, else <content>/maps, else the content dir.
    QString mapsDir() const;
    QString userEntityDir() const;

    void setGridSize(int g);
    void setSnapEnabled(bool s);
    void setEntityClass(const QString &c);
    void setProjectDir(const QString &d);

    float snap(float v) const { return m_snap ? snapScalar(v, float(m_grid)) : v; }
    QVector3D snap(const QVector3D &v) const { return m_snap ? snapVec(v, float(m_grid)) : v; }

    void registerViewport(Viewport *v) { m_viewports << v; }
    const QList<Viewport *> &viewports() const { return m_viewports; }
    Viewport *activeViewport() const { return m_active; }
    void setActiveViewport(Viewport *v);

    void focusOn(const AABB &b) { emit focusRequested(b); }
    void log(const QString &msg, LogLevel level = LogLevel::Info) { emit logMessage(msg, int(level)); }
    void setCursorText(const QString &s) { emit cursorText(s); }

signals:
    void gridChanged();
    void snapChanged();
    void entityClassChanged();
    void projectDirChanged();
    void currentMaterialChanged();
    void projectChanged();
    void activeViewportChanged();
    void focusRequested(const AABB &bounds);
    void logMessage(const QString &text, int level);
    void cursorText(const QString &text);

private:
    MapDocument *m_doc;
    EntityRegistry *m_registry;
    ToolManager *m_tools;
    MaterialLibrary *m_materials;
    QString m_currentMaterial;
    QList<Viewport *> m_viewports;
    Viewport *m_active = nullptr;
    int m_grid = 16;
    bool m_snap = true;
    QString m_entityClass;
    QString m_projectDir;
    KesProject m_project;
};
