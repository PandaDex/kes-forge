#include "core/Geometry.h"
#include <QJsonArray>
#include <QObject>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kWorldLimit = 32768.f;

    bool finite(const QVector3D &v)
    {
        return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
    }

    QVector3D newell(const QVector<QVector3D> &pts)
    {
        QVector3D n;
        for (int i = 0, c = pts.size(); i < c; ++i)
        {
            const QVector3D &a = pts[i];
            const QVector3D &b = pts[(i + 1) % c];
            n += QVector3D((a.y() - b.y()) * (a.z() + b.z()),
                           (a.z() - b.z()) * (a.x() + b.x()),
                           (a.x() - b.x()) * (a.y() + b.y()));
        }
        return n;
    }

    void removeDuplicates(QVector<QVector3D> &pts)
    {
        QVector<QVector3D> out;
        out.reserve(pts.size());
        for (const QVector3D &p : pts)
            if (out.isEmpty() || (out.last() - p).lengthSquared() > 1e-6f)
                out.push_back(p);
        while (out.size() > 1 && (out.first() - out.last()).lengthSquared() <= 1e-6f)
            out.pop_back();
        pts = out;
    }

    bool usablePolygon(const QVector<QVector3D> &pts)
    {
        return pts.size() >= 3 && newell(pts).length() > 1e-4f;
    }

    void addUnique(QVector<QVector3D> &list, const QVector3D &p)
    {
        for (const QVector3D &q : list)
            if ((q - p).lengthSquared() < 1e-5f)
                return;
        list.push_back(p);
    }

    // Orders roughly coplanar points into a convex polygon whose winding faces `normal`.
    QVector<QVector3D> sortAround(QVector<QVector3D> pts, const QVector3D &normal)
    {
        if (pts.size() < 3)
            return {};
        QVector3D c;
        for (const QVector3D &p : pts)
            c += p;
        c /= float(pts.size());
        QVector3D u = QVector3D::crossProduct(normal, std::abs(normal.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0)).normalized();
        const QVector3D v = QVector3D::crossProduct(normal, u);
        std::sort(pts.begin(), pts.end(), [&](const QVector3D &a, const QVector3D &b)
                  {
            const QVector3D da = a - c, db = b - c;
            return std::atan2(QVector3D::dotProduct(da, v), QVector3D::dotProduct(da, u)) <
                   std::atan2(QVector3D::dotProduct(db, v), QVector3D::dotProduct(db, u)); });
        return pts;
    }

    void orientOutward(BrushFace &f, const QVector3D &inside)
    {
        if (QVector3D::dotProduct(newell(f.points), f.center() - inside) < 0.f)
            std::reverse(f.points.begin(), f.points.end());
    }

    BrushFace makeFace(const QVector<QVector3D> &pts, const QString &material, const QVector3D &inside)
    {
        BrushFace f;
        f.points = pts;
        f.material = material;
        orientOutward(f, inside);
        return f;
    }
}

bool Plane::fromPoints(const QVector3D &a, const QVector3D &b, const QVector3D &c, Plane &out)
{
    const QVector3D n = QVector3D::crossProduct(b - a, c - a);
    const float len = n.length();
    if (len < 1e-6f)
        return false;
    out.normal = n / len;
    out.dist = QVector3D::dotProduct(out.normal, a);
    return true;
}

QVector3D BrushFace::normal() const
{
    const QVector3D n = newell(points);
    return n.lengthSquared() > 0.f ? n.normalized() : QVector3D(0, 0, 1);
}

Plane BrushFace::plane() const
{
    const QVector3D n = normal();
    return {n, QVector3D::dotProduct(n, center())};
}

QVector3D BrushFace::center() const
{
    QVector3D c;
    for (const QVector3D &p : points)
        c += p;
    return points.isEmpty() ? c : c / float(points.size());
}

bool Brush::isValid() const
{
    if (faces.size() < 4)
        return false;
    for (const BrushFace &f : faces)
    {
        if (f.points.size() < 3)
            return false;
        for (const QVector3D &p : f.points)
            if (!finite(p))
                return false;
    }
    const QVector3D s = bounds().size();
    return s.x() > kPlaneEpsilon && s.y() > kPlaneEpsilon && s.z() > kPlaneEpsilon;
}

AABB Brush::bounds() const
{
    AABB b;
    for (const BrushFace &f : faces)
        for (const QVector3D &p : f.points)
            b.expand(p);
    return b;
}

QVector<QVector3D> Brush::vertices() const
{
    QVector<QVector3D> out;
    for (const BrushFace &f : faces)
        for (const QVector3D &p : f.points)
            addUnique(out, p);
    return out;
}

QVector<Plane> Brush::planes() const
{
    QVector<Plane> out;
    out.reserve(faces.size());
    for (const BrushFace &f : faces)
        out.push_back(f.plane());
    return out;
}

void Brush::translate(const QVector3D &d)
{
    for (BrushFace &f : faces)
        for (QVector3D &p : f.points)
            p += d;
}

void Brush::transform(const QMatrix4x4 &m)
{
    const bool mirror = m.determinant() < 0.0;
    for (BrushFace &f : faces)
    {
        for (QVector3D &p : f.points)
            p = m.map(p);
        if (mirror)
            std::reverse(f.points.begin(), f.points.end());
    }
}

void Brush::setMaterial(const QString &material)
{
    for (BrushFace &f : faces)
        f.material = material;
}

void Brush::setTextureMapping(const TextureMapping &mapping)
{
    for (BrushFace &f : faces)
        f.mapping = mapping;
}

void textureAxesForNormal(const QVector3D &n, QVector3D &uAxis, QVector3D &vAxis)
{
    const float ax = std::abs(n.x()), ay = std::abs(n.y()), az = std::abs(n.z());
    if (az >= ax && az >= ay)
    {
        uAxis = {1.f, 0.f, 0.f}; // floors and ceilings
        vAxis = {0.f, -1.f, 0.f};
    }
    else if (ax >= ay)
    {
        uAxis = {0.f, 1.f, 0.f}; // walls facing X
        vAxis = {0.f, 0.f, -1.f};
    }
    else
    {
        uAxis = {1.f, 0.f, 0.f}; // walls facing Y
        vAxis = {0.f, 0.f, -1.f};
    }
}

QVector2D BrushFace::textureCoord(const QVector3D &p, float textureWidth, float textureHeight) const
{
    QVector3D uAxis, vAxis;
    textureAxesForNormal(normal(), uAxis, vAxis);
    float u = QVector3D::dotProduct(p, uAxis);
    float v = QVector3D::dotProduct(p, vAxis);
    if (mapping.rotation != 0.f)
    {
        const float r = mapping.rotation * 3.14159265358979f / 180.f;
        const float c = std::cos(r), s = std::sin(r);
        const float ru = u * c - v * s;
        const float rv = u * s + v * c;
        u = ru;
        v = rv;
    }
    const float sx = std::abs(mapping.scale.x()) > 1e-4f ? mapping.scale.x() : 1.f;
    const float sy = std::abs(mapping.scale.y()) > 1e-4f ? mapping.scale.y() : 1.f;
    u = u / sx + mapping.offset.x();
    v = v / sy + mapping.offset.y();
    return {u / std::max(textureWidth, 1.f), v / std::max(textureHeight, 1.f)};
}

bool Brush::intersectRay(const Ray &r, float *tOut) const
{
    float tEnter = -1e30f, tExit = 1e30f;
    for (const BrushFace &f : faces)
    {
        const Plane p = f.plane();
        const float denom = QVector3D::dotProduct(p.normal, r.dir);
        const float dist = p.distanceTo(r.origin);
        if (std::abs(denom) < 1e-8f)
        {
            if (dist > kPlaneEpsilon)
                return false;
            continue;
        }
        const float t = -dist / denom;
        if (denom < 0.f)
            tEnter = std::max(tEnter, t);
        else
            tExit = std::min(tExit, t);
        if (tEnter > tExit)
            return false;
    }
    if (tExit < 0.f)
        return false;
    if (tOut)
        *tOut = std::max(tEnter, 0.f);
    return true;
}

void Brush::split(const Plane &plane, Brush *front, Brush *back) const
{
    if (front)
        front->faces.clear();
    if (back)
        back->faces.clear();

    float minD = 1e30f, maxD = -1e30f;
    for (const BrushFace &f : faces)
        for (const QVector3D &p : f.points)
        {
            const float d = plane.distanceTo(p);
            minD = std::min(minD, d);
            maxD = std::max(maxD, d);
        }
    if (minD >= -kPlaneEpsilon)
    {
        if (front)
            *front = *this;
        return;
    }
    if (maxD <= kPlaneEpsilon)
    {
        if (back)
            *back = *this;
        return;
    }

    Brush f, b;
    QVector<QVector3D> cap;
    const QString capMaterial = faces.isEmpty() ? QString() : faces.first().material;
    for (const BrushFace &face : faces)
    {
        QVector<QVector3D> fp, bp;
        const int n = face.points.size();
        for (int i = 0; i < n; ++i)
        {
            const QVector3D &a = face.points[i];
            const QVector3D &c = face.points[(i + 1) % n];
            const float da = plane.distanceTo(a), dc = plane.distanceTo(c);
            if (da >= -kPlaneEpsilon)
                fp.push_back(a);
            if (da <= kPlaneEpsilon)
                bp.push_back(a);
            if (std::abs(da) <= kPlaneEpsilon)
                addUnique(cap, a);
            if ((da > kPlaneEpsilon && dc < -kPlaneEpsilon) || (da < -kPlaneEpsilon && dc > kPlaneEpsilon))
            {
                const QVector3D x = a + (c - a) * (da / (da - dc));
                fp.push_back(x);
                bp.push_back(x);
                addUnique(cap, x);
            }
        }
        removeDuplicates(fp);
        removeDuplicates(bp);
        if (usablePolygon(fp))
        {
            BrushFace piece = face;
            piece.points = fp;
            f.faces.push_back(piece);
        }
        if (usablePolygon(bp))
        {
            BrushFace piece = face;
            piece.points = bp;
            b.faces.push_back(piece);
        }
    }

    if (cap.size() >= 3)
    {
        QVector<QVector3D> frontCap = sortAround(cap, -plane.normal);
        if (QVector3D::dotProduct(newell(frontCap), plane.normal) > 0.f)
            std::reverse(frontCap.begin(), frontCap.end());
        QVector<QVector3D> backCap = frontCap;
        std::reverse(backCap.begin(), backCap.end());
        if (usablePolygon(frontCap))
        {
            f.faces.push_back({frontCap, capMaterial});
            b.faces.push_back({backCap, capMaterial});
        }
    }
    if (front && f.isValid())
        *front = f;
    if (back && b.isValid())
        *back = b;
}

QVector<Brush> Brush::subtract(const Brush &cutter, bool *intersected) const
{
    if (intersected)
        *intersected = false;
    const AABB a = bounds(), c = cutter.bounds();
    for (int i = 0; i < 3; ++i)
        if (a.mx[i] <= c.mn[i] + kPlaneEpsilon || c.mx[i] <= a.mn[i] + kPlaneEpsilon)
            return {*this};

    QVector<Brush> outside;
    Brush rest = *this;
    for (const Plane &p : cutter.planes())
    {
        Brush front, back;
        rest.split(p, &front, &back);
        if (front.isValid())
            outside.push_back(front);
        rest = back;
        if (!rest.isValid())
            return {*this}; // never reached the cutter's interior: no overlap
    }
    if (intersected)
        *intersected = true;
    return outside;
}

QJsonObject Brush::toJson() const
{
    QJsonArray fs;
    for (const BrushFace &f : faces)
    {
        QJsonArray pts;
        for (const QVector3D &p : f.points)
            pts << double(p.x()) << double(p.y()) << double(p.z());
        QJsonObject fo{{"p", pts}};
        if (!f.material.isEmpty())
            fo["m"] = f.material;
        if (!f.mapping.isDefault())
            fo["t"] = QJsonArray{double(f.mapping.scale.x()), double(f.mapping.scale.y()),
                                 double(f.mapping.offset.x()), double(f.mapping.offset.y()), double(f.mapping.rotation)};
        fs.append(fo);
    }
    return QJsonObject{{"faces", fs}};
}

Brush Brush::fromJson(const QJsonObject &j)
{
    Brush b;
    for (const QJsonValue &v : j["faces"].toArray())
    {
        const QJsonObject fo = v.toObject();
        const QJsonArray pts = fo["p"].toArray();
        BrushFace f;
        f.material = fo["m"].toString();
        const QJsonArray t = fo["t"].toArray();
        if (t.size() >= 5)
        {
            f.mapping.scale = {float(t[0].toDouble(1.0)), float(t[1].toDouble(1.0))};
            f.mapping.offset = {float(t[2].toDouble()), float(t[3].toDouble())};
            f.mapping.rotation = float(t[4].toDouble());
        }
        for (int i = 0; i + 2 < pts.size(); i += 3)
            f.points.push_back(QVector3D(float(pts[i].toDouble()), float(pts[i + 1].toDouble()), float(pts[i + 2].toDouble())));
        b.faces.push_back(f);
    }
    return b;
}

Brush Brush::box(const AABB &bb, const QString &material)
{
    const QVector3D c = bb.center();
    auto K = [&](int i)
    { return bb.corner(i); };
    Brush b;
    b.faces = {
        makeFace({K(0), K(2), K(6), K(4)}, material, c), // -X
        makeFace({K(1), K(3), K(7), K(5)}, material, c), // +X
        makeFace({K(0), K(1), K(5), K(4)}, material, c), // -Y
        makeFace({K(2), K(3), K(7), K(6)}, material, c), // +Y
        makeFace({K(0), K(1), K(3), K(2)}, material, c), // -Z
        makeFace({K(4), K(5), K(7), K(6)}, material, c), // +Z
    };
    return b;
}

Brush Brush::fromPlanes(const QVector<Plane> &planes, const QString &material, const AABB &limitIn)
{
    const AABB limit = limitIn.valid() ? limitIn : AABB({-kWorldLimit, -kWorldLimit, -kWorldLimit}, {kWorldLimit, kWorldLimit, kWorldLimit});
    Brush b = box(limit, material);
    for (const Plane &p : planes)
    {
        Brush back;
        b.split(p, nullptr, &back);
        if (!back.isValid())
            return {};
        b = back;
    }
    for (BrushFace &f : b.faces)
        f.material = material;
    const AABB bb = b.bounds();
    for (int i = 0; i < 3; ++i)
        if (bb.mn[i] <= limit.mn[i] + 0.25f || bb.mx[i] >= limit.mx[i] - 0.25f)
            return {}; // unbounded
    return b;
}

Brush Brush::convexHull(const QVector<QVector3D> &input, const QString &material)
{
    QVector<QVector3D> pts;
    for (const QVector3D &p : input)
        if (finite(p))
            addUnique(pts, p);
    if (pts.size() < 4)
        return {};

    QVector<Plane> planes;
    const int n = pts.size();
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            for (int k = j + 1; k < n; ++k)
            {
                Plane p;
                if (!Plane::fromPoints(pts[i], pts[j], pts[k], p))
                    continue;
                bool known = false;
                for (const Plane &q : planes)
                    if ((QVector3D::dotProduct(q.normal, p.normal) > 0.9999f && std::abs(q.dist - p.dist) < kPlaneEpsilon) ||
                        (QVector3D::dotProduct(q.normal, p.normal) < -0.9999f && std::abs(q.dist + p.dist) < kPlaneEpsilon))
                    {
                        known = true;
                        break;
                    }
                if (known)
                    continue;
                bool pos = false, neg = false;
                for (int m = 0; m < n && !(pos && neg); ++m)
                {
                    const float d = p.distanceTo(pts[m]);
                    pos |= d > kPlaneEpsilon;
                    neg |= d < -kPlaneEpsilon;
                }
                if (pos && neg)
                    continue;
                planes.push_back(pos ? p.flipped() : p);
            }

    AABB in;
    for (const QVector3D &p : pts)
        in.expand(p);
    Brush b = fromPlanes(planes, material, AABB(in.mn - QVector3D(4, 4, 4), in.mx + QVector3D(4, 4, 4)));
    if (!b.isValid())
        return {};
    const AABB out = b.bounds();
    for (int i = 0; i < 3; ++i)
        if (out.mn[i] < in.mn[i] - 0.5f || out.mx[i] > in.mx[i] + 0.5f)
            return {};
    return b;
}

namespace Primitives
{
    QString name(Kind k)
    {
        switch (k)
        {
        case Kind::Block:
            return QObject::tr("Block");
        case Kind::Wedge:
            return QObject::tr("Wedge");
        case Kind::Cylinder:
            return QObject::tr("Cylinder");
        case Kind::Spike:
            return QObject::tr("Spike");
        case Kind::Sphere:
            return QObject::tr("Sphere");
        case Kind::Arch:
            return QObject::tr("Arch");
        case Kind::Stairs:
            return QObject::tr("Stairs");
        }
        return {};
    }

    namespace
    {
        QVector3D ring(const AABB &b, float rx, float ry, float angle, float z)
        {
            const QVector3D c = b.center();
            return {c.x() + rx * std::cos(angle), c.y() + ry * std::sin(angle), z};
        }

        Brush cylinder(const AABB &b, int sides, const QString &mat)
        {
            const QVector3D c = b.center();
            const float rx = b.size().x() * 0.5f, ry = b.size().y() * 0.5f;
            QVector<QVector3D> bottom, top;
            for (int i = 0; i < sides; ++i)
            {
                const float a = 2.f * kPi * i / sides;
                bottom.push_back(ring(b, rx, ry, a, b.mn.z()));
                top.push_back(ring(b, rx, ry, a, b.mx.z()));
            }
            Brush br;
            br.faces.push_back(makeFace(top, mat, c));
            br.faces.push_back(makeFace(bottom, mat, c));
            for (int i = 0; i < sides; ++i)
            {
                const int j = (i + 1) % sides;
                br.faces.push_back(makeFace({bottom[i], bottom[j], top[j], top[i]}, mat, c));
            }
            return br;
        }

        Brush spike(const AABB &b, int sides, const QString &mat)
        {
            const QVector3D c = b.center();
            const float rx = b.size().x() * 0.5f, ry = b.size().y() * 0.5f;
            const QVector3D apex(c.x(), c.y(), b.mx.z());
            const QVector3D inside(c.x(), c.y(), b.mn.z() + b.size().z() * 0.25f);
            QVector<QVector3D> base;
            for (int i = 0; i < sides; ++i)
                base.push_back(ring(b, rx, ry, 2.f * kPi * i / sides, b.mn.z()));
            Brush br;
            br.faces.push_back(makeFace(base, mat, inside));
            for (int i = 0; i < sides; ++i)
                br.faces.push_back(makeFace({base[i], base[(i + 1) % sides], apex}, mat, inside));
            return br;
        }

        Brush sphere(const AABB &b, int sides, const QString &mat)
        {
            const QVector3D c = b.center(), h = b.size() * 0.5f;
            const int rings = std::max(2, sides / 2);
            auto P = [&](int k, int i)
            {
                const float theta = kPi * k / rings, phi = 2.f * kPi * i / sides;
                return QVector3D(c.x() + h.x() * std::sin(theta) * std::cos(phi),
                                 c.y() + h.y() * std::sin(theta) * std::sin(phi),
                                 c.z() + h.z() * std::cos(theta));
            };
            const QVector3D top(c.x(), c.y(), b.mx.z()), bottom(c.x(), c.y(), b.mn.z());
            Brush br;
            for (int i = 0; i < sides; ++i)
            {
                const int j = (i + 1) % sides;
                br.faces.push_back(makeFace({top, P(1, i), P(1, j)}, mat, c));
                for (int k = 1; k + 1 < rings; ++k)
                    br.faces.push_back(makeFace({P(k, i), P(k + 1, i), P(k + 1, j), P(k, j)}, mat, c));
                br.faces.push_back(makeFace({bottom, P(rings - 1, j), P(rings - 1, i)}, mat, c));
            }
            return br;
        }

        QVector<Brush> arch(const AABB &b, const Params &p)
        {
            const int segments = std::clamp(p.sides, 1, 64);
            const float rx = b.size().x() * 0.5f, ry = b.size().y() * 0.5f;
            const float w = std::max(1.f, p.wallWidth);
            const float irx = std::max(0.f, rx - w), iry = std::max(0.f, ry - w);
            const float arc = std::clamp(p.archDegrees, 1.f, 360.f) * kPi / 180.f;
            const float start = p.archStart * kPi / 180.f;
            QVector<Brush> out;
            for (int s = 0; s < segments; ++s)
            {
                const float a0 = start + arc * s / segments, a1 = start + arc * (s + 1) / segments;
                QVector<QVector3D> pts;
                for (float z : {b.mn.z(), b.mx.z()})
                {
                    pts << ring(b, rx, ry, a0, z) << ring(b, rx, ry, a1, z)
                        << ring(b, irx, iry, a1, z) << ring(b, irx, iry, a0, z);
                }
                const Brush br = Brush::convexHull(pts, p.material);
                if (br.isValid())
                    out.push_back(br);
            }
            return out;
        }

        QVector<Brush> stairs(const AABB &b, const Params &p)
        {
            const int steps = std::clamp(p.steps, 1, 256);
            const int dir = ((p.direction % 4) + 4) % 4;
            const int axis = (dir == 0 || dir == 2) ? 1 : 0;
            const bool positive = dir == 0 || dir == 1;
            const float run = b.size()[axis] / steps, rise = b.size().z() / steps;
            QVector<Brush> out;
            for (int i = 0; i < steps; ++i)
            {
                AABB s = b;
                if (positive)
                {
                    s.mn[axis] = b.mn[axis] + run * i;
                    s.mx[axis] = b.mn[axis] + run * (i + 1);
                }
                else
                {
                    s.mx[axis] = b.mx[axis] - run * i;
                    s.mn[axis] = b.mx[axis] - run * (i + 1);
                }
                s.mx[2] = b.mn.z() + rise * (i + 1);
                if (s.size()[axis] > kPlaneEpsilon && s.size().z() > kPlaneEpsilon)
                    out.push_back(Brush::box(s, p.material));
            }
            return out;
        }

        Brush wedge(const AABB &b, const Params &p)
        {
            const int dir = ((p.direction % 4) + 4) % 4;
            QVector<QVector3D> pts;
            for (int i = 0; i < 4; ++i)
                pts << b.corner(i);
            for (int i = 4; i < 8; ++i)
            {
                const QVector3D c = b.corner(i);
                const bool keep = dir == 0 ? (i & 2) : dir == 1 ? (i & 1)
                                                   : dir == 2   ? !(i & 2)
                                                                : !(i & 1);
                if (keep)
                    pts << c;
            }
            return Brush::convexHull(pts, p.material);
        }
    }

    QVector<Brush> build(const AABB &box, const Params &p)
    {
        const QVector3D s = box.size();
        if (!box.valid() || s.x() < 1.f || s.y() < 1.f || s.z() < 1.f)
            return {};
        const int sides = std::clamp(p.sides, 3, 64);
        Brush single;
        switch (p.kind)
        {
        case Kind::Block:
            single = Brush::box(box, p.material);
            break;
        case Kind::Wedge:
            single = wedge(box, p);
            break;
        case Kind::Cylinder:
            single = cylinder(box, sides, p.material);
            break;
        case Kind::Spike:
            single = spike(box, sides, p.material);
            break;
        case Kind::Sphere:
            single = sphere(box, std::max(sides, 4), p.material);
            break;
        case Kind::Arch:
            return arch(box, p);
        case Kind::Stairs:
            return stairs(box, p);
        }
        if (!single.isValid())
            return {};
        return {single};
    }
}
