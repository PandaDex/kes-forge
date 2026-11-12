#pragma once
#include "core/Geometry.h"
#include "core/MapObject.h"
#include <QMap>
#include <QString>
#include <QVector>

class EditorContext;

namespace MapOps
{
    enum class ClipKeep
    {
        Both,
        Front,
        Back
    };

    // Replaces the brushes of solid objects with new world-space pieces in one undo step.
    // Static geometry is split into one object per piece; solid entities keep all pieces.
    // An empty piece list deletes the object. Returns the ids of every resulting object.
    QList<ObjectId> replaceSolids(EditorContext *ctx, const QMap<ObjectId, QVector<Brush>> &pieces, const QString &text);

    void clip(EditorContext *ctx, const Plane &plane, ClipKeep keep);
    void carve(EditorContext *ctx);
    void hollow(EditorContext *ctx, float wallThickness);
    void tieToEntity(EditorContext *ctx, const QString &classname);
    void moveToWorld(EditorContext *ctx);
    void transformSelection(EditorContext *ctx, const QMatrix4x4 &m, const QString &text, bool rotatePointAngles = false);
    void flip(EditorContext *ctx, int axis);
    void snapToGrid(EditorContext *ctx);
    void hideSelected(EditorContext *ctx);
    void hideUnselected(EditorContext *ctx);
    void unhideAll(EditorContext *ctx);
    void selectSameClass(EditorContext *ctx);

    void copy(EditorContext *ctx);
    void cut(EditorContext *ctx);
    bool paste(EditorContext *ctx);
    bool canPaste();

    // Euler helpers (Source convention: pitch about Y, yaw about Z, roll about X; degrees).
    QVector3D rotateAngles(const QVector3D &angles, const QMatrix4x4 &rotation);
}
