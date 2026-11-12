#include "core/MapOperations.h"
#include "core/EditorContext.h"
#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QQuaternion>
#include <cmath>

namespace
{
    const char *kClipboardMime = "application/x-forge-objects";
    constexpr float kRadToDeg = 180.f / 3.14159265358979f;

    QVector<MapObject> selectedObjects(EditorContext *ctx, bool solidsOnly = false)
    {
        QVector<MapObject> out;
        for (ObjectId id : ctx->doc()->selectionList())
            if (const MapObject *o = ctx->doc()->find(id))
                if (!solidsOnly || o->solid)
                    out.push_back(*o);
        return out;
    }

    // RAII undo macro that only opens when there is something to record.
    class Macro
    {
    public:
        Macro(MapDocument *doc, const QString &text) : m_doc(doc) { m_doc->undoStack()->beginMacro(text); }
        ~Macro() { m_doc->undoStack()->endMacro(); }

    private:
        MapDocument *m_doc;
    };
}

namespace MapOps
{

    QVector3D rotateAngles(const QVector3D &angles, const QMatrix4x4 &rotation)
    {
        const QQuaternion orientation =
            QQuaternion::fromAxisAndAngle(0.f, 0.f, 1.f, angles.y()) *
            QQuaternion::fromAxisAndAngle(0.f, 1.f, 0.f, angles.x()) *
            QQuaternion::fromAxisAndAngle(1.f, 0.f, 0.f, angles.z());
        const QQuaternion rotated = QQuaternion::fromRotationMatrix(rotation.normalMatrix()) * orientation;
        const QMatrix3x3 m = rotated.toRotationMatrix();
        const float pitch = std::asin(std::clamp(-m(2, 0), -1.f, 1.f));
        float yaw, roll;
        if (std::abs(std::cos(pitch)) > 1e-5f)
        {
            roll = std::atan2(m(2, 1), m(2, 2));
            yaw = std::atan2(m(1, 0), m(0, 0));
        }
        else
        {
            roll = 0.f;
            yaw = std::atan2(-m(0, 1), m(1, 1));
        }
        auto clean = [](float deg)
        {
            const float r = std::round(deg * 1000.f) / 1000.f;
            return r == 0.f ? 0.f : r; // no "-0"
        };
        return {clean(pitch * kRadToDeg), clean(yaw * kRadToDeg), clean(roll * kRadToDeg)};
    }

    QList<ObjectId> replaceSolids(EditorContext *ctx, const QMap<ObjectId, QVector<Brush>> &pieces, const QString &text)
    {
        MapDocument *doc = ctx->doc();
        QVector<MapObject> modified, added;
        QSet<ObjectId> removed;
        QList<ObjectId> result;
        for (auto it = pieces.cbegin(); it != pieces.cend(); ++it)
        {
            const MapObject *src = doc->find(it.key());
            if (!src || !src->solid)
                continue;
            QVector<Brush> valid;
            for (const Brush &b : it.value())
                if (b.isValid())
                    valid.push_back(b);
            if (valid.isEmpty())
            {
                removed.insert(src->id);
                continue;
            }
            MapObject first = *src;
            if (!src->classname.isEmpty())
            {
                first.setWorldBrushes(valid);
                modified.push_back(first);
                result << first.id;
                continue;
            }
            first.setWorldBrushes({valid.first()});
            modified.push_back(first);
            result << first.id;
            for (int i = 1; i < valid.size(); ++i)
            {
                MapObject extra = *src;
                extra.id = 0;
                extra.name.clear();
                extra.setWorldBrushes({valid[i]});
                added.push_back(extra);
            }
        }
        if (modified.isEmpty() && added.isEmpty() && removed.isEmpty())
            return result;
        {
            Macro macro(doc, text);
            doc->modify(modified, text);
            if (!removed.isEmpty())
                doc->deleteObjects(removed);
            if (!added.isEmpty())
                result += doc->addObjects(added, text);
        }
        return result;
    }

    void clip(EditorContext *ctx, const Plane &plane, ClipKeep keep)
    {
        QMap<ObjectId, QVector<Brush>> pieces;
        int splitCount = 0;
        for (const MapObject &o : selectedObjects(ctx, true))
        {
            QVector<Brush> out;
            bool changed = false;
            for (const Brush &b : o.worldBrushes())
            {
                Brush front, back;
                b.split(plane, &front, &back);
                const bool split = front.isValid() && back.isValid();
                if (split)
                    ++splitCount;
                if (front.isValid() && (keep != ClipKeep::Back || !split))
                    out.push_back(front);
                if (back.isValid() && (keep != ClipKeep::Front || !split))
                    out.push_back(back);
                changed |= split;
            }
            if (changed)
                pieces[o.id] = out;
        }
        if (pieces.isEmpty())
        {
            ctx->log(QObject::tr("Clip: the clipping plane does not cross any selected solid."), LogLevel::Warning);
            return;
        }
        const QList<ObjectId> ids = replaceSolids(ctx, pieces, QObject::tr("Clip"));
        QSet<ObjectId> sel = ctx->doc()->selection();
        for (ObjectId id : ids)
            sel.insert(id);
        ctx->doc()->setSelection(sel);
        ctx->log(QObject::tr("Clipped %1 brush(es).").arg(splitCount));
    }

    void carve(EditorContext *ctx)
    {
        MapDocument *doc = ctx->doc();
        const QVector<MapObject> cutters = selectedObjects(ctx, true);
        if (cutters.isEmpty())
        {
            ctx->log(QObject::tr("Carve: select the solid(s) to carve with."), LogLevel::Warning);
            return;
        }
        QVector<Brush> cutterBrushes;
        AABB cutBounds;
        for (const MapObject &c : cutters)
            for (const Brush &b : c.worldBrushes())
            {
                cutterBrushes.push_back(b);
                cutBounds.expand(b.bounds());
            }

        QMap<ObjectId, QVector<Brush>> pieces;
        for (const auto &kvp : doc->objects())
        {
            const MapObject &o = kvp.second;
            if (!o.solid || doc->selection().contains(o.id) || !doc->isVisible(o))
                continue;
            const AABB ob = o.worldBounds();
            bool overlaps = true;
            for (int i = 0; i < 3; ++i)
                overlaps &= ob.mn[i] < cutBounds.mx[i] && cutBounds.mn[i] < ob.mx[i];
            if (!overlaps)
                continue;
            QVector<Brush> current = o.worldBrushes();
            bool any = false;
            for (const Brush &cutter : cutterBrushes)
            {
                QVector<Brush> next;
                for (const Brush &b : current)
                {
                    bool hit = false;
                    next += b.subtract(cutter, &hit);
                    any |= hit;
                }
                current = next;
            }
            if (any)
                pieces[o.id] = current;
        }
        if (pieces.isEmpty())
        {
            ctx->log(QObject::tr("Carve: nothing intersects the selection."), LogLevel::Warning);
            return;
        }
        const QSet<ObjectId> keep = doc->selection();
        replaceSolids(ctx, pieces, QObject::tr("Carve"));
        doc->setSelection(keep);
        ctx->log(QObject::tr("Carved %1 object(s).").arg(pieces.size()));
    }

    void hollow(EditorContext *ctx, float wall)
    {
        if (std::abs(wall) < 1.f)
            return;
        QMap<ObjectId, QVector<Brush>> pieces;
        int skipped = 0;
        for (const MapObject &o : selectedObjects(ctx, true))
        {
            QVector<Brush> out;
            bool ok = true;
            for (const Brush &b : o.worldBrushes())
            {
                // Positive thickness hollows inward; negative grows walls outward (like Hammer).
                QVector<Plane> inner = b.planes();
                Brush shell = b;
                if (wall > 0.f)
                {
                    for (Plane &p : inner)
                        p.dist -= wall;
                }
                else
                {
                    QVector<Plane> outer = b.planes();
                    for (Plane &p : outer)
                        p.dist -= wall;
                    const AABB bb = b.bounds();
                    const QVector3D pad(-wall * 4.f + 4.f, -wall * 4.f + 4.f, -wall * 4.f + 4.f);
                    shell = Brush::fromPlanes(outer, b.faces.first().material, AABB(bb.mn - pad, bb.mx + pad));
                }
                const AABB bb = b.bounds();
                const Brush cavity = wall > 0.f ? Brush::fromPlanes(inner, b.faces.first().material,
                                                                    AABB(bb.mn - QVector3D(4, 4, 4), bb.mx + QVector3D(4, 4, 4)))
                                                : b;
                if (!cavity.isValid() || !shell.isValid())
                {
                    ok = false;
                    break;
                }
                out += shell.subtract(cavity);
            }
            if (ok && !out.isEmpty())
                pieces[o.id] = out;
            else
                ++skipped;
        }
        if (skipped)
            ctx->log(QObject::tr("Hollow: %1 solid(s) are too small for a %2 unit wall.").arg(skipped).arg(wall), LogLevel::Warning);
        if (pieces.isEmpty())
            return;
        const QList<ObjectId> ids = replaceSolids(ctx, pieces, QObject::tr("Hollow"));
        ctx->doc()->setSelection(QSet<ObjectId>(ids.begin(), ids.end()));
    }

    void tieToEntity(EditorContext *ctx, const QString &classname)
    {
        const QVector<MapObject> solids = selectedObjects(ctx, true);
        if (solids.isEmpty())
        {
            ctx->log(QObject::tr("Tie to Entity: select one or more solids first."), LogLevel::Warning);
            return;
        }
        const ClassDef *def = ctx->registry()->resolved(classname);
        if (!def || def->kind != ClassKind::Solid)
        {
            ctx->log(QObject::tr("Tie to Entity: '%1' is not a solid entity class.").arg(classname), LogLevel::Warning);
            return;
        }
        MapObject ent = solids.first();
        QVector<Brush> all;
        QSet<ObjectId> remove;
        for (const MapObject &o : solids)
        {
            all += o.worldBrushes();
            remove.insert(o.id);
        }
        ent.id = 0;
        ent.kv.clear();
        ent.outputs.clear();
        applyClassDefaults(ent, *def);
        ent.name = ctx->doc()->uniqueName(def->name);
        ent.setWorldBrushes(all);
        MapDocument *doc = ctx->doc();
        Macro macro(doc, QObject::tr("Tie to Entity"));
        doc->deleteObjects(remove);
        doc->addObject(ent, QObject::tr("Tie to Entity"));
    }

    void moveToWorld(EditorContext *ctx)
    {
        QVector<MapObject> added;
        QSet<ObjectId> remove;
        for (const MapObject &o : selectedObjects(ctx, true))
        {
            if (o.classname.isEmpty())
                continue;
            remove.insert(o.id);
            for (const Brush &b : o.worldBrushes())
            {
                MapObject w;
                w.solid = true;
                w.groupPath = o.groupPath;
                w.visgroup = o.visgroup;
                w.setWorldBrushes({b});
                added.push_back(w);
            }
        }
        if (remove.isEmpty())
        {
            ctx->log(QObject::tr("Move to World: no solid entities selected."), LogLevel::Warning);
            return;
        }
        MapDocument *doc = ctx->doc();
        Macro macro(doc, QObject::tr("Move to World"));
        doc->deleteObjects(remove);
        doc->addObjects(added, QObject::tr("Move to World"));
    }

    void transformSelection(EditorContext *ctx, const QMatrix4x4 &m, const QString &text, bool rotatePointAngles)
    {
        QVector<MapObject> after = selectedObjects(ctx);
        for (MapObject &o : after)
        {
            o.transformWorld(m);
            if (!o.solid && rotatePointAngles)
                o.angles = rotateAngles(o.angles, m);
        }
        ctx->doc()->modify(after, text);
    }

    void flip(EditorContext *ctx, int axis)
    {
        const AABB b = ctx->doc()->selectionBounds();
        if (!b.valid() || axis < 0 || axis > 2)
            return;
        const QVector3D c = ctx->snap(b.center());
        QVector3D s(1, 1, 1);
        s[axis] = -1.f;
        QMatrix4x4 m;
        m.translate(c);
        m.scale(s);
        m.translate(-c);
        QVector<MapObject> after = selectedObjects(ctx);
        for (MapObject &o : after)
        {
            o.transformWorld(m);
            if (!o.solid)
            {
                if (axis == 0)
                    o.angles.setY(std::remainder(180.f - o.angles.y(), 360.f));
                else if (axis == 1)
                    o.angles.setY(std::remainder(-o.angles.y(), 360.f));
                else
                    o.angles.setX(-o.angles.x());
            }
        }
        ctx->doc()->modify(after, QObject::tr("Flip Objects"));
    }

    void snapToGrid(EditorContext *ctx)
    {
        const float g = float(std::max(1, ctx->gridSize()));
        QVector<MapObject> after = selectedObjects(ctx);
        for (MapObject &o : after)
        {
            if (!o.solid)
            {
                o.origin = snapVec(o.origin, g);
                continue;
            }
            // Snap every brush vertex, rebuilding each brush as the hull of the snapped points.
            QVector<Brush> world = o.worldBrushes(), snapped;
            for (const Brush &b : world)
            {
                QVector<QVector3D> pts = b.vertices();
                for (QVector3D &p : pts)
                    p = snapVec(p, g);
                const Brush h = Brush::convexHull(pts, b.faces.first().material);
                snapped.push_back(h.isValid() ? h : b);
            }
            o.setWorldBrushes(snapped);
        }
        ctx->doc()->modify(after, QObject::tr("Snap to Grid"));
    }

    static void setVisibility(EditorContext *ctx, const QSet<ObjectId> &ids, bool visible, const QString &text)
    {
        QVector<MapObject> after;
        for (ObjectId id : ids)
            if (const MapObject *o = ctx->doc()->find(id); o && o->visible != visible)
            {
                MapObject c = *o;
                c.visible = visible;
                after.push_back(c);
            }
        if (after.isEmpty())
            return;
        ctx->doc()->modify(after, text);
        if (!visible)
        {
            QSet<ObjectId> sel = ctx->doc()->selection();
            for (ObjectId id : ids)
                sel.remove(id);
            ctx->doc()->setSelection(sel);
        }
    }

    void hideSelected(EditorContext *ctx)
    {
        setVisibility(ctx, ctx->doc()->selection(), false, QObject::tr("Hide Selected"));
    }

    void hideUnselected(EditorContext *ctx)
    {
        QSet<ObjectId> ids;
        for (const auto &kvp : ctx->doc()->objects())
            if (!ctx->doc()->selection().contains(kvp.first))
                ids.insert(kvp.first);
        setVisibility(ctx, ids, false, QObject::tr("Hide Unselected"));
    }

    void unhideAll(EditorContext *ctx)
    {
        QSet<ObjectId> ids;
        for (const auto &kvp : ctx->doc()->objects())
            ids.insert(kvp.first);
        setVisibility(ctx, ids, true, QObject::tr("Unhide All"));
    }

    void selectSameClass(EditorContext *ctx)
    {
        QSet<QString> classes;
        QSet<bool> kinds;
        for (const MapObject &o : selectedObjects(ctx))
        {
            classes.insert(o.classname);
            kinds.insert(o.solid);
        }
        if (classes.isEmpty())
            return;
        QSet<ObjectId> sel;
        for (const auto &kvp : ctx->doc()->objects())
            if (classes.contains(kvp.second.classname) && kinds.contains(kvp.second.solid) && ctx->doc()->isVisible(kvp.second))
                sel.insert(kvp.first);
        ctx->doc()->setSelection(sel);
    }

    void copy(EditorContext *ctx)
    {
        if (ctx->doc()->selection().isEmpty())
            return;
        const QByteArray data = ctx->doc()->serialize(ctx->doc()->selection());
        auto *mime = new QMimeData;
        mime->setData(kClipboardMime, data);
        mime->setText(QString::fromUtf8(data));
        QApplication::clipboard()->setMimeData(mime);
    }

    void cut(EditorContext *ctx)
    {
        if (ctx->doc()->selection().isEmpty())
            return;
        copy(ctx);
        ctx->doc()->deleteObjects(ctx->doc()->selection());
    }

    bool canPaste()
    {
        const QMimeData *m = QApplication::clipboard()->mimeData();
        return m && (m->hasFormat(kClipboardMime) || m->text().contains("\"forge-map\""));
    }

    bool paste(EditorContext *ctx)
    {
        const QMimeData *m = QApplication::clipboard()->mimeData();
        if (!m)
            return false;
        const QByteArray data = m->hasFormat(kClipboardMime) ? m->data(kClipboardMime) : m->text().toUtf8();
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.object()["format"].toString() != "forge-map")
            return false;
        QVector<MapObject> objs;
        QSet<QString> used;
        for (const auto &kvp : ctx->doc()->objects())
            used.insert(kvp.second.name);
        for (const QJsonValue &v : doc.object()["objects"].toArray())
        {
            MapObject o = MapObject::fromJson(v.toObject());
            o.id = 0;
            if (!o.name.isEmpty() && used.contains(o.name))
            {
                QString base = o.name;
                const int us = base.lastIndexOf('_');
                bool numeric = false;
                if (us > 0)
                    base.mid(us + 1).toInt(&numeric);
                base = numeric ? base.left(us) : base;
                for (int i = 1;; ++i)
                {
                    const QString candidate = QString("%1_%2").arg(base).arg(i);
                    if (!used.contains(candidate))
                    {
                        o.name = candidate;
                        break;
                    }
                }
            }
            used.insert(o.name);
            objs.push_back(o);
        }
        if (objs.isEmpty())
            return false;
        ctx->doc()->addObjects(objs, QObject::tr("Paste"));
        return true;
    }

}
