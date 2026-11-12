#include "tools/MaterialTool.h"
#include "core/EditorContext.h"
#include "render/Batch.h"
#include "render/Viewport.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
    const QColor kFaceHighlight(80, 220, 255, 90);
    const QColor kFaceOutline(80, 220, 255);

    QVector<Brush> &editableBrushes(MapObject &o)
    {
        o.ensureBrushes();
        return o.brushes;
    }
}

bool MaterialOps::pickFace(EditorContext *ctx, Viewport *vp, const QPointF &pos, MaterialFaceRef &out)
{
    const Ray ray = vp->camera().ray(pos);
    float best = 1e30f;
    bool found = false;
    for (const auto &entry : ctx->doc()->objects())
    {
        const MapObject &o = entry.second;
        if (!o.solid || !ctx->doc()->isVisible(o) || !o.worldBounds().intersect(ray))
            continue;
        const QVector<Brush> brushes = o.worldBrushes();
        for (int bi = 0; bi < brushes.size(); ++bi)
        {
            float t = 0.f;
            if (!brushes[bi].intersectRay(ray, &t) || t >= best)
                continue;
            // The hit face is the one whose plane contains the hit point and faces the ray.
            const QVector3D hit = ray.origin + ray.dir * t;
            int bestFace = -1;
            float bestDistance = 1e30f;
            for (int fi = 0; fi < brushes[bi].faces.size(); ++fi)
            {
                const Plane plane = brushes[bi].faces[fi].plane();
                if (QVector3D::dotProduct(plane.normal, ray.dir) >= 0.f)
                    continue;
                const float d = std::abs(plane.distanceTo(hit));
                if (d < bestDistance)
                {
                    bestDistance = d;
                    bestFace = fi;
                }
            }
            if (bestFace < 0)
                continue;
            best = t;
            out = {o.id, bi, bestFace};
            found = true;
        }
    }
    return found;
}

void MaterialOps::applyToFaces(EditorContext *ctx, const QVector<MaterialFaceRef> &faces, const QString &material, const QString &undoText)
{
    QMap<ObjectId, MapObject> changed;
    for (const MaterialFaceRef &ref : faces)
    {
        if (!changed.contains(ref.object))
        {
            const MapObject *o = ctx->doc()->find(ref.object);
            if (!o)
                continue;
            changed.insert(ref.object, *o);
        }
        QVector<Brush> &brushes = editableBrushes(changed[ref.object]);
        if (ref.brush >= 0 && ref.brush < brushes.size() && ref.face >= 0 && ref.face < brushes[ref.brush].faces.size())
            brushes[ref.brush].faces[ref.face].material = material;
    }
    if (!changed.isEmpty())
        ctx->doc()->modify(changed.values().toVector(), undoText);
}

bool MaterialOps::applyToObjects(EditorContext *ctx, const QList<ObjectId> &ids, const QString &material)
{
    QVector<MapObject> changed;
    for (ObjectId id : ids)
    {
        const MapObject *o = ctx->doc()->find(id);
        if (!o || !o->solid)
            continue;
        MapObject copy = *o;
        for (Brush &b : editableBrushes(copy))
            b.setMaterial(material);
        changed.push_back(copy);
    }
    if (changed.isEmpty())
        return false;
    ctx->doc()->modify(changed, QObject::tr("Apply Material"));
    return true;
}

bool MaterialOps::dropOnViewport(EditorContext *ctx, Viewport *vp, const QPointF &pos, const QString &material, bool wholeObject)
{
    ctx->setCurrentMaterial(material);
    if (vp->isOrtho())
    {
        const ObjectId id = vp->pickObject(pos);
        return id && applyToObjects(ctx, {id}, material);
    }
    MaterialFaceRef face;
    if (!pickFace(ctx, vp, pos, face))
        return false;
    if (wholeObject)
        return applyToObjects(ctx, {face.object}, material);
    applyToFaces(ctx, {face}, material, QObject::tr("Apply Material"));
    return true;
}

MaterialTool::MaterialTool(EditorContext *ctx) : Tool(ctx)
{
    connect(ctx, &EditorContext::currentMaterialChanged, this, [this]
            {
        if (m_materialLabel)
            m_materialLabel->setText(m_ctx->currentMaterial().isEmpty() ? tr("(none)") : m_ctx->currentMaterial()); });
    connect(ctx->doc(), &MapDocument::objectsChanged, this, [this]
            { pruneSelection(); refreshMappingFields(); });
    connect(ctx->doc(), &MapDocument::selectionChanged, this, [this]
            { refreshMappingFields(); });
}

QString MaterialTool::statusHint() const
{
    return tr("Click: apply material   Shift+Click: whole object   Alt+Click: pick material   Ctrl+Click: select face");
}

void MaterialTool::deactivate()
{
    m_selectedFaces.clear();
    for (Viewport *v : m_ctx->viewports())
        v->update();
}

void MaterialTool::pickMaterial(const FaceRef &ref)
{
    const MapObject *o = m_ctx->doc()->find(ref.object);
    if (!o || ref.brush < 0 || ref.brush >= o->brushes.size() || ref.face < 0 || ref.face >= o->brushes[ref.brush].faces.size())
        return;
    const QString material = o->brushes[ref.brush].faces[ref.face].material;
    m_ctx->setCurrentMaterial(material);
    m_ctx->log(material.isEmpty() ? tr("Picked: face has no material") : tr("Picked material %1").arg(material));
}

void MaterialTool::mousePress(Viewport *vp, QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const Qt::KeyboardModifiers mods = e->modifiers();

    if (vp->isOrtho())
    {
        const ObjectId id = vp->pickObject(e->position());
        if (!id)
            return;
        if (mods & Qt::AltModifier)
        {
            if (const MapObject *o = m_ctx->doc()->find(id); o && !o->brushes.isEmpty() && !o->brushes.first().faces.isEmpty())
                pickMaterial({id, 0, 0});
            return;
        }
        MaterialOps::applyToObjects(m_ctx, {id}, m_ctx->currentMaterial());
        return;
    }

    FaceRef face;
    if (!MaterialOps::pickFace(m_ctx, vp, e->position(), face))
    {
        if (mods & Qt::ControlModifier)
            return;
        m_selectedFaces.clear();
        refreshMappingFields();
        vp->update();
        return;
    }

    if (mods & Qt::AltModifier)
        pickMaterial(face);
    else if (mods & Qt::ControlModifier)
    {
        const int at = m_selectedFaces.indexOf(face);
        if (at >= 0)
            m_selectedFaces.removeAt(at);
        else
            m_selectedFaces.push_back(face);
        refreshMappingFields();
    }
    else if (mods & Qt::ShiftModifier)
        MaterialOps::applyToObjects(m_ctx, {face.object}, m_ctx->currentMaterial());
    else
        MaterialOps::applyToFaces(m_ctx, {face}, m_ctx->currentMaterial(), tr("Apply Material"));

    for (Viewport *v : m_ctx->viewports())
        v->update();
}

void MaterialTool::keyPress(Viewport *vp, QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && !m_selectedFaces.isEmpty())
    {
        m_selectedFaces.clear();
        refreshMappingFields();
        vp->update();
    }
}

void MaterialTool::draw(Viewport *vp, Batch &b)
{
    if (vp->isOrtho())
        return;
    for (const FaceRef &ref : m_selectedFaces)
    {
        const MapObject *o = m_ctx->doc()->find(ref.object);
        if (!o)
            continue;
        const QVector<Brush> brushes = o->worldBrushes();
        if (ref.brush < 0 || ref.brush >= brushes.size() || ref.face < 0 || ref.face >= brushes[ref.brush].faces.size())
            continue;
        const BrushFace &f = brushes[ref.brush].faces[ref.face];
        for (int i = 1; i + 1 < f.points.size(); ++i)
            b.tri(f.points[0], f.points[i], f.points[i + 1], kFaceHighlight, true, 1.f);
        for (int i = 0, n = f.points.size(); i < n; ++i)
            b.line(f.points[i], f.points[(i + 1) % n], kFaceOutline, true);
    }
}

QVector<MaterialTool::FaceRef> MaterialTool::mappingTargets() const
{
    if (!m_selectedFaces.isEmpty())
        return m_selectedFaces;
    QVector<FaceRef> faces;
    for (ObjectId id : m_ctx->doc()->selectionList())
    {
        const MapObject *o = m_ctx->doc()->find(id);
        if (!o || !o->solid)
            continue;
        for (int bi = 0; bi < o->brushes.size(); ++bi)
            for (int fi = 0; fi < o->brushes[bi].faces.size(); ++fi)
                faces.push_back({id, bi, fi});
    }
    return faces;
}

void MaterialTool::applyMapping()
{
    if (m_updatingFields || !m_scaleU)
        return;
    const QVector<FaceRef> targets = mappingTargets();
    if (targets.isEmpty())
        return;

    TextureMapping mapping;
    mapping.scale = {float(m_scaleU->value()), float(m_scaleV->value())};
    mapping.offset = {float(m_offsetU->value()), float(m_offsetV->value())};
    mapping.rotation = float(m_rotation->value());

    QMap<ObjectId, MapObject> changed;
    for (const FaceRef &ref : targets)
    {
        if (!changed.contains(ref.object))
        {
            const MapObject *o = m_ctx->doc()->find(ref.object);
            if (!o)
                continue;
            changed.insert(ref.object, *o);
        }
        QVector<Brush> &brushes = editableBrushes(changed[ref.object]);
        if (ref.brush >= 0 && ref.brush < brushes.size() && ref.face >= 0 && ref.face < brushes[ref.brush].faces.size())
            brushes[ref.brush].faces[ref.face].mapping = mapping;
    }
    if (!changed.isEmpty())
        m_ctx->doc()->modify(changed.values().toVector(), tr("Edit Texture Mapping"), "material-mapping");
}

void MaterialTool::refreshMappingFields()
{
    if (!m_scaleU)
        return;
    const QVector<FaceRef> targets = mappingTargets();
    TextureMapping mapping;
    if (!targets.isEmpty())
    {
        const FaceRef &ref = targets.first();
        if (const MapObject *o = m_ctx->doc()->find(ref.object);
            o && ref.brush < o->brushes.size() && ref.face < o->brushes[ref.brush].faces.size())
            mapping = o->brushes[ref.brush].faces[ref.face].mapping;
    }
    m_updatingFields = true;
    m_scaleU->setValue(mapping.scale.x());
    m_scaleV->setValue(mapping.scale.y());
    m_offsetU->setValue(mapping.offset.x());
    m_offsetV->setValue(mapping.offset.y());
    m_rotation->setValue(mapping.rotation);
    for (QDoubleSpinBox *box : {m_scaleU.data(), m_scaleV.data(), m_offsetU.data(), m_offsetV.data(), m_rotation.data()})
        box->setEnabled(!targets.isEmpty());
    m_updatingFields = false;
}

void MaterialTool::pruneSelection()
{
    m_selectedFaces.erase(std::remove_if(m_selectedFaces.begin(), m_selectedFaces.end(), [this](const FaceRef &ref)
                                         {
        const MapObject *o = m_ctx->doc()->find(ref.object);
        return !o || ref.brush >= o->brushes.size() || ref.face >= o->brushes[ref.brush].faces.size(); }),
                          m_selectedFaces.end());
}

QWidget *MaterialTool::createOptionsPanel(QWidget *parent)
{
    auto *panel = new QWidget(parent);
    auto *layout = new QVBoxLayout(panel);

    auto *hint = new QLabel(tr("<small>Materials live in the Asset Browser: click one to make it current, drag it "
                               "onto a face (Shift: whole object), or double-click it to apply it to the selection.</small>"), panel);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *current = new QFormLayout;
    m_materialLabel = new QLabel(m_ctx->currentMaterial().isEmpty() ? tr("(none)") : m_ctx->currentMaterial(), panel);
    m_materialLabel->setWordWrap(true);
    current->addRow(tr("Material:"), m_materialLabel);
    layout->addLayout(current);

    auto *applyRow = new QHBoxLayout;
    auto *applySelection = new QPushButton(tr("Apply to Selection"), panel);
    applySelection->setToolTip(tr("Apply the current material to the selected faces, or to every face of the selected objects."));
    connect(applySelection, &QPushButton::clicked, this, [this]
            { MaterialOps::applyToFaces(m_ctx, mappingTargets(), m_ctx->currentMaterial(), tr("Apply Material")); });
    applyRow->addWidget(applySelection);
    auto *clear = new QPushButton(tr("No Material"), panel);
    clear->setToolTip(tr("Clear the current material (faces then use the engine default material)."));
    connect(clear, &QPushButton::clicked, this, [this]
            { m_ctx->setCurrentMaterial(QString()); });
    applyRow->addWidget(clear);
    layout->addLayout(applyRow);

    auto *mappingTitle = new QLabel(tr("<b>Texture mapping</b><br><small>Selected faces (Ctrl+Click), or all faces of the selected objects. Scale = world units per texel.</small>"), panel);
    mappingTitle->setWordWrap(true);
    layout->addWidget(mappingTitle);

    auto spin = [panel](double min, double max, double step, int decimals)
    {
        auto *box = new QDoubleSpinBox(panel);
        box->setRange(min, max);
        box->setSingleStep(step);
        box->setDecimals(decimals);
        return box;
    };
    m_scaleU = spin(-64.0, 64.0, 0.25, 3);
    m_scaleV = spin(-64.0, 64.0, 0.25, 3);
    m_offsetU = spin(-8192.0, 8192.0, 1.0, 1);
    m_offsetV = spin(-8192.0, 8192.0, 1.0, 1);
    m_rotation = spin(-360.0, 360.0, 15.0, 1);

    auto *form = new QFormLayout;
    auto pair = [panel](QWidget *a, QWidget *b)
    {
        auto *row = new QWidget(panel);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(a);
        h->addWidget(b);
        return row;
    };
    form->addRow(tr("Scale U/V:"), pair(m_scaleU, m_scaleV));
    form->addRow(tr("Offset U/V:"), pair(m_offsetU, m_offsetV));
    form->addRow(tr("Rotation:"), m_rotation);
    layout->addLayout(form);

    for (QDoubleSpinBox *box : {m_scaleU.data(), m_scaleV.data(), m_offsetU.data(), m_offsetV.data(), m_rotation.data()})
        connect(box, &QDoubleSpinBox::valueChanged, this, [this]
                { applyMapping(); });

    auto *reset = new QPushButton(tr("Reset Mapping"), panel);
    connect(reset, &QPushButton::clicked, this, [this]
            {
        m_updatingFields = true;
        m_scaleU->setValue(1.0); m_scaleV->setValue(1.0);
        m_offsetU->setValue(0.0); m_offsetV->setValue(0.0);
        m_rotation->setValue(0.0);
        m_updatingFields = false;
        applyMapping(); });
    layout->addWidget(reset);
    layout->addStretch(1);

    refreshMappingFields();
    return panel;
}
