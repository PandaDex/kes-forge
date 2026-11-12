#include "tools/BuiltinTools.h"
#include "core/EditorContext.h"
#include "render/Batch.h"
#include "render/Viewport.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{
    const QColor kOrange(255, 150, 30);
    const QColor kAxisColors[3] = {QColor(235, 70, 70), QColor(80, 210, 80), QColor(80, 130, 245)};
    const QVector3D kAxisDirs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    constexpr float kPi = 3.14159265358979f;

    void gizmoPlaneBasis(int axis, QVector3D &u, QVector3D &v)
    {
        static const QVector3D bases[3][2] = {
            {{0, 1, 0}, {0, 0, 1}},
            {{0, 0, 1}, {1, 0, 0}},
            {{1, 0, 0}, {0, 1, 0}}};
        u = bases[axis][0];
        v = bases[axis][1];
    }

    float pointSegmentDistance(const QPointF &p, const QPointF &a, const QPointF &b)
    {
        const QPointF ab = b - a;
        const float lengthSquared = float(ab.x() * ab.x() + ab.y() * ab.y());
        const float t = lengthSquared > 0.f
                            ? std::clamp(float(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / lengthSquared), 0.f, 1.f)
                            : 0.f;
        const QPointF nearest = a + ab * t;
        return float(std::hypot(p.x() - nearest.x(), p.y() - nearest.y()));
    }

    void marker(Batch &b, Viewport *vp, const QVector3D &p, float pixels, const QColor &c)
    {
        const float h = vp->camera().unitsPerPixel(p) * pixels;
        b.solidBox(AABB(p - QVector3D(h, h, h), p + QVector3D(h, h, h)), c, true);
    }

    void circleMarker(Batch &b, Viewport *vp, const QVector3D &p, float pixels, const QColor &c)
    {
        const Camera &cam = vp->camera();
        const float r = cam.unitsPerPixel(p) * pixels;
        QVector3D u, v;
        u[cam.axisH()] = 1.f;
        v[cam.axisV()] = 1.f;
        QVector3D prev = p + u * r;
        for (int i = 1; i <= 16; ++i)
        {
            const float a = 2.f * kPi * i / 16.f;
            const QVector3D cur = p + (u * std::cos(a) + v * std::sin(a)) * r;
            b.line(prev, cur, c, true);
            prev = cur;
        }
    }

    void drawLabel(QPainter &p, const QPointF &at, const QString &text)
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        const QFontMetrics fm(p.font());
        const QRectF r(at.x(), at.y(), fm.horizontalAdvance(text) + 12, fm.height() + 6);
        p.setPen(QPen(QColor(255, 150, 30, 160), 1));
        p.setBrush(QColor(20, 21, 24, 215));
        p.drawRoundedRect(r, 4, 4);
        p.setPen(QColor(240, 240, 240));
        p.drawText(r, Qt::AlignCenter, text);
        p.restore();
    }
}

SelectTool::SelectTool(EditorContext *ctx) : Tool(ctx) {}

void SelectTool::deactivate()
{
    cancelDrag();
}

AABB SelectTool::solidSelectionBounds() const
{
    AABB b;
    for (ObjectId id : m_ctx->doc()->selection())
    {
        const MapObject *o = m_ctx->doc()->find(id);
        if (o && o->solid && m_ctx->doc()->isVisible(*o))
            b.expand(o->worldBounds());
    }
    return b;
}

// Handles: idx = (hx+1)*3 + (hy+1) with hx,hy in {-1,0,1}; idx 4 is the centre (unused).
QPointF SelectTool::handleScreenPos(Viewport *vp, int idx, const AABB &b, QVector3D *worldOut) const
{
    const Camera &cam = vp->camera();
    const int hx = idx / 3 - 1, hy = idx % 3 - 1;
    QVector3D w = b.center();
    w[cam.axisH()] = hx < 0 ? b.mn[cam.axisH()] : hx > 0 ? b.mx[cam.axisH()]
                                                         : w[cam.axisH()];
    w[cam.axisV()] = hy < 0 ? b.mn[cam.axisV()] : hy > 0 ? b.mx[cam.axisV()]
                                                         : w[cam.axisV()];
    if (worldOut)
        *worldOut = w;
    QPointF s;
    cam.project(w, s);
    return s;
}

int SelectTool::hitHandle(Viewport *vp, const QPointF &p) const
{
    const AABB b = solidSelectionBounds();
    if (!vp->isOrtho() || !b.valid())
        return -1;
    for (int i = 0; i < 9; ++i)
    {
        if (i == 4 || (m_rotateHandles && (i % 2 == 1)))
            continue; // rotate mode only uses the corners
        const QPointF s = handleScreenPos(vp, i, b);
        if (std::abs(s.x() - p.x()) <= 7 && std::abs(s.y() - p.y()) <= 7)
            return i;
    }
    return -1;
}

float SelectTool::gizmoLength(Viewport *vp, const QVector3D &c) const { return vp->camera().unitsPerPixel(c) * 90.f; }

QRectF SelectTool::gizmoToolbarButtonRect(Viewport *vp, int index) const
{
    constexpr qreal buttonWidth = 30.0;
    constexpr qreal buttonHeight = 30.0;
    constexpr qreal gap = 3.0;
    constexpr qreal margin = 10.0;
    constexpr qreal totalWidth = buttonWidth * 3.0 + gap * 2.0;
    return QRectF(vp->width() - margin - totalWidth + index * (buttonWidth + gap),
                  vp->height() - margin - buttonHeight, buttonWidth, buttonHeight);
}

int SelectTool::hitGizmoToolbar(Viewport *vp, const QPointF &position) const
{
    if (vp->isOrtho())
        return -1;
    for (int i = 0; i < 3; ++i)
        if (gizmoToolbarButtonRect(vp, i).contains(position))
            return i;
    return -1;
}

// Parameter along line (center + axis*t) closest to the mouse ray.
bool SelectTool::axisParam(Viewport *vp, const QPointF &p, const QVector3D &center, int axis, float &t) const
{
    const Ray r = vp->camera().ray(p);
    const QVector3D u = kAxisDirs[axis];
    const QVector3D w0 = r.origin - center;
    const float b = QVector3D::dotProduct(r.dir, u), d = QVector3D::dotProduct(r.dir, w0), e = QVector3D::dotProduct(u, w0);
    const float denom = 1.f - b * b;
    if (std::abs(denom) < 1e-5f)
        return false;
    t = (e - b * d) / denom;
    return true;
}

bool SelectTool::rotationVector(Viewport *vp, const QPointF &p, const QVector3D &center, int axis, QVector3D &vector) const
{
    const Ray ray = vp->camera().ray(p);
    const QVector3D normal = kAxisDirs[axis];
    const float denominator = QVector3D::dotProduct(ray.dir, normal);
    if (std::abs(denominator) < 1e-5f)
        return false;
    const float t = QVector3D::dotProduct(center - ray.origin, normal) / denominator;
    if (t < 0.f)
        return false;
    vector = ray.origin + ray.dir * t - center;
    if (vector.lengthSquared() < 1e-6f)
        return false;
    vector.normalize();
    return true;
}

int SelectTool::hitGizmo(Viewport *vp, const QPointF &p) const
{
    if (vp->isOrtho() || m_ctx->doc()->selection().isEmpty())
        return -1;
    const QVector3D c = m_ctx->doc()->selectionBounds().center();
    const float L = gizmoLength(vp, c);
    if (m_gizmoMode == GizmoMode::Rotate)
    {
        int best = -1;
        float bestDistance = 9.f;
        for (int axis = 0; axis < 3; ++axis)
        {
            QVector3D u, v;
            gizmoPlaneBasis(axis, u, v);
            QPointF previous;
            bool havePrevious = vp->camera().project(c + u * L, previous);
            for (int segment = 1; segment <= 48; ++segment)
            {
                const float angle = 2.f * kPi * segment / 48.f;
                QPointF current;
                const bool projected = vp->camera().project(c + (u * std::cos(angle) + v * std::sin(angle)) * L, current);
                if (havePrevious && projected)
                {
                    const float distance = pointSegmentDistance(p, previous, current);
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        best = axis;
                    }
                }
                previous = current;
                havePrevious = projected;
            }
        }
        return best;
    }
    const float thr = vp->camera().unitsPerPixel(c) * 9.f;
    const Ray r = vp->camera().ray(p);
    int best = -1;
    float bestD = thr;
    for (int i = 0; i < 3; ++i)
    {
        const QVector3D u = kAxisDirs[i];
        const QVector3D w0 = r.origin - c;
        const float b = QVector3D::dotProduct(r.dir, u), d = QVector3D::dotProduct(r.dir, w0), e = QVector3D::dotProduct(u, w0);
        const float denom = 1.f - b * b;
        if (std::abs(denom) < 1e-5f)
            continue;
        const float s = (b * e - d) / denom;
        if (s < 0.f)
            continue;
        const float t = std::clamp((e - b * d) / denom, 0.f, L);
        const float dist = ((r.origin + r.dir * s) - (c + u * t)).length();
        if (dist < bestD)
        {
            bestD = dist;
            best = i;
        }
    }
    return best;
}

void SelectTool::captureBefore()
{
    m_before.clear();
    for (ObjectId id : m_ctx->doc()->selectionList())
        if (const MapObject *o = m_ctx->doc()->find(id))
            m_before.push_back(*o);
}

void SelectTool::cancelDrag()
{
    if (m_mode == Mode::Move || m_mode == Mode::Resize || m_mode == Mode::Gizmo || m_mode == Mode::Rotate2D)
    {
        if (m_moved && !m_before.isEmpty())
            m_ctx->doc()->previewReplace(m_before);
    }
    m_mode = Mode::None;
    m_moved = false;
    m_before.clear();
    for (Viewport *v : m_ctx->viewports())
        v->update();
}

void SelectTool::beginMove(Viewport *vp, const QPointF &p)
{
    MapDocument *doc = m_ctx->doc();
    captureBefore();
    const Camera &cam = vp->camera();
    const float planeVal = vp->isOrtho() ? 0.f : doc->selectionBounds().center().z();
    if (!vp->planePoint(p, vp->isOrtho() ? cam.axisD() : 2, planeVal, m_startPt))
        return;
    m_mode = Mode::Move;
    m_vp = vp;
}

void SelectTool::applyDelta(const QVector3D &delta)
{
    QVector<MapObject> after = m_before;
    for (MapObject &o : after)
        o.origin += delta;
    m_ctx->doc()->previewReplace(after);
}

void SelectTool::applyResize(Viewport *vp, const QPointF &p)
{
    const Camera &cam = vp->camera();
    const int h = cam.axisH(), v = cam.axisV();
    QVector3D cur;
    if (!vp->planePoint(p, cam.axisD(), 0.f, cur))
        return;
    const int hx = m_handle / 3 - 1, hy = m_handle % 3 - 1;
    const float minSize = m_ctx->snapEnabled() ? float(m_ctx->gridSize()) : 1.f;
    AABB nb = m_startBounds;
    auto edit = [&](int axis, int dir)
    {
        if (dir == 0)
            return;
        const float val = m_ctx->snap(cur[axis]);
        if (dir < 0)
            nb.mn[axis] = std::min(val, nb.mx[axis] - minSize);
        else
            nb.mx[axis] = std::max(val, nb.mn[axis] + minSize);
    };
    edit(h, hx);
    edit(v, hy);

    QVector<MapObject> after = m_before;
    for (MapObject &o : after)
    {
        if (!o.solid)
            continue;
        AABB b = o.worldBounds();
        for (int axis : {h, v})
        {
            const float oldS = m_startBounds.mx[axis] - m_startBounds.mn[axis];
            if (oldS < 1e-4f)
                continue;
            const float newS = nb.mx[axis] - nb.mn[axis];
            auto map = [&](float x)
            { return nb.mn[axis] + (x - m_startBounds.mn[axis]) / oldS * newS; };
            const float a = map(b.mn[axis]), c = map(b.mx[axis]);
            b.mn[axis] = a;
            b.mx[axis] = c;
        }
        o.setWorldBounds(b);
    }
    m_ctx->doc()->previewReplace(after);
}

void SelectTool::applyScale(float delta)
{
    const float length = gizmoLength(m_vp, m_gizmoCenter);
    float factor = std::max(0.05f, 1.f + delta / length);
    // Keep the selection's size on the grid along the scaled axis.
    const AABB start = [this]
    { AABB b; for (const MapObject &o : m_before) b.expand(o.worldBounds()); return b; }();
    const float size = start.size()[m_axis];
    if (m_ctx->snapEnabled() && size > 1e-3f)
        factor = std::max(float(m_ctx->gridSize()), m_ctx->snap(size * factor)) / size;
    QVector3D s(1, 1, 1);
    s[m_axis] = factor;
    QMatrix4x4 m;
    m.translate(m_gizmoCenter);
    m.scale(s);
    m.translate(-m_gizmoCenter);
    QVector<MapObject> after = m_before;
    for (MapObject &o : after)
        o.transformWorld(m); // point entities only move; their box comes from the class
    m_ctx->doc()->previewReplace(after);
}

void SelectTool::applyRotation(const QVector3D &axis, float radians)
{
    if (m_ctx->snapEnabled())
    {
        constexpr float increment = 15.f * kPi / 180.f;
        radians = std::round(radians / increment) * increment;
    }
    QMatrix4x4 m;
    m.translate(m_gizmoCenter);
    m.rotate(radians * 180.f / kPi, axis);
    m.translate(-m_gizmoCenter);
    QVector<MapObject> after = m_before;
    if (std::abs(radians) > 1e-6f)
    {
        for (MapObject &o : after)
        {
            o.transformWorld(m);
            if (!o.solid)
                o.angles = MapOps::rotateAngles(o.angles, m);
        }
    }
    m_ctx->doc()->previewReplace(after);
}

void SelectTool::mousePress(Viewport *vp, QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int toolbarButton = hitGizmoToolbar(vp, e->position());
    if (toolbarButton >= 0)
    {
        m_gizmoMode = static_cast<GizmoMode>(toolbarButton);
        vp->update();
        return;
    }
    MapDocument *doc = m_ctx->doc();
    m_press = m_cur = e->pos();
    m_moved = false;
    m_mode = Mode::None;
    m_vp = vp;
    m_toggle = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
    m_wasSelected = false;

    if (vp->isOrtho())
    {
        const int h = hitHandle(vp, e->position());
        if (h >= 0)
        {
            m_handle = h;
            m_startBounds = solidSelectionBounds();
            captureBefore();
            if (m_rotateHandles)
            {
                m_gizmoCenter = m_startBounds.center();
                if (!vp->planePoint(e->position(), vp->camera().axisD(), m_gizmoCenter[vp->camera().axisD()], m_startPt))
                    return;
                m_mode = Mode::Rotate2D;
            }
            else
            {
                m_mode = Mode::Resize;
            }
            return;
        }
    }
    else
    {
        const int axis = hitGizmo(vp, e->position());
        if (axis >= 0)
        {
            m_axis = axis;
            m_gizmoCenter = doc->selectionBounds().center();
            if (m_gizmoMode == GizmoMode::Rotate)
            {
                if (!rotationVector(vp, e->position(), m_gizmoCenter, axis, m_rotationStart))
                    return;
                m_rotationAccumulated = 0.f;
            }
            else if (!axisParam(vp, e->position(), m_gizmoCenter, axis, m_axisStart))
            {
                return;
            }
            captureBefore();
            m_mode = Mode::Gizmo;
            return;
        }
    }

    const ObjectId id = vp->pickObject(e->position());
    if (id)
    {
        QSet<ObjectId> s = doc->selection();
        if (m_toggle)
        {
            if (s.contains(id))
            {
                s.remove(id);
                doc->setSelection(s);
                return;
            }
            s.insert(id);
        }
        else if (!s.contains(id))
        {
            s = {id};
            m_rotateHandles = false;
        }
        else
        {
            m_wasSelected = true;
        }
        doc->setSelection(s);
        beginMove(vp, e->position());
    }
    else
    {
        m_mode = Mode::Marquee;
    }
}

void SelectTool::mouseMove(Viewport *vp, QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton) || m_mode == Mode::None)
    {
        if (vp->isOrtho())
        {
            const int h = hitHandle(vp, e->position());
            if (h < 0)
                vp->unsetCursor();
            else if (m_rotateHandles)
                vp->setCursor(Qt::CrossCursor);
            else
            {
                const int hx = h / 3 - 1, hy = h % 3 - 1;
                vp->setCursor(hx == 0 ? Qt::SizeVerCursor : hy == 0 ? Qt::SizeHorCursor
                                                                    : (hx * hy > 0 ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor));
            }
        }
        else
        {
            const int hover = hitGizmoToolbar(vp, e->position());
            if (hover != m_gizmoHover)
            {
                m_gizmoHover = hover;
                vp->update();
            }
            if (hover >= 0)
                vp->setCursor(Qt::PointingHandCursor);
            else
                vp->unsetCursor();
        }
        return;
    }
    if (vp != m_vp)
        return;
    m_cur = e->pos();
    if (!m_moved && (m_cur - m_press).manhattanLength() < 3)
        return;
    m_moved = true;
    const Camera &cam = vp->camera();

    switch (m_mode)
    {
    case Mode::Move:
    {
        QVector3D cur;
        if (!vp->planePoint(e->position(), vp->isOrtho() ? cam.axisD() : 2, vp->isOrtho() ? 0.f : m_startPt.z(), cur))
            return;
        QVector3D delta = cur - m_startPt;
        delta[vp->isOrtho() ? cam.axisD() : 2] = 0.f;
        applyDelta(m_ctx->snap(delta));
        break;
    }
    case Mode::Gizmo:
    {
        if (m_gizmoMode == GizmoMode::Rotate)
        {
            QVector3D current;
            if (!rotationVector(vp, e->position(), m_gizmoCenter, m_axis, current))
                return;
            m_rotationAccumulated += std::atan2(
                QVector3D::dotProduct(kAxisDirs[m_axis], QVector3D::crossProduct(m_rotationStart, current)),
                QVector3D::dotProduct(m_rotationStart, current));
            m_rotationStart = current;
            applyRotation(kAxisDirs[m_axis], m_rotationAccumulated);
        }
        else
        {
            float t;
            if (!axisParam(vp, e->position(), m_gizmoCenter, m_axis, t))
                return;
            const float delta = t - m_axisStart;
            if (m_gizmoMode == GizmoMode::Move)
                applyDelta(kAxisDirs[m_axis] * m_ctx->snap(delta));
            else
                applyScale(delta);
        }
        break;
    }
    case Mode::Rotate2D:
    {
        const int d = cam.axisD();
        QVector3D cur;
        if (!vp->planePoint(e->position(), d, m_gizmoCenter[d], cur))
            return;
        QVector3D a = m_startPt - m_gizmoCenter, b = cur - m_gizmoCenter;
        a[d] = 0.f;
        b[d] = 0.f;
        if (a.lengthSquared() < 1e-6f || b.lengthSquared() < 1e-6f)
            return;
        const QVector3D axis = kAxisDirs[d];
        const float angle = std::atan2(QVector3D::dotProduct(axis, QVector3D::crossProduct(a, b)), QVector3D::dotProduct(a, b));
        applyRotation(axis, angle);
        break;
    }
    case Mode::Resize:
        applyResize(vp, e->position());
        break;
    case Mode::Marquee:
        vp->update();
        break;
    default:
        break;
    }
}

void SelectTool::mouseRelease(Viewport *vp, QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_mode == Mode::None)
        return;
    MapDocument *doc = m_ctx->doc();
    const Mode mode = m_mode;
    m_mode = Mode::None; // not busy any more: the commit below may emit signals
    switch (mode)
    {
    case Mode::Move:
        if (m_moved)
            doc->commitFromPreview(m_before, tr("Move Objects"));
        else if (m_wasSelected && vp->isOrtho() && solidSelectionBounds().valid())
            m_rotateHandles = !m_rotateHandles;
        break;
    case Mode::Gizmo:
        if (m_moved)
            doc->commitFromPreview(m_before, m_gizmoMode == GizmoMode::Move    ? tr("Move Objects")
                                             : m_gizmoMode == GizmoMode::Scale ? tr("Scale Objects")
                                                                               : tr("Rotate Objects"));
        break;
    case Mode::Rotate2D:
        if (m_moved)
            doc->commitFromPreview(m_before, tr("Rotate Objects"));
        break;
    case Mode::Resize:
        if (m_moved)
            doc->commitFromPreview(m_before, tr("Resize Objects"));
        break;
    case Mode::Marquee:
    {
        QSet<ObjectId> sel = m_toggle ? doc->selection() : QSet<ObjectId>();
        if (m_moved)
        {
            const QRectF r = QRectF(m_press, m_cur).normalized();
            for (const auto &kvp : doc->objects())
            {
                const MapObject &o = kvp.second;
                if (!doc->isVisible(o))
                    continue;
                const AABB b = o.worldBounds();
                bool inside = true;
                if (vp->isOrtho())
                {
                    for (int i = 0; i < 8 && inside; ++i)
                    {
                        QPointF s;
                        inside = vp->camera().project(b.corner(i), s) && r.contains(s);
                    }
                }
                else
                {
                    QPointF s;
                    inside = vp->camera().project(b.center(), s) && r.contains(s);
                }
                if (inside)
                    sel.insert(o.id);
            }
        }
        if (!sel.isEmpty() || !m_toggle)
            m_rotateHandles = false;
        doc->setSelection(sel);
        break;
    }
    default:
        break;
    }
    m_before.clear();
    vp->update();
}

void SelectTool::keyPress(Viewport *vp, QKeyEvent *e)
{
    MapDocument *doc = m_ctx->doc();
    if (e->key() == Qt::Key_Escape)
    {
        if (m_mode != Mode::None)
            cancelDrag();
        else
            doc->clearSelection();
        return;
    }
    if (doc->selection().isEmpty() || m_mode != Mode::None)
        return;

    const Camera &cam = vp->camera();
    const float g = float(std::max(1, m_ctx->gridSize()));
    QVector3D d;
    switch (e->key())
    {
    case Qt::Key_Left:
        d[vp->isOrtho() ? cam.axisH() : 0] = -g;
        break;
    case Qt::Key_Right:
        d[vp->isOrtho() ? cam.axisH() : 0] = g;
        break;
    case Qt::Key_Up:
        d[vp->isOrtho() ? cam.axisV() : 1] = g;
        break;
    case Qt::Key_Down:
        d[vp->isOrtho() ? cam.axisV() : 1] = -g;
        break;
    case Qt::Key_PageUp:
        d[2] = g;
        break;
    case Qt::Key_PageDown:
        d[2] = -g;
        break;
    default:
        return;
    }
    QVector<MapObject> after;
    for (ObjectId id : doc->selectionList())
        if (const MapObject *o = doc->find(id))
        {
            MapObject c = *o;
            c.origin += d;
            after.push_back(c);
        }
    doc->modify(after, tr("Nudge Objects"), "nudge");
}

void SelectTool::draw(Viewport *vp, Batch &b)
{
    if (vp->isOrtho())
    {
        const AABB sb = solidSelectionBounds();
        if (!sb.valid())
            return;
        for (int i = 0; i < 9; ++i)
        {
            if (i == 4)
                continue;
            QVector3D w;
            handleScreenPos(vp, i, sb, &w);
            if (m_rotateHandles)
            {
                if (i % 2 == 0)
                    circleMarker(b, vp, w, 5.f, QColor(255, 255, 255));
            }
            else
            {
                marker(b, vp, w, 3.5f, QColor(255, 255, 255));
            }
        }
    }
    else if (!m_ctx->doc()->selection().isEmpty())
    {
        const QVector3D c = m_mode == Mode::Gizmo ? m_gizmoCenter : m_ctx->doc()->selectionBounds().center();
        const float L = gizmoLength(vp, c);
        for (int i = 0; i < 3; ++i)
        {
            QColor col = kAxisColors[i];
            if ((m_mode == Mode::Gizmo && m_axis == i))
                col = QColor(255, 235, 90);
            if (m_gizmoMode == GizmoMode::Rotate)
            {
                QVector3D u, v;
                gizmoPlaneBasis(i, u, v);
                QVector3D previous = c + u * L;
                for (int segment = 1; segment <= 48; ++segment)
                {
                    const float angle = 2.f * kPi * segment / 48.f;
                    const QVector3D current = c + (u * std::cos(angle) + v * std::sin(angle)) * L;
                    b.line(previous, current, col, true);
                    previous = current;
                }
            }
            else
            {
                const QVector3D tip = c + kAxisDirs[i] * L;
                if (m_gizmoMode == GizmoMode::Scale)
                {
                    b.line(c, tip, col, true);
                    const float k = L * 0.07f;
                    b.solidBox(AABB(tip - QVector3D(k, k, k), tip + QVector3D(k, k, k)), col, true);
                }
                else
                {
                    QVector3D u, v;
                    gizmoPlaneBasis(i, u, v);
                    const float headLength = L * 0.18f;
                    const float headRadius = L * 0.075f;
                    const QVector3D base = tip - kAxisDirs[i] * headLength;
                    b.line(c, base, col, true);
                    for (int segment = 0; segment < 8; ++segment)
                    {
                        const float a0 = 2.f * kPi * segment / 8.f;
                        const float a1 = 2.f * kPi * (segment + 1) / 8.f;
                        const QVector3D p0 = base + (u * std::cos(a0) + v * std::sin(a0)) * headRadius;
                        const QVector3D p1 = base + (u * std::cos(a1) + v * std::sin(a1)) * headRadius;
                        b.tri(tip, p0, p1, col, true);
                    }
                }
            }
        }
    }
}

void SelectTool::paintOverlay(Viewport *vp, QPainter &p)
{
    if (!vp->isOrtho())
    {
        const QRectF first = gizmoToolbarButtonRect(vp, 0);
        const QRectF last = gizmoToolbarButtonRect(vp, 2);
        const QRectF toolbar = QRectF(first.left() - 4.0, first.top() - 4.0,
                                      last.right() - first.left() + 8.0,
                                      first.height() + 8.0);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(110, 115, 125, 220), 1.0));
        p.setBrush(QColor(28, 30, 35, 225));
        p.drawRoundedRect(toolbar, 6.0, 6.0);

        const QString labels[] = {tr("M"), tr("S"), tr("R")};
        for (int i = 0; i < 3; ++i)
        {
            const bool active = int(m_gizmoMode) == i;
            const bool hovered = m_gizmoHover == i;
            const QRectF button = gizmoToolbarButtonRect(vp, i);
            p.setPen(active ? QPen(QColor(255, 190, 105), 1.0)
                            : QPen(QColor(95, 100, 110, hovered ? 230 : 150), 1.0));
            p.setBrush(active    ? QColor(130, 72, 28, 235)
                       : hovered ? QColor(70, 74, 82, 235)
                                 : QColor(45, 48, 55, 210));
            p.drawRoundedRect(button, 3.0, 3.0);
            p.setPen(active ? QColor(255, 242, 225) : QColor(220, 222, 228));
            QFont buttonFont = p.font();
            buttonFont.setBold(active);
            p.setFont(buttonFont);
            p.drawText(button, Qt::AlignCenter, labels[i]);
        }
        p.restore();
    }
    if (vp == m_vp && m_moved && (m_mode == Mode::Resize || m_mode == Mode::Rotate2D || m_mode == Mode::Move))
    {
        const AABB b = m_ctx->doc()->selectionBounds();
        if (b.valid())
        {
            const QVector3D s = b.size();
            const QString text = m_mode == Mode::Rotate2D
                                     ? tr("Rotating")
                                     : QString("%1 x %2 x %3").arg(s.x(), 0, 'g', 5).arg(s.y(), 0, 'g', 5).arg(s.z(), 0, 'g', 5);
            drawLabel(p, QPointF(m_cur) + QPointF(14, 14), text);
        }
    }
    if (m_mode == Mode::Marquee && m_moved && vp == m_vp)
    {
        p.setPen(QPen(kOrange, 1, Qt::DashLine));
        p.setBrush(QColor(kOrange.red(), kOrange.green(), kOrange.blue(), 30));
        p.drawRect(QRect(m_press, m_cur).normalized());
    }
}

AABB BlockTool::ghost() const
{
    AABB b;
    b.mn[m_h] = std::min(m_a[m_h], m_b[m_h]);
    b.mx[m_h] = std::max(m_a[m_h], m_b[m_h]);
    b.mn[m_v] = std::min(m_a[m_v], m_b[m_v]);
    b.mx[m_v] = std::max(m_a[m_v], m_b[m_v]);
    b.mn[m_d] = m_depthMin;
    b.mx[m_d] = m_depthMax;
    return b;
}

void BlockTool::mousePress(Viewport *vp, QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const Camera &cam = vp->camera();
    QVector3D w;
    if (vp->isOrtho())
    {
        m_h = cam.axisH();
        m_v = cam.axisV();
        m_d = cam.axisD();
        // Like Hammer: reuse the selection's extent along the depth axis when there is one.
        const AABB sel = m_ctx->doc()->selectionBounds();
        if (sel.valid() && sel.size()[m_d] >= 1.f)
        {
            m_depthMin = sel.mn[m_d];
            m_depthMax = sel.mx[m_d];
        }
        else
        {
            m_depthMin = 0.f;
            m_depthMax = float(m_height);
        }
        if (!vp->planePoint(e->position(), m_d, m_depthMin, w))
            return;
    }
    else
    {
        if (!vp->pickPoint(e->position(), w))
            return;
        m_h = 0;
        m_v = 1;
        m_d = 2;
        m_depthMin = m_ctx->snap(w.z());
        m_depthMax = m_depthMin + float(m_height);
    }
    m_a = m_b = m_ctx->snap(w);
    m_vp = vp;
    m_active = true;
    vp->update();
}

void BlockTool::mouseMove(Viewport *vp, QMouseEvent *e)
{
    if (!m_active || vp != m_vp)
        return;
    QVector3D w;
    if (vp->planePoint(e->position(), m_d, m_depthMin, w))
    {
        m_b = m_ctx->snap(w);
        for (Viewport *v : m_ctx->viewports())
            v->update();
    }
}

void BlockTool::mouseRelease(Viewport *vp, QMouseEvent *e)
{
    if (!m_active || e->button() != Qt::LeftButton)
        return;
    m_active = false;
    const AABB g = ghost();
    if (g.size()[m_h] < 1.f || g.size()[m_v] < 1.f)
    {
        for (Viewport *v : m_ctx->viewports())
            v->update();
        return;
    }
    Primitives::Params params = m_params;
    params.material = m_ctx->currentMaterial();
    const QVector<Brush> brushes = Primitives::build(g, params);
    if (brushes.isEmpty())
    {
        m_ctx->log(tr("Block tool: that %1 is too small to build.").arg(Primitives::name(m_params.kind).toLower()), LogLevel::Warning);
        vp->update();
        return;
    }
    QVector<MapObject> objs;
    for (const Brush &b : brushes)
    {
        MapObject o;
        o.solid = true;
        o.setWorldBrushes({b});
        objs.push_back(o);
    }
    m_ctx->doc()->addObjects(objs, tr("Create %1").arg(Primitives::name(m_params.kind)));
}

void BlockTool::keyPress(Viewport *vp, QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && m_active)
    {
        m_active = false;
        vp->update();
    }
}

void BlockTool::draw(Viewport *, Batch &b)
{
    if (!m_active)
        return;
    const AABB g = ghost();
    if (g.size()[m_h] < 1.f || g.size()[m_v] < 1.f)
        return;
    const QVector<Brush> brushes = Primitives::build(g, m_params);
    if (brushes.isEmpty())
    {
        b.wireBox(g, kOrange, true);
        return;
    }
    b.wireBox(g, QColor(kOrange.red(), kOrange.green(), kOrange.blue(), 70), true);
    for (const Brush &br : brushes)
    {
        b.brushFaces(br, QColor(kOrange.red(), kOrange.green(), kOrange.blue(), 45), true);
        b.brushEdges(br, kOrange, true);
    }
}

void BlockTool::paintOverlay(Viewport *vp, QPainter &p)
{
    if (!m_active || vp != m_vp)
        return;
    const AABB g = ghost();
    const QVector3D s = g.size();
    QPointF at;
    if (!vp->camera().project(QVector3D(g.mx.x(), g.mx.y(), g.mx.z()), at))
        return;
    drawLabel(p, at + QPointF(10, 6), QString("%1  %2 x %3 x %4").arg(Primitives::name(m_params.kind)).arg(s.x()).arg(s.y()).arg(s.z()));
}

QWidget *BlockTool::createOptionsPanel(QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *f = new QFormLayout(w);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *hint = new QLabel(tr("Drag in a 2D view or on a surface in the 3D view. In 2D the depth follows the current selection, like Hammer."), w);
    hint->setWordWrap(true);
    hint->setObjectName("hint");
    f->addRow(hint);

    auto *shape = new QComboBox(w);
    for (Primitives::Kind k : {Primitives::Kind::Block, Primitives::Kind::Wedge, Primitives::Kind::Cylinder,
                               Primitives::Kind::Spike, Primitives::Kind::Sphere, Primitives::Kind::Arch, Primitives::Kind::Stairs})
        shape->addItem(Primitives::name(k), int(k));
    f->addRow(tr("Primitive"), shape);

    auto *sides = new QSpinBox(w);
    sides->setRange(3, 64);
    sides->setValue(m_params.sides);
    f->addRow(tr("Sides"), sides);

    auto *arc = new QDoubleSpinBox(w);
    arc->setRange(1, 360);
    arc->setValue(m_params.archDegrees);
    arc->setSuffix(QStringLiteral("°"));
    f->addRow(tr("Arc"), arc);

    auto *start = new QDoubleSpinBox(w);
    start->setRange(-360, 360);
    start->setValue(m_params.archStart);
    start->setSuffix(QStringLiteral("°"));
    f->addRow(tr("Start angle"), start);

    auto *wall = new QDoubleSpinBox(w);
    wall->setRange(1, 4096);
    wall->setValue(m_params.wallWidth);
    f->addRow(tr("Wall width"), wall);

    auto *steps = new QSpinBox(w);
    steps->setRange(1, 256);
    steps->setValue(m_params.steps);
    f->addRow(tr("Steps"), steps);

    auto *dir = new QComboBox(w);
    dir->addItems({tr("Rising to +Y (north)"), tr("Rising to +X (east)"), tr("Rising to -Y (south)"), tr("Rising to -X (west)")});
    f->addRow(tr("Direction"), dir);

    auto *depth = new QSpinBox(w);
    depth->setRange(1, 65536);
    depth->setValue(m_height);
    depth->setToolTip(tr("Height used when nothing is selected, and for blocks drawn in the 3D view."));
    f->addRow(tr("Default depth"), depth);

    auto refreshRows = [=]
    {
        const auto k = Primitives::Kind(shape->currentData().toInt());
        const bool round = k == Primitives::Kind::Cylinder || k == Primitives::Kind::Spike ||
                           k == Primitives::Kind::Sphere || k == Primitives::Kind::Arch;
        f->setRowVisible(sides, round);
        f->setRowVisible(arc, k == Primitives::Kind::Arch);
        f->setRowVisible(start, k == Primitives::Kind::Arch);
        f->setRowVisible(wall, k == Primitives::Kind::Arch);
        f->setRowVisible(steps, k == Primitives::Kind::Stairs);
        f->setRowVisible(dir, k == Primitives::Kind::Stairs || k == Primitives::Kind::Wedge);
    };
    refreshRows();

    connect(shape, qOverload<int>(&QComboBox::currentIndexChanged), this, [=]
            {
        m_params.kind = Primitives::Kind(shape->currentData().toInt());
        refreshRows(); });
    connect(sides, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v)
            { m_params.sides = v; });
    connect(arc, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v)
            { m_params.archDegrees = float(v); });
    connect(start, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v)
            { m_params.archStart = float(v); });
    connect(wall, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v)
            { m_params.wallWidth = float(v); });
    connect(steps, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v)
            { m_params.steps = v; });
    connect(dir, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int v)
            { m_params.direction = v; });
    connect(depth, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v)
            { m_height = v; });
    return w;
}

void EntityTool::mousePress(Viewport *vp, QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const ClassDef *def = m_ctx->registry()->resolved(m_ctx->entityClass());
    if (!def || def->kind != ClassKind::Point)
    {
        m_ctx->log(tr("Entity tool: pick a point entity class in Tool Options first."), LogLevel::Warning);
        return;
    }
    QVector3D pt;
    if (!vp->pickPoint(e->position(), pt))
        return;
    MapObject o;
    o.origin = m_ctx->snap(pt);
    applyClassDefaults(o, *def);
    o.name = m_ctx->doc()->uniqueName(def->name);
    m_ctx->doc()->addObject(o, tr("Create Entity"));
}

void EntityTool::mouseMove(Viewport *vp, QMouseEvent *e)
{
    QVector3D pt;
    m_hover = vp->pickPoint(e->position(), pt);
    if (m_hover)
        m_hoverPt = m_ctx->snap(pt);
    for (Viewport *v : m_ctx->viewports())
        v->update();
}

void EntityTool::draw(Viewport *, Batch &b)
{
    if (!m_hover)
        return;
    const ClassDef *def = m_ctx->registry()->resolved(m_ctx->entityClass());
    if (!def || def->kind != ClassKind::Point)
        return;
    const AABB bb(m_hoverPt + (def->hasSize ? def->mins : QVector3D(-8, -8, -8)), m_hoverPt + (def->hasSize ? def->maxs : QVector3D(8, 8, 8)));
    QColor c = def->hasColor ? def->color : QColor(80, 160, 255);
    b.wireBox(bb, c.lighter(130), true);
    c.setAlpha(70);
    b.solidBox(bb, c, true);
}

QWidget *EntityTool::createOptionsPanel(QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(6, 6, 6, 6);
    auto *filter = new QLineEdit(w);
    filter->setPlaceholderText(tr("Filter entity classes..."));
    filter->setClearButtonEnabled(true);
    auto *list = new QListWidget(w);
    auto *desc = new QLabel(w);
    desc->setWordWrap(true);
    desc->setMinimumHeight(48);
    lay->addWidget(new QLabel(tr("Point entity class"), w));
    lay->addWidget(filter);
    lay->addWidget(list, 1);
    lay->addWidget(desc);

    EditorContext *ctx = m_ctx;
    auto populate = [=]
    {
        QSignalBlocker block(list);
        list->clear();
        const QString f = filter->text().trimmed();
        for (const ClassDef *c : ctx->registry()->classes(ClassKind::Point))
        {
            if (!f.isEmpty() && !c->name.contains(f, Qt::CaseInsensitive))
                continue;
            const ClassDef *r = ctx->registry()->resolved(c->name);
            QPixmap pm(12, 12);
            pm.fill(r && r->hasColor ? r->color : QColor(80, 160, 255));
            auto *it = new QListWidgetItem(QIcon(pm), c->name, list);
            if (c->name.compare(ctx->entityClass(), Qt::CaseInsensitive) == 0)
                list->setCurrentItem(it);
        }
    };
    populate();
    connect(filter, &QLineEdit::textChanged, w, populate);
    connect(ctx->registry(), &EntityRegistry::changed, w, populate);
    connect(list, &QListWidget::currentTextChanged, w, [=](const QString &name)
            {
        if (name.isEmpty()) return;
        ctx->setEntityClass(name);
        const ClassDef* r = ctx->registry()->resolved(name);
        desc->setText(r ? r->description : QString()); });
    connect(ctx, &EditorContext::entityClassChanged, w, [=]
            {
        const auto items = list->findItems(ctx->entityClass(), Qt::MatchExactly);
        if (!items.isEmpty()) list->setCurrentItem(items.first()); });
    if (ctx->entityClass().isEmpty() && list->count())
        list->setCurrentRow(0);
    return w;
}

bool ClipTool::plane(Plane &out) const
{
    if (!m_hasLine)
        return false;
    const QVector3D dir = m_p[1] - m_p[0];
    QVector3D n = QVector3D::crossProduct(dir, kAxisDirs[m_axis]);
    if (n.lengthSquared() < 1e-6f)
        return false;
    n.normalize();
    out = {n, QVector3D::dotProduct(n, m_p[0])};
    return true;
}

void ClipTool::mousePress(Viewport *vp, QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (!vp->isOrtho())
    {
        m_ctx->log(tr("Clip tool: draw the clipping line in a Top/Front/Side view."), LogLevel::Warning);
        return;
    }
    const Camera &cam = vp->camera();
    if (m_hasLine && m_axis == cam.axisD())
    {
        for (int i = 0; i < 2; ++i)
        {
            QPointF s;
            if (cam.project(m_p[i], s) && QLineF(s, e->position()).length() < 8.0)
            {
                m_drag = i;
                m_vp = vp;
                return;
            }
        }
    }
    QVector3D w;
    if (!vp->planePoint(e->position(), cam.axisD(), 0.f, w))
        return;
    m_axis = cam.axisD();
    m_p[0] = m_p[1] = m_ctx->snap(w);
    m_p[0][m_axis] = m_p[1][m_axis] = 0.f;
    m_hasLine = true;
    m_drag = 1;
    m_vp = vp;
}

void ClipTool::mouseMove(Viewport *vp, QMouseEvent *e)
{
    if (m_drag < 0 || vp != m_vp)
        return;
    QVector3D w;
    if (!vp->planePoint(e->position(), m_axis, 0.f, w))
        return;
    w = m_ctx->snap(w);
    w[m_axis] = 0.f;
    m_p[m_drag] = w;
    for (Viewport *v : m_ctx->viewports())
        v->update();
}

void ClipTool::mouseRelease(Viewport *, QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_drag < 0)
        return;
    m_drag = -1;
    if ((m_p[1] - m_p[0]).length() < 0.5f)
        m_hasLine = false;
    for (Viewport *v : m_ctx->viewports())
        v->update();
}

void ClipTool::cycleKeep()
{
    m_keep = m_keep == MapOps::ClipKeep::Both    ? MapOps::ClipKeep::Front
             : m_keep == MapOps::ClipKeep::Front ? MapOps::ClipKeep::Back
                                                 : MapOps::ClipKeep::Both;
    emit keepChanged();
    for (Viewport *v : m_ctx->viewports())
        v->update();
}

void ClipTool::apply()
{
    Plane p;
    if (!plane(p))
    {
        m_ctx->log(tr("Clip tool: drag a clipping line first."), LogLevel::Warning);
        return;
    }
    MapOps::clip(m_ctx, p, m_keep);
    m_hasLine = false;
    for (Viewport *v : m_ctx->viewports())
        v->update();
}

void ClipTool::keyPress(Viewport *vp, QKeyEvent *e)
{
    switch (e->key())
    {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        apply();
        break;
    case Qt::Key_Tab:
        cycleKeep();
        break;
    case Qt::Key_Escape:
        m_hasLine = false;
        m_drag = -1;
        vp->update();
        break;
    default:
        break;
    }
}

void ClipTool::draw(Viewport *vp, Batch &b)
{
    if (!m_hasLine)
        return;
    Plane p;
    const bool havePlane = plane(p);
    if (havePlane)
    {
        const QColor kept(255, 200, 80), cut(220, 70, 60, 150);
        for (ObjectId id : m_ctx->doc()->selection())
        {
            const MapObject *o = m_ctx->doc()->find(id);
            if (!o || !o->solid)
                continue;
            for (const Brush &br : o->worldBrushes())
            {
                Brush front, back;
                br.split(p, &front, &back);
                if (!front.isValid() || !back.isValid())
                    continue;
                b.brushEdges(front, m_keep == MapOps::ClipKeep::Back ? cut : kept, true);
                b.brushEdges(back, m_keep == MapOps::ClipKeep::Front ? cut : kept, true);
                if (!vp->isOrtho())
                {
                    if (m_keep != MapOps::ClipKeep::Back)
                        b.brushFaces(front, QColor(255, 200, 80, 40), true);
                    if (m_keep != MapOps::ClipKeep::Front)
                        b.brushFaces(back, QColor(255, 200, 80, 40), true);
                }
            }
        }
    }
    if (vp->isOrtho() && vp->camera().axisD() == m_axis)
    {
        b.line(m_p[0], m_p[1], QColor(255, 255, 255), true);
        marker(b, vp, m_p[0], 3.5f, QColor(255, 255, 255));
        marker(b, vp, m_p[1], 3.5f, QColor(255, 255, 255));
    }
}

void ClipTool::paintOverlay(Viewport *vp, QPainter &p)
{
    if (!m_hasLine || !vp->isOrtho())
        return;
    const QString keep = m_keep == MapOps::ClipKeep::Both    ? tr("both sides")
                         : m_keep == MapOps::ClipKeep::Front ? tr("front side")
                                                             : tr("back side");
    drawLabel(p, QPointF(6, 32), tr("Clip: keep %1  ·  Tab cycles  ·  Enter applies").arg(keep));
}

QWidget *ClipTool::createOptionsPanel(QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *lay = new QVBoxLayout(w);
    auto *hint = new QLabel(tr("Drag a line across the selected solids in a 2D view. Drag the end points to adjust it."), w);
    hint->setWordWrap(true);
    hint->setObjectName("hint");
    lay->addWidget(hint);
    auto *group = new QButtonGroup(w);
    const QString labels[] = {tr("Keep both sides"), tr("Keep front side"), tr("Keep back side")};
    for (int i = 0; i < 3; ++i)
    {
        auto *r = new QRadioButton(labels[i], w);
        group->addButton(r, i);
        lay->addWidget(r);
    }
    group->button(int(m_keep))->setChecked(true);
    auto *applyButton = new QPushButton(tr("Clip (Enter)"), w);
    lay->addWidget(applyButton);
    lay->addStretch();
    connect(group, &QButtonGroup::idClicked, this, [this](int id)
            {
        m_keep = MapOps::ClipKeep(id);
        for (Viewport *v : m_ctx->viewports()) v->update(); });
    connect(this, &ClipTool::keepChanged, w, [this, group]
            { group->button(int(m_keep))->setChecked(true); });
    connect(applyButton, &QPushButton::clicked, this, &ClipTool::apply);
    return w;
}

void VertexTool::deactivate()
{
    cancel();
    m_selected.clear();
}

void VertexTool::cancel()
{
    if (m_dragging && m_moved && !m_before.isEmpty())
        m_ctx->doc()->previewReplace(m_before);
    m_dragging = m_moved = false;
    m_before.clear();
    for (Viewport *v : m_ctx->viewports())
        v->update();
}

QVector<VertexTool::VertexRef> VertexTool::verticesAt(Viewport *vp, const QPointF &p) const
{
    QVector<VertexRef> out;
    const MapDocument *doc = m_ctx->doc();
    for (ObjectId id : doc->selectionList())
    {
        const MapObject *o = doc->find(id);
        if (!o || !o->solid || !doc->isVisible(*o))
            continue;
        const QVector<Brush> world = o->worldBrushes();
        for (int bi = 0; bi < world.size(); ++bi)
            for (const QVector3D &v : world[bi].vertices())
            {
                QPointF s;
                if (vp->camera().project(v, s) && QLineF(s, p).length() < 7.0)
                    out.push_back({id, bi, v});
            }
    }
    return out;
}

void VertexTool::mousePress(Viewport *vp, QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QVector<VertexRef> hits = verticesAt(vp, e->position());
    const bool add = e->modifiers() & Qt::ControlModifier;
    if (hits.isEmpty())
    {
        if (!add)
            m_selected.clear();
        // Clicking an unselected solid selects it, so the tool is usable without switching back.
        if (const ObjectId id = vp->pickObject(e->position()))
        {
            const MapObject *o = m_ctx->doc()->find(id);
            if (o && o->solid && !m_ctx->doc()->selection().contains(id))
            {
                QSet<ObjectId> s = add ? m_ctx->doc()->selection() : QSet<ObjectId>();
                s.insert(id);
                m_ctx->doc()->setSelection(s);
            }
        }
        for (Viewport *v : m_ctx->viewports())
            v->update();
        return;
    }
    auto contains = [this](const VertexRef &r)
    {
        for (const VertexRef &s : m_selected)
            if (s.id == r.id && s.brush == r.brush && (s.world - r.world).lengthSquared() < 1e-4f)
                return true;
        return false;
    };
    const bool allSelected = std::all_of(hits.begin(), hits.end(), contains);
    if (!add && !allSelected)
        m_selected = hits;
    else if (add)
        for (const VertexRef &h : hits)
            if (!contains(h))
                m_selected.push_back(h);

    m_before.clear();
    for (ObjectId id : m_ctx->doc()->selectionList())
        if (const MapObject *o = m_ctx->doc()->find(id); o && o->solid)
            m_before.push_back(*o);
    const Camera &cam = vp->camera();
    m_planeAxis = vp->isOrtho() ? cam.axisD() : 2;
    const float planeValue = vp->isOrtho() ? 0.f : hits.first().world.z();
    if (!vp->planePoint(e->position(), m_planeAxis, planeValue, m_startPt))
        return;
    m_dragging = true;
    m_moved = false;
    m_vp = vp;
    m_press = e->pos();
    for (Viewport *v : m_ctx->viewports())
        v->update();
}

void VertexTool::applyOffset(const QVector3D &offset)
{
    QVector<MapObject> after = m_before;
    for (MapObject &o : after)
    {
        QVector<Brush> world = o.worldBrushes();
        bool changed = false;
        for (int bi = 0; bi < world.size(); ++bi)
        {
            QVector<QVector3D> pts = world[bi].vertices();
            bool moved = false;
            for (QVector3D &p : pts)
                for (const VertexRef &r : m_selected)
                    if (r.id == o.id && r.brush == bi && (r.world - p).lengthSquared() < 1e-4f)
                    {
                        p += offset;
                        moved = true;
                        break;
                    }
            if (!moved)
                continue;
            const Brush hull = Brush::convexHull(pts, world[bi].faces.first().material);
            if (hull.isValid())
            {
                world[bi] = hull;
                changed = true;
            }
        }
        if (changed)
            o.setWorldBrushes(world);
    }
    m_ctx->doc()->previewReplace(after);
}

void VertexTool::mouseMove(Viewport *vp, QMouseEvent *e)
{
    if (!m_dragging || vp != m_vp || m_selected.isEmpty())
        return;
    if (!m_moved && (e->pos() - m_press).manhattanLength() < 3)
        return;
    const float planeValue = vp->isOrtho() ? 0.f : m_selected.first().world.z();
    QVector3D cur;
    if (!vp->planePoint(e->position(), m_planeAxis, planeValue, cur))
        return;
    QVector3D offset = cur - m_startPt;
    offset[m_planeAxis] = 0.f;
    // Snap the grabbed vertex itself, not the offset, so off-grid vertices land on the grid.
    const QVector3D anchor = m_selected.first().world;
    QVector3D target = anchor + offset;
    for (int i = 0; i < 3; ++i)
        if (i != m_planeAxis)
            target[i] = m_ctx->snap(target[i]);
    offset = target - anchor;
    m_moved = true;
    applyOffset(offset);
    m_lastOffset = offset;
}

void VertexTool::mouseRelease(Viewport *, QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !m_dragging)
        return;
    m_dragging = false;
    if (m_moved)
    {
        m_ctx->doc()->commitFromPreview(m_before, tr("Move Vertices"));
        for (VertexRef &r : m_selected)
            r.world += m_lastOffset;
    }
    m_moved = false;
    m_before.clear();
    for (Viewport *v : m_ctx->viewports())
        v->update();
}

void VertexTool::keyPress(Viewport *vp, QKeyEvent *e)
{
    if (e->key() != Qt::Key_Escape)
        return;
    if (m_dragging)
        cancel();
    else
        m_selected.clear();
    vp->update();
}

void VertexTool::draw(Viewport *vp, Batch &b)
{
    const MapDocument *doc = m_ctx->doc();
    for (ObjectId id : doc->selectionList())
    {
        const MapObject *o = doc->find(id);
        if (!o || !o->solid || !doc->isVisible(*o))
            continue;
        const QVector<Brush> world = o->worldBrushes();
        for (int bi = 0; bi < world.size(); ++bi)
            for (const QVector3D &v : world[bi].vertices())
            {
                bool selected = false;
                for (const VertexRef &r : m_selected)
                {
                    const QVector3D cur = m_dragging && m_moved ? r.world + m_lastOffset : r.world;
                    if (r.id == id && r.brush == bi && (cur - v).lengthSquared() < 1e-3f)
                    {
                        selected = true;
                        break;
                    }
                }
                marker(b, vp, v, selected ? 4.f : 3.f, selected ? QColor(255, 60, 50) : QColor(255, 255, 255));
            }
    }
}
