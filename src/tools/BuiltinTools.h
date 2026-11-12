#pragma once
#include "core/Geometry.h"
#include "core/MapObject.h"
#include "core/MapOperations.h"
#include "tools/Tool.h"
#include <QPoint>
#include <QRectF>
#include <QVector>

class QSpinBox;
class QWidget;

class SelectTool : public Tool
{
    Q_OBJECT
public:
    explicit SelectTool(EditorContext *ctx);
    QString id() const override { return "select"; }
    QString title() const override { return tr("Selection"); }
    QString shortcut() const override { return "Shift+S"; }
    QString statusHint() const override { return tr("Click to select, drag to move. Click a selected object again in a 2D view to switch between scale and rotate handles. Esc cancels a drag."); }

    void deactivate() override;
    void mousePress(Viewport *, QMouseEvent *) override;
    void mouseMove(Viewport *, QMouseEvent *) override;
    void mouseRelease(Viewport *, QMouseEvent *) override;
    void keyPress(Viewport *, QKeyEvent *) override;
    void draw(Viewport *, Batch &) override;
    void paintOverlay(Viewport *, QPainter &) override;
    bool isBusy() const override { return m_mode != Mode::None; }
    void cancel() override { cancelDrag(); }

private:
    enum class Mode
    {
        None,
        Move,
        Resize,
        Rotate2D,
        Gizmo,
        Marquee
    };

    enum class GizmoMode
    {
        Move,
        Scale,
        Rotate
    };

    AABB solidSelectionBounds() const;
    QPointF handleScreenPos(Viewport *, int idx, const AABB &, QVector3D *world = nullptr) const;
    int hitHandle(Viewport *, const QPointF &) const;
    float gizmoLength(Viewport *, const QVector3D &center) const;
    int hitGizmo(Viewport *, const QPointF &) const;
    QRectF gizmoToolbarButtonRect(Viewport *, int index) const;
    int hitGizmoToolbar(Viewport *, const QPointF &) const;
    bool axisParam(Viewport *, const QPointF &, const QVector3D &center, int axis, float &t) const;
    bool rotationVector(Viewport *, const QPointF &, const QVector3D &center, int axis, QVector3D &vector) const;
    void captureBefore();
    void beginMove(Viewport *, const QPointF &);
    void applyDelta(const QVector3D &delta);
    void applyResize(Viewport *, const QPointF &);
    void applyScale(float delta);
    void applyRotation(const QVector3D &axis, float radians);
    void cancelDrag();

    Mode m_mode = Mode::None;
    GizmoMode m_gizmoMode = GizmoMode::Move;
    bool m_rotateHandles = false;
    bool m_wasSelected = false;
    int m_gizmoHover = -1;
    Viewport *m_vp = nullptr;
    QPoint m_press, m_cur;
    bool m_moved = false;
    bool m_toggle = false;
    QVector<MapObject> m_before;
    QVector3D m_startPt;
    int m_handle = -1;
    AABB m_startBounds;
    int m_axis = -1;
    float m_axisStart = 0.f;
    QVector3D m_gizmoCenter;
    QVector3D m_rotationStart;
    float m_rotationAccumulated = 0.f;
};

class BlockTool : public Tool
{
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return "block"; }
    QString title() const override { return tr("Block"); }
    QString shortcut() const override { return "Shift+B"; }
    QString statusHint() const override { return tr("Drag in a 2D view, or on a surface in 3D, to create the selected primitive. Esc cancels."); }

    void deactivate() override { m_active = false; }
    void mousePress(Viewport *, QMouseEvent *) override;
    void mouseMove(Viewport *, QMouseEvent *) override;
    void mouseRelease(Viewport *, QMouseEvent *) override;
    void keyPress(Viewport *, QKeyEvent *) override;
    void draw(Viewport *, Batch &) override;
    void paintOverlay(Viewport *, QPainter &) override;
    QWidget *createOptionsPanel(QWidget *parent) override;
    bool isBusy() const override { return m_active; }
    void cancel() override { m_active = false; }

private:
    AABB ghost() const;
    bool m_active = false;
    Viewport *m_vp = nullptr;
    int m_h = 0, m_v = 1, m_d = 2;
    QVector3D m_a, m_b;
    float m_depthMin = 0.f, m_depthMax = 64.f;
    int m_height = 64;
    Primitives::Params m_params;
};

class EntityTool : public Tool
{
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return "entity"; }
    QString title() const override { return tr("Entity"); }
    QString shortcut() const override { return "Shift+E"; }
    QString statusHint() const override { return tr("Choose a class in Tool Options, then click to place an entity."); }

    void deactivate() override { m_hover = false; }
    void mousePress(Viewport *, QMouseEvent *) override;
    void mouseMove(Viewport *, QMouseEvent *) override;
    void draw(Viewport *, Batch &) override;
    QWidget *createOptionsPanel(QWidget *parent) override;

private:
    bool m_hover = false;
    QVector3D m_hoverPt;
};

class ClipTool : public Tool
{
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return "clip"; }
    QString title() const override { return tr("Clip"); }
    QString shortcut() const override { return "Shift+X"; }
    QString statusHint() const override { return tr("Drag a line through the selection in a 2D view. Drag the end points to adjust, Tab cycles the kept side, Enter clips, Esc clears."); }

    void deactivate() override { m_hasLine = false; m_drag = -1; }
    void mousePress(Viewport *, QMouseEvent *) override;
    void mouseMove(Viewport *, QMouseEvent *) override;
    void mouseRelease(Viewport *, QMouseEvent *) override;
    void keyPress(Viewport *, QKeyEvent *) override;
    void draw(Viewport *, Batch &) override;
    void paintOverlay(Viewport *, QPainter &) override;
    QWidget *createOptionsPanel(QWidget *parent) override;

signals:
    void keepChanged();

private:
    bool plane(Plane &out) const;
    void apply();
    void cycleKeep();

    bool m_hasLine = false;
    int m_drag = -1; // 0/1 = end point, 2 = new line
    int m_axis = 2;  // depth axis of the view the line was drawn in
    QVector3D m_p[2];
    Viewport *m_vp = nullptr;
    MapOps::ClipKeep m_keep = MapOps::ClipKeep::Both;
};

// Brushes are rebuilt as the convex hull of their vertices, so they always stay valid.
class VertexTool : public Tool
{
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return "vertex"; }
    QString title() const override { return tr("Vertex"); }
    QString shortcut() const override { return "Shift+V"; }
    QString statusHint() const override { return tr("Click a vertex of a selected solid (Ctrl adds), then drag to move it. Overlapping vertices move together. Esc cancels."); }

    void deactivate() override;
    void mousePress(Viewport *, QMouseEvent *) override;
    void mouseMove(Viewport *, QMouseEvent *) override;
    void mouseRelease(Viewport *, QMouseEvent *) override;
    void keyPress(Viewport *, QKeyEvent *) override;
    void draw(Viewport *, Batch &) override;
    bool isBusy() const override { return m_dragging; }
    void cancel() override;

private:
    struct VertexRef
    {
        ObjectId id;
        int brush;
        QVector3D world; // position when the drag started
    };
    QVector<VertexRef> verticesAt(Viewport *, const QPointF &) const;
    void applyOffset(const QVector3D &offset);

    QVector<VertexRef> m_selected;
    QVector<MapObject> m_before;
    bool m_dragging = false, m_moved = false;
    Viewport *m_vp = nullptr;
    QPoint m_press;
    QVector3D m_startPt;
    QVector3D m_lastOffset;
    int m_planeAxis = 2;
};
