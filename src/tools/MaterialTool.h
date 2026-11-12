#pragma once
#include "core/MapObject.h"
#include "tools/Tool.h"
#include <QPointer>
#include <QVector>

class QDoubleSpinBox;
class QLabel;
class QPointF;

struct MaterialFaceRef
{
    ObjectId object = 0;
    int brush = -1;
    int face = -1;
    bool operator==(const MaterialFaceRef &o) const { return object == o.object && brush == o.brush && face == o.face; }
};

namespace MaterialOps
{
    bool pickFace(EditorContext *ctx, Viewport *vp, const QPointF &pos, MaterialFaceRef &out);
    void applyToFaces(EditorContext *ctx, const QVector<MaterialFaceRef> &faces, const QString &material, const QString &undoText);
    bool applyToObjects(EditorContext *ctx, const QList<ObjectId> &ids, const QString &material);
    // A material dropped on a viewport: the face under the cursor in 3D (whole object with
    // `wholeObject`), the object under the cursor in 2D. Also makes it the current material.
    bool dropOnViewport(EditorContext *ctx, Viewport *vp, const QPointF &pos, const QString &material, bool wholeObject);
}

class MaterialTool : public Tool
{
    Q_OBJECT
public:
    explicit MaterialTool(EditorContext *ctx);

    QString id() const override { return "material"; }
    QString title() const override { return tr("Material"); }
    QString shortcut() const override { return "Shift+A"; }
    QString statusHint() const override;

    void deactivate() override;
    void mousePress(Viewport *vp, QMouseEvent *e) override;
    void keyPress(Viewport *vp, QKeyEvent *e) override;
    void draw(Viewport *vp, Batch &b) override;
    QWidget *createOptionsPanel(QWidget *parent) override;

    using FaceRef = MaterialFaceRef;

private:
    void pickMaterial(const FaceRef &face);
    QVector<FaceRef> mappingTargets() const;
    void applyMapping();
    void refreshMappingFields();
    void pruneSelection();

    QVector<FaceRef> m_selectedFaces;
    QPointer<QLabel> m_materialLabel;
    QPointer<QDoubleSpinBox> m_scaleU, m_scaleV, m_offsetU, m_offsetV, m_rotation;
    bool m_updatingFields = false;
};
