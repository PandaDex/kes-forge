#include "core/MapObject.h"
#include <QJsonArray>

namespace
{
    QJsonArray toArr(const QVector3D &v) { return QJsonArray{double(v.x()), double(v.y()), double(v.z())}; }
    QVector3D fromArr(const QJsonValue &v, const QVector3D &def)
    {
        const QJsonArray a = v.toArray();
        if (a.size() < 3)
            return def;
        const QVector3D r(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
        if (!std::isfinite(r.x()) || !std::isfinite(r.y()) || !std::isfinite(r.z()))
            return def;
        return r;
    }

    // True when the object is exactly its legacy axis-aligned box (no need to store brushes).
    bool isPlainBox(const MapObject &o)
    {
        if (o.brushes.size() != 1)
            return false;
        const Brush &b = o.brushes.first();
        if (b.faces.size() != 6)
            return false;
        for (const BrushFace &f : b.faces)
        {
            if (!f.material.isEmpty() || !f.mapping.isDefault() || f.points.size() != 4)
                return false;
            const QVector3D n = f.normal();
            if (std::abs(std::abs(n.x()) + std::abs(n.y()) + std::abs(n.z()) - 1.f) > 1e-4f)
                return false;
        }
        return Brush::box(AABB(o.mins, o.maxs)) == b;
    }
}

bool Connection::operator==(const Connection &o) const
{
    return output == o.output && target == o.target && legacyTarget == o.legacyTarget && input == o.input &&
           param == o.param && delay == o.delay && once == o.once;
}

bool MapObject::operator==(const MapObject &o) const
{
    return id == o.id && guid == o.guid && name == o.name && classname == o.classname && solid == o.solid &&
           visible == o.visible && groupPath == o.groupPath && visgroup == o.visgroup &&
           origin == o.origin && angles == o.angles && mins == o.mins &&
           maxs == o.maxs && brushes == o.brushes && kv == o.kv && outputs == o.outputs;
}

QString MapObject::displayName() const
{
    if (!name.isEmpty())
        return name;
    return QString("%1 #%2").arg(classname.isEmpty() ? QStringLiteral("Solid") : classname).arg(id);
}

void MapObject::ensureBrushes()
{
    if (solid && brushes.isEmpty())
        brushes.push_back(Brush::box(AABB(mins, maxs)));
}

void MapObject::updateBounds()
{
    if (!solid || brushes.isEmpty())
        return;
    AABB b;
    for (const Brush &br : brushes)
        b.expand(br.bounds());
    if (!b.valid())
        return;
    const QVector3D shift = b.center();
    if (!shift.isNull())
    {
        for (Brush &br : brushes)
            br.translate(-shift);
        origin += shift;
    }
    mins = b.mn - shift;
    maxs = b.mx - shift;
}

QVector<Brush> MapObject::worldBrushes() const
{
    QVector<Brush> out = brushes;
    if (out.isEmpty() && solid)
        out.push_back(Brush::box(AABB(mins, maxs)));
    for (Brush &b : out)
        b.translate(origin);
    return out;
}

void MapObject::setWorldBrushes(const QVector<Brush> &world)
{
    brushes = world;
    for (Brush &b : brushes)
        b.translate(-origin);
    updateBounds();
}

void MapObject::transformWorld(const QMatrix4x4 &m)
{
    if (!solid)
    {
        origin = m.map(origin);
        return;
    }
    QVector<Brush> w = worldBrushes();
    for (Brush &b : w)
        b.transform(m);
    setWorldBrushes(w);
}

void MapObject::setWorldBounds(const AABB &b)
{
    if (solid)
    {
        ensureBrushes();
        const AABB old = worldBounds();
        QMatrix4x4 m;
        m.translate(b.mn);
        QVector3D s(1, 1, 1);
        for (int i = 0; i < 3; ++i)
            if (old.size()[i] > 1e-4f)
                s[i] = b.size()[i] / old.size()[i];
        m.scale(s);
        m.translate(-old.mn);
        transformWorld(m);
        return;
    }
    origin = b.center();
    const QVector3D h = b.size() * 0.5f;
    mins = -h;
    maxs = h;
}

bool MapObject::intersectRay(const Ray &r, float *t) const
{
    if (!worldBounds().intersect(r))
        return false;
    if (!solid || brushes.isEmpty())
        return worldBounds().intersect(r, t);
    const Ray local{r.origin - origin, r.dir};
    float best = 1e30f;
    bool hit = false;
    for (const Brush &b : brushes)
    {
        float bt;
        if (b.intersectRay(local, &bt) && bt < best)
        {
            best = bt;
            hit = true;
        }
    }
    if (hit && t)
        *t = best;
    return hit;
}

QJsonObject MapObject::toJson() const
{
    QJsonObject j;
    j["id"] = id;
    if (!guid.isNull())
        j["guid"] = guid.toString(QUuid::WithoutBraces);
    j["name"] = name;
    j["class"] = classname;
    j["solid"] = solid;
    j["visible"] = visible;
    j["group"] = groupPath;
    j["visgroup"] = visgroup;
    j["origin"] = toArr(origin);
    j["angles"] = toArr(angles);
    j["mins"] = toArr(mins);
    j["maxs"] = toArr(maxs);
    if (solid && !brushes.isEmpty() && !isPlainBox(*this))
    {
        QJsonArray bs;
        for (const Brush &b : brushes)
            bs.append(b.toJson());
        j["brushes"] = bs;
    }
    QJsonObject k;
    for (auto it = kv.begin(); it != kv.end(); ++it)
        k[it.key()] = it.value();
    j["kv"] = k;
    QJsonArray outs;
    for (const Connection &c : outputs)
    {
        const QString target = c.target.isNull() ? c.legacyTarget : c.target.toString(QUuid::WithoutBraces);
        outs.append(QJsonObject{{"output", c.output}, {"target", target}, {"input", c.input}, {"param", c.param}, {"delay", double(c.delay)}, {"once", c.once}});
    }
    j["outputs"] = outs;
    return j;
}

MapObject MapObject::fromJson(const QJsonObject &j)
{
    MapObject o;
    o.id = j["id"].toInt();
    o.guid = QUuid::fromString(j["guid"].toString());
    o.name = j["name"].toString();
    o.classname = j["class"].toString();
    o.solid = j["solid"].toBool();
    o.visible = j["visible"].toBool(true);
    o.groupPath = j["group"].toString();
    o.visgroup = j["visgroup"].toString();
    o.origin = fromArr(j["origin"], {});
    o.angles = fromArr(j["angles"], {});
    o.mins = fromArr(j["mins"], {-8, -8, -8});
    o.maxs = fromArr(j["maxs"], {8, 8, 8});
    for (int i = 0; i < 3; ++i)
        if (o.mins[i] > o.maxs[i])
            std::swap(o.mins[i], o.maxs[i]);
    if (o.solid)
    {
        for (const QJsonValue &v : j["brushes"].toArray())
        {
            const Brush b = Brush::fromJson(v.toObject());
            if (b.isValid())
                o.brushes.push_back(b);
        }
        o.ensureBrushes();
        o.updateBounds();
    }
    const QJsonObject k = j["kv"].toObject();
    for (auto it = k.begin(); it != k.end(); ++it)
        o.kv[it.key()] = it.value().toString();
    for (const QJsonValue &v : j["outputs"].toArray())
    {
        const QJsonObject c = v.toObject();
        Connection con;
        con.output = c["output"].toString();
        const QString target = c["target"].toString();
        con.target = QUuid::fromString(target);
        if (con.target.isNull())
            con.legacyTarget = target;
        con.input = c["input"].toString();
        con.param = c["param"].toString();
        con.delay = float(c["delay"].toDouble());
        con.once = c["once"].toBool();
        o.outputs.push_back(con);
    }
    return o;
}
