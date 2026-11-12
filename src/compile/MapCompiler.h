#pragma once

#include "core/MapObject.h"
#include <QVector2D>
#include <QVector>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <memory>
#include <vector>

class EntityRegistry;
class MaterialLibrary;

struct CompileVertex
{
    QVector3D position;
    QVector3D normal;
    QVector2D uv;
};

// Render geometry for one material of one owner. entity == -1 for world geometry (world
// space), otherwise the index of the owning entity (positions relative to its origin).
struct CompileMesh
{
    qint32 entity = -1;
    quint32 material = 0;
    QVector<CompileVertex> vertices;
    QVector<quint32> indices;
};

// Convex collision hull for one brush, same ownership/space rules as CompileMesh.
struct CompileBrush
{
    qint32 entity = -1;
    AABB bounds;
    QVector<Plane> planes;
};

struct CompileSettings
{
    QString outputPath;
    QString gameDir;
    bool runAfterBuild = false;
    QStringList extraArgs;
};

struct CompileContext
{
    CompileSettings settings;
    const EntityRegistry *registry = nullptr;
    const MaterialLibrary *materials = nullptr;
    QVector<MapObject> map;
    QStringList materialNames;                    // "" = engine default material
    QVector<CompileMesh> meshes;
    QVector<CompileBrush> brushes;
    QStringList log;
    bool cancelRequested = false;
};

class ICompileStage
{
public:
    virtual ~ICompileStage() = default;
    virtual QString name() const = 0;
    virtual bool run(CompileContext &ctx) = 0;
};

class IMapCompiler
{
public:
    virtual ~IMapCompiler() = default;
    virtual void addStage(std::unique_ptr<ICompileStage> stage) = 0;
    virtual bool compile(CompileContext &ctx) = 0;
};

std::unique_ptr<IMapCompiler> createMapCompiler();
bool validateMap(CompileContext &ctx);
