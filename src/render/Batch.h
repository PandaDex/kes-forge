#pragma once
#include "core/Geometry.h"
#include "core/Math.h"
#include <QColor>
#include <QVector>

struct Vertex
{
    float x, y, z, r, g, b, a;
};

// "overlay" geometry is drawn last, alpha-blended, with depth testing off (selection, gizmos...).
struct Batch
{
    QVector<Vertex> tris, lines, otris, olines;

    static Vertex V(const QVector3D &p, const QColor &c, float shade = 1.f)
    {
        return {p.x(), p.y(), p.z(), float(c.redF()) * shade, float(c.greenF()) * shade,
                float(c.blueF()) * shade, float(c.alphaF())};
    }
    void line(const QVector3D &a, const QVector3D &b, const QColor &c, bool overlay = false)
    {
        auto &v = overlay ? olines : lines;
        v.push_back(V(a, c));
        v.push_back(V(b, c));
    }
    void tri(const QVector3D &a, const QVector3D &b, const QVector3D &c, const QColor &col, bool overlay = false, float shade = 1.f)
    {
        auto &v = overlay ? otris : tris;
        v.push_back(V(a, col, shade));
        v.push_back(V(b, col, shade));
        v.push_back(V(c, col, shade));
    }
    void quad(const QVector3D &a, const QVector3D &b, const QVector3D &c, const QVector3D &d, const QColor &col, bool overlay, float shade)
    {
        tri(a, b, c, col, overlay, shade);
        tri(a, c, d, col, overlay, shade);
    }
    void wireBox(const AABB &bb, const QColor &c, bool overlay = false)
    {
        for (int i = 0; i < 8; ++i)
            for (int bit = 1; bit <= 4; bit <<= 1)
                if (!(i & bit))
                    line(bb.corner(i), bb.corner(i | bit), c, overlay);
    }
    void solidBox(const AABB &bb, const QColor &c, bool overlay = false)
    {
        auto K = [&](int i)
        { return bb.corner(i); };
        quad(K(0), K(2), K(6), K(4), c, overlay, 0.80f); // -X
        quad(K(1), K(3), K(7), K(5), c, overlay, 0.80f); // +X
        quad(K(0), K(1), K(5), K(4), c, overlay, 0.65f); // -Y
        quad(K(2), K(3), K(7), K(6), c, overlay, 0.65f); // +Y
        quad(K(0), K(1), K(3), K(2), c, overlay, 0.45f); // -Z
        quad(K(4), K(5), K(7), K(6), c, overlay, 1.00f); // +Z
    }

    // Half-Lambert shading from a fixed key light so slanted brush faces read clearly in 3D.
    static float faceShade(const QVector3D &n)
    {
        static const QVector3D light = QVector3D(0.35f, 0.55f, 0.76f).normalized();
        return 0.42f + 0.58f * (0.5f + 0.5f * QVector3D::dotProduct(n, light));
    }
    void brushFaces(const Brush &b, const QColor &c, bool overlay = false, bool shaded = true)
    {
        for (const BrushFace &f : b.faces)
        {
            const float shade = shaded ? faceShade(f.normal()) : 1.f;
            for (int i = 1; i + 1 < f.points.size(); ++i)
                tri(f.points[0], f.points[i], f.points[i + 1], c, overlay, shade);
        }
    }
    void brushEdges(const Brush &b, const QColor &c, bool overlay = false)
    {
        for (const BrushFace &f : b.faces)
            for (int i = 0, n = f.points.size(); i < n; ++i)
                line(f.points[i], f.points[(i + 1) % n], c, overlay);
    }
};
