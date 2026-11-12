#pragma once
#include "core/Math.h"
#include <QJsonObject>
#include <QVector2D>
#include <QString>
#include <QVector>

// Convex brush geometry (the Hammer "solid" model): every brush is a closed convex polyhedron made
// of planar faces whose points wind counter-clockwise when seen from outside.

constexpr float kPlaneEpsilon = 0.01f;

struct Plane
{
    QVector3D normal{0, 0, 1};
    float dist = 0.f; // normal . p == dist on the plane

    float distanceTo(const QVector3D &p) const { return QVector3D::dotProduct(normal, p) - dist; }
    Plane flipped() const { return {-normal, -dist}; }
    // Plane through three points; normal follows (b-a) x (c-a). Returns false if degenerate.
    static bool fromPoints(const QVector3D &a, const QVector3D &b, const QVector3D &c, Plane &out);
};

// How a material's texture is laid onto a face (Hammer style). The texture is projected
// world-aligned along the dominant axis of the face normal, then:
//   u = (dot(p, uAxis) rotated) / scale.x + offset.x   (in texels; divided by the texture width)
// scale = world units per texel (1 = one texel per unit), offset in texels, rotation in degrees.
struct TextureMapping
{
    QVector2D scale{1.f, 1.f};
    QVector2D offset{0.f, 0.f};
    float rotation = 0.f;

    bool isDefault() const { return scale == QVector2D(1.f, 1.f) && offset.isNull() && rotation == 0.f; }
    bool operator==(const TextureMapping &o) const { return scale == o.scale && offset == o.offset && rotation == o.rotation; }
};

struct BrushFace
{
    QVector<QVector3D> points;
    QString material;
    TextureMapping mapping;

    QVector3D normal() const; // Newell's method, robust for slightly non-planar input
    Plane plane() const;
    QVector3D center() const;
    QVector2D textureCoord(const QVector3D &p, float textureWidth, float textureHeight) const;
    bool operator==(const BrushFace &o) const { return points == o.points && material == o.material && mapping == o.mapping; }
};

void textureAxesForNormal(const QVector3D &normal, QVector3D &uAxis, QVector3D &vAxis);

struct Brush
{
    QVector<BrushFace> faces;

    bool isValid() const;
    AABB bounds() const;
    QVector<QVector3D> vertices() const;
    QVector<Plane> planes() const;
    QVector3D center() const { return bounds().center(); }

    void translate(const QVector3D &d);
    void transform(const QMatrix4x4 &m); // keeps outward winding when m mirrors
    void setMaterial(const QString &material);
    void setTextureMapping(const TextureMapping &mapping);

    bool intersectRay(const Ray &r, float *t = nullptr) const;
    // Splits by `plane`. `front` receives the part on the positive side. Either output may come back
    // invalid (empty) if the brush lies entirely on one side.
    void split(const Plane &plane, Brush *front, Brush *back) const;

    // CSG subtract: the pieces of this brush outside `cutter`. Returns {*this} if they don't overlap.
    QVector<Brush> subtract(const Brush &cutter, bool *intersected = nullptr) const;

    bool operator==(const Brush &o) const { return faces == o.faces; }

    QJsonObject toJson() const;
    static Brush fromJson(const QJsonObject &j);

    static Brush box(const AABB &b, const QString &material = QString());
    // Intersection of the half-spaces behind each plane (normal points out of the brush).
    // `limit` (optional) is the starting box; a tight one keeps float error down. The result is
    // rejected if it touches the limit, since that means the planes didn't enclose a volume.
    static Brush fromPlanes(const QVector<Plane> &planes, const QString &material = QString(), const AABB &limit = AABB());
    static Brush convexHull(const QVector<QVector3D> &points, const QString &material = QString());
};

namespace Primitives
{
    enum class Kind
    {
        Block,
        Wedge,
        Cylinder,
        Spike,
        Sphere,
        Arch,
        Stairs
    };

    struct Params
    {
        Kind kind = Kind::Block;
        int sides = 8;
        float archDegrees = 180.f;
        float archStart = 0.f;     // arch: start angle (degrees, CCW from +X)
        float wallWidth = 16.f;
        int steps = 8;
        int direction = 0;         // wedge / stairs: 0 +Y, 1 +X, 2 -Y, 3 -X (the high side)
        QString material;
    };

    QString name(Kind k);
    QVector<Brush> build(const AABB &box, const Params &p);
}
