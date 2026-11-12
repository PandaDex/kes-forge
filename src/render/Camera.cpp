
#include "render/Camera.h"

#include <QVector4D>

#include <algorithm>
#include <cmath>

static constexpr float kDeg2Rad =
    3.14159265358979f / 180.f;

static constexpr float kMinPitch = -89.5f;
static constexpr float kMaxPitch = 89.5f;

QVector3D Camera::forward() const
{
    switch (kind)
    {
    case ViewKind::Top:
        return {0, 0, -1};

    case ViewKind::Front:
        return {0, 1, 0};

    case ViewKind::Side:
        return {-1, 0, 0};

    case ViewKind::Perspective:
        break;
    }

    const float p = std::clamp(pitch, kMinPitch, kMaxPitch);
    const float cp = std::cos(p * kDeg2Rad);

    return QVector3D(
               cp * std::cos(yaw * kDeg2Rad),
               cp * std::sin(yaw * kDeg2Rad),
               std::sin(p * kDeg2Rad))
        .normalized();
}

QVector3D Camera::right() const
{
    if (kind == ViewKind::Top)
        return {1, 0, 0};

    if (kind == ViewKind::Front)
        return {-1, 0, 0};

    if (kind == ViewKind::Side)
        return {0, 1, 0};

    const QVector3D f = forward();

    const QVector3D r =
        QVector3D::crossProduct(f, QVector3D(0, 0, 1));

    if (r.lengthSquared() < 1e-8f)
    {
        // Safe fallback near the vertical poles.
        return {1, 0, 0};
    }

    return r.normalized();
}

void Camera::update()
{
    const float w = float(std::max(1, size.width()));
    const float h = float(std::max(1, size.height()));

    if (kind == ViewKind::Perspective)
    {
        pitch = std::clamp(pitch, kMinPitch, kMaxPitch);

        // Keep yaw bounded without introducing a visual jump.
        yaw = std::remainder(yaw, 360.f);
    }

    view.setToIdentity();
    proj.setToIdentity();

    if (isOrtho())
    {
        const QVector3D f = forward();

        const QVector3D up =
            kind == ViewKind::Top
                ? QVector3D(0, 1, 0)
                : QVector3D(0, 0, 1);

        QVector3D c;
        c[axisH()] = center.x();
        c[axisV()] = center.y();
        c[axisD()] = 0.f;

        view.lookAt(
            c - f * 30000.f,
            c,
            up);

        const float safeZoom = std::max(zoom, 0.0001f);

        proj.ortho(
            -w * 0.5f / safeZoom,
            w * 0.5f / safeZoom,
            -h * 0.5f / safeZoom,
            h * 0.5f / safeZoom,
            1.f,
            60000.f);
    }
    else
    {
        const QVector3D f = forward();

        // Fixed world-up with pitch clamping prevents pole flips.
        view.lookAt(
            pos,
            pos + f,
            QVector3D(0, 0, 1));

        const float aspect = w / h;

        const float vf =
            2.f * std::atan(
                      std::tan(fovH * kDeg2Rad * 0.5f) / aspect);

        proj.perspective(
            std::min(vf / kDeg2Rad, 150.f),
            aspect,
            4.f,
            40000.f);
    }

    viewProj = proj * view;
    invViewProj = viewProj.inverted();
}

Ray Camera::ray(const QPointF &p) const
{
    const float w = float(std::max(1, size.width()));
    const float h = float(std::max(1, size.height()));

    const float nx = float(2.0 * p.x() / w - 1.0);
    const float ny = float(1.0 - 2.0 * p.y() / h);

    const QVector3D n =
        invViewProj.map(QVector3D(nx, ny, -1.f));

    const QVector3D f =
        invViewProj.map(QVector3D(nx, ny, 1.f));

    return {n, (f - n).normalized()};
}

bool Camera::project(
    const QVector3D &world,
    QPointF &out) const
{
    const QVector4D c =
        viewProj * QVector4D(world, 1.f);

    if (c.w() <= 1e-4f)
        return false;

    const float x = c.x() / c.w();
    const float y = c.y() / c.w();

    out = QPointF(
        (x * 0.5 + 0.5) * size.width(),
        (1.0 - (y * 0.5 + 0.5)) * size.height());

    return true;
}

float Camera::unitsPerPixel(
    const QVector3D &at) const
{
    if (isOrtho())
        return 1.f / std::max(zoom, 0.0001f);

    const float dist = std::max(
        1.f,
        QVector3D::dotProduct(at - pos, forward()));

    return dist * 2.f * std::tan(fovH * kDeg2Rad * 0.5f) / float(std::max(1, size.width()));
}

void Camera::focus(const AABB &b)
{
    if (!b.valid())
        return;

    const QVector3D c = b.center();
    const QVector3D s = b.size();

    if (isOrtho())
    {
        center = QVector2D(
            c[axisH()],
            c[axisV()]);

        const float sh = std::max(s[axisH()], 32.f);
        const float sv = std::max(s[axisV()], 32.f);

        zoom = std::clamp(
            std::min(
                float(size.width()) / (sh * 1.6f),
                float(size.height()) / (sv * 1.6f)),
            0.02f,
            16.f);
    }
    else
    {
        const float r = std::max({s.x(), s.y(), s.z(), 32.f});

        pos = c - forward() * std::max(r * 3.f, 160.f);
    }

    update();
}

void Camera::orbit(
    float dx,
    float dy,
    float sensitivity)
{
    if (kind != ViewKind::Perspective)
        return;

    // Incremental deltas avoid angle discontinuities when
    // crossing the -180/180 degree yaw boundary.
    yaw += dx * sensitivity;

    pitch = std::clamp(
        pitch - dy * sensitivity,
        kMinPitch,
        kMaxPitch);

    update();
}
