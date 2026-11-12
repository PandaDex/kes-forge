
#pragma once

#include "core/Math.h"

#include <QPointF>
#include <QSize>
#include <QVector2D>
#include <QVector3D>
#include <QMatrix4x4>

enum class ViewKind
{
    Perspective,
    Top,
    Front,
    Side
};

// One camera type for all four viewports. Z is up (Source convention).
//   Top   : looks down -Z, screen axes X (right) / Y (up)
//   Front : looks along +Y, screen axes X (right) / Z (up)
//   Side  : looks along -X, screen axes Y (right) / Z (up)
struct Camera
{
    ViewKind kind = ViewKind::Perspective;

    QVector3D pos{-384, -384, 256};
    float yaw = 45.f;
    float pitch = -28.f;
    float fovH = 90.f;

    QVector2D center{0, 0};
    float zoom = 0.5f; // Pixels per world unit

    QSize size{1, 1};

    QMatrix4x4 view;
    QMatrix4x4 proj;
    QMatrix4x4 viewProj;
    QMatrix4x4 invViewProj;

    bool isOrtho() const
    {
        return kind != ViewKind::Perspective;
    }

    int axisH() const
    {
        return kind == ViewKind::Side ? 1 : 0;
    }

    int axisV() const
    {
        return kind == ViewKind::Top ? 1 : 2;
    }

    int axisD() const
    {
        return kind == ViewKind::Top     ? 2
               : kind == ViewKind::Front ? 1
                                         : 0;
    }

    QVector3D forward() const;
    QVector3D right() const;

    void update();

    Ray ray(const QPointF &p) const;
    bool project(const QVector3D &world, QPointF &out) const;
    float unitsPerPixel(const QVector3D &at) const;

    void focus(const AABB &b);

    void orbit(float dx, float dy, float sensitivity = 0.25f);
};
