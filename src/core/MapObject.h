#pragma once
#include "core/Geometry.h"
#include "core/Math.h"
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QUuid>

using ObjectId = int;

struct Connection
{
    QString output, input, param;
    QUuid target;
    // Maps saved before GUID links stored the target's name; MapDocument::load resolves it.
    QString legacyTarget;
    float delay = 0.f;
    bool once = false;
    bool operator==(const Connection &o) const;
};

// Solids are made of one or more convex brushes stored relative to `origin`. For solids, origin is
// kept at the centre of the brushes' bounds and mins/maxs mirror those bounds (call updateBounds()
// after editing `brushes` directly). Point entities use mins/maxs from their class size.
struct MapObject
{
    ObjectId id = 0;
    // ids are per-session; the GUID survives save/load and is what MapScript links target.
    QUuid guid;
    QString name;
    QString classname;
    bool solid = false;
    bool visible = true;
    QString groupPath;
    QString visgroup;
    QVector3D origin;
    QVector3D angles; // pitch yaw roll
    QVector3D mins{-8, -8, -8};
    QVector3D maxs{8, 8, 8};
    QVector<Brush> brushes; // solids only, local space
    QMap<QString, QString> kv;
    QList<Connection> outputs;

    QString displayName() const;

    AABB worldBounds() const { return AABB(origin + mins, origin + maxs); }
    // Solids: stretches the brushes to fill `b`. Points: moves/resizes the box.
    void setWorldBounds(const AABB &b);

    void ensureBrushes();                          // legacy box-only solids get a box brush
    void updateBounds();                           // re-centre origin and refresh mins/maxs
    void setWorldBrushes(const QVector<Brush> &world);
    QVector<Brush> worldBrushes() const;
    void transformWorld(const QMatrix4x4 &m);
    bool intersectRay(const Ray &r, float *t) const;

    bool operator==(const MapObject &o) const;
    QJsonObject toJson() const;
    static MapObject fromJson(const QJsonObject &j);
};
