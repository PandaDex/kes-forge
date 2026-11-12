#pragma once
#include <QMatrix4x4>
#include <QVector3D>
#include <algorithm>
#include <cmath>

struct Ray
{
    QVector3D origin;
    QVector3D dir;
};

struct AABB
{
    QVector3D mn{1e30f, 1e30f, 1e30f};
    QVector3D mx{-1e30f, -1e30f, -1e30f};

    AABB() = default;
    AABB(const QVector3D &a, const QVector3D &b) : mn(a), mx(b) {}

    bool valid() const { return mn.x() <= mx.x() && mn.y() <= mx.y() && mn.z() <= mx.z(); }
    QVector3D center() const { return (mn + mx) * 0.5f; }
    QVector3D size() const { return mx - mn; }

    void expand(const QVector3D &p)
    {
        for (int i = 0; i < 3; ++i)
        {
            mn[i] = std::min(mn[i], p[i]);
            mx[i] = std::max(mx[i], p[i]);
        }
    }
    void expand(const AABB &o)
    {
        if (o.valid())
        {
            expand(o.mn);
            expand(o.mx);
        }
    }
    // bit0 = x, bit1 = y, bit2 = z (set => max)
    QVector3D corner(int i) const
    {
        return {(i & 1) ? mx.x() : mn.x(), (i & 2) ? mx.y() : mn.y(), (i & 4) ? mx.z() : mn.z()};
    }

    // Slab test. Returns the entry distance along the ray (0 when the origin is inside).
    bool intersect(const Ray &r, float *tHit = nullptr) const
    {
        float tmin = -1e30f, tmax = 1e30f;
        for (int i = 0; i < 3; ++i)
        {
            const float o = r.origin[i], d = r.dir[i];
            if (std::abs(d) < 1e-8f)
            {
                if (o < mn[i] || o > mx[i])
                    return false;
            }
            else
            {
                float t1 = (mn[i] - o) / d, t2 = (mx[i] - o) / d;
                if (t1 > t2)
                    std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax)
                    return false;
            }
        }
        if (tmax < 0.f)
            return false;
        if (tHit)
            *tHit = std::max(tmin, 0.f);
        return true;
    }
};

inline float snapScalar(float v, float grid) { return grid > 0.f ? std::round(v / grid) * grid : v; }
inline QVector3D snapVec(const QVector3D &v, float grid)
{
    return {snapScalar(v.x(), grid), snapScalar(v.y(), grid), snapScalar(v.z(), grid)};
}
