#include "core/MapDocument.h"
#include <QHash>
#include "entities/EntityDefs.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QSaveFile>
#include <algorithm>

namespace
{

    class AddCmd : public QUndoCommand
    {
    public:
        AddCmd(MapDocument *d, QVector<MapObject> o, const QString &text) : m_d(d), m_objs(std::move(o)) { setText(text); }
        void redo() override
        {
            for (const auto &o : m_objs)
                m_d->rawInsert(o);
            m_d->notifyChanged();
        }
        void undo() override
        {
            for (const auto &o : m_objs)
                m_d->rawRemove(o.id);
            m_d->notifyChanged();
        }

    private:
        MapDocument *m_d;
        QVector<MapObject> m_objs;
    };

    class DeleteCmd : public QUndoCommand
    {
    public:
        DeleteCmd(MapDocument *d, QVector<MapObject> o) : m_d(d), m_objs(std::move(o)) { setText(QObject::tr("Delete Objects")); }
        void redo() override
        {
            for (const auto &o : m_objs)
                m_d->rawRemove(o.id);
            m_d->notifyChanged();
        }
        void undo() override
        {
            for (const auto &o : m_objs)
                m_d->rawInsert(o);
            m_d->notifyChanged();
        }

    private:
        MapDocument *m_d;
        QVector<MapObject> m_objs;
    };

    class ModifyCmd : public QUndoCommand
    {
    public:
        ModifyCmd(MapDocument *d, QVector<MapObject> before, QVector<MapObject> after, const QString &text, const QString &key)
            : m_d(d), m_before(std::move(before)), m_after(std::move(after)), m_key(key) { setText(text); }
        void redo() override
        {
            for (const auto &o : m_after)
                m_d->rawReplace(o);
            m_d->notifyChanged();
        }
        void undo() override
        {
            for (const auto &o : m_before)
                m_d->rawReplace(o);
            m_d->notifyChanged();
        }
        int id() const override { return m_key.isEmpty() ? -1 : 0x4D4F44; }
        bool mergeWith(const QUndoCommand *other) override
        {
            auto *o = dynamic_cast<const ModifyCmd *>(other);
            if (!o || o->m_key != m_key || o->m_after.size() != m_after.size())
                return false;
            for (int i = 0; i < m_after.size(); ++i)
                if (m_after[i].id != o->m_after[i].id)
                    return false;
            m_after = o->m_after;
            return true;
        }

    private:
        MapDocument *m_d;
        QVector<MapObject> m_before, m_after;
        QString m_key;
    };

    class LayerVisibilityCmd : public QUndoCommand
    {
    public:
        LayerVisibilityCmd(MapDocument *doc, bool visgroup, QString name, bool before, bool beforeExists, bool after)
            : m_doc(doc), m_visgroup(visgroup), m_name(std::move(name)), m_before(before),
              m_beforeExists(beforeExists), m_after(after)
        {
            setText(visgroup ? QObject::tr("Toggle Visgroup Visibility") : QObject::tr("Toggle Group Visibility"));
        }
        void redo() override { m_doc->rawSetLayerVisibility(m_visgroup, m_name, m_after, true); }
        void undo() override { m_doc->rawSetLayerVisibility(m_visgroup, m_name, m_before, m_beforeExists); }

    private:
        MapDocument *m_doc;
        bool m_visgroup;
        QString m_name;
        bool m_before;
        bool m_beforeExists;
        bool m_after;
    };

}

MapDocument::MapDocument(QObject *parent) : QObject(parent)
{
    connect(&m_undo, &QUndoStack::cleanChanged, this, [this](bool)
            { emit modifiedChanged(); });
}

const MapObject *MapDocument::findByGuid(const QUuid &guid) const
{
    if (guid.isNull())
        return nullptr;
    for (const auto &kvp : m_objects)
        if (kvp.second.guid == guid)
            return &kvp.second;
    return nullptr;
}

const MapObject *MapDocument::find(ObjectId id) const
{
    auto it = m_objects.find(id);
    return it == m_objects.end() ? nullptr : &it->second;
}

bool MapDocument::isVisible(const MapObject &o) const
{
    if (!o.visible)
        return false;
    QStringList parts = o.groupPath.split('/', Qt::SkipEmptyParts);
    QString path;
    for (const QString &part : parts)
    {
        path = path.isEmpty() ? part : path + '/' + part;
        if (!groupVisible(path))
            return false;
    }
    return o.visgroup.isEmpty() || visgroupVisible(o.visgroup);
}

QStringList MapDocument::visgroups() const
{
    QSet<QString> names;
    for (const auto &entry : m_objects)
        if (!entry.second.visgroup.isEmpty())
            names.insert(entry.second.visgroup);
    for (auto it = m_visgroupVisibility.cbegin(); it != m_visgroupVisibility.cend(); ++it)
        names.insert(it.key());
    QStringList result = names.values();
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool MapDocument::groupVisible(const QString &path) const
{
    return m_groupVisibility.value(path, true);
}

bool MapDocument::visgroupVisible(const QString &name) const
{
    return m_visgroupVisibility.value(name, true);
}

void MapDocument::setGroupVisible(const QString &path, bool visible)
{
    if (path.isEmpty() || groupVisible(path) == visible)
        return;
    m_undo.push(new LayerVisibilityCmd(this, false, path, groupVisible(path),
                                       m_groupVisibility.contains(path), visible));
}

void MapDocument::setVisgroupVisible(const QString &name, bool visible)
{
    if (name.isEmpty() || visgroupVisible(name) == visible)
        return;
    m_undo.push(new LayerVisibilityCmd(this, true, name, visgroupVisible(name),
                                       m_visgroupVisibility.contains(name), visible));
}

QString MapDocument::uniqueName(const QString &base) const
{
    QSet<QString> used;
    for (const auto &kvp : m_objects)
        used.insert(kvp.second.name);
    for (int i = 1;; ++i)
    {
        const QString n = QString("%1_%2").arg(base).arg(i);
        if (!used.contains(n))
            return n;
    }
}

QVector<MapObject> MapDocument::snapshot() const
{
    QVector<MapObject> v;
    for (const auto &kvp : m_objects)
        v.push_back(kvp.second);
    return v;
}

QList<ObjectId> MapDocument::selectionList() const
{
    QList<ObjectId> l = m_selection.values();
    std::sort(l.begin(), l.end());
    return l;
}

void MapDocument::setSelection(const QSet<ObjectId> &s)
{
    QSet<ObjectId> filtered;
    for (ObjectId id : s)
        if (m_objects.count(id))
            filtered.insert(id);
    if (filtered == m_selection)
        return;
    m_selection = filtered;
    emit selectionChanged();
}

void MapDocument::selectAll()
{
    QSet<ObjectId> s;
    for (const auto &kvp : m_objects)
        if (isVisible(kvp.second))
            s.insert(kvp.first);
    setSelection(s);
}

AABB MapDocument::selectionBounds() const
{
    AABB b;
    for (ObjectId id : m_selection)
        if (const MapObject *o = find(id))
            b.expand(o->worldBounds());
    return b;
}

void MapDocument::rawRemove(ObjectId id)
{
    m_objects.erase(id);
    if (m_selection.remove(id))
        m_selDirty = true;
}

void MapDocument::rawReplace(const MapObject &o)
{
    auto it = m_objects.find(o.id);
    if (it != m_objects.end())
        it->second = o;
}

void MapDocument::rawSetLayerVisibility(bool visgroup, const QString &name, bool visible, bool existed)
{
    QMap<QString, bool> &visibility = visgroup ? m_visgroupVisibility : m_groupVisibility;
    if (existed)
        visibility[name] = visible;
    else
        visibility.remove(name);
    notifyChanged();
}

void MapDocument::notifyChanged()
{
    emit objectsChanged();
    if (m_selDirty)
    {
        m_selDirty = false;
        emit selectionChanged();
    }
}

QList<ObjectId> MapDocument::addObjects(QVector<MapObject> objs, const QString &text)
{
    // New objects get a GUID. Copies (paste, duplicate, split) whose GUID is still in use get
    // a fresh one, and links between objects of the same batch follow them to the copies.
    // A cut + paste keeps the GUIDs, so links to the pasted objects stay intact.
    QSet<QUuid> used;
    for (const auto &kvp : m_objects)
        used.insert(kvp.second.guid);
    QHash<QUuid, QUuid> renamed;
    for (MapObject &o : objs)
    {
        if (o.guid.isNull() || used.contains(o.guid))
        {
            const QUuid fresh = QUuid::createUuid();
            if (!o.guid.isNull())
                renamed.insert(o.guid, fresh);
            o.guid = fresh;
        }
        used.insert(o.guid);
    }
    for (MapObject &o : objs)
        for (Connection &c : o.outputs)
            c.target = renamed.value(c.target, c.target);

    QList<ObjectId> ids;
    for (MapObject &o : objs)
    {
        while (m_objects.count(m_nextId))
            ++m_nextId;
        o.id = m_nextId++;
        if (o.solid)
        {
            o.ensureBrushes();
            o.updateBounds();
        }
        ids << o.id;
    }
    if (objs.isEmpty())
        return ids;
    m_undo.push(new AddCmd(this, objs, text.isEmpty() ? tr("Create Objects") : text));
    setSelection(QSet<ObjectId>(ids.begin(), ids.end()));
    return ids;
}

ObjectId MapDocument::addObject(const MapObject &o, const QString &text)
{
    return addObjects({o}, text.isEmpty() ? tr("Create Object") : text).value(0, 0);
}

void MapDocument::deleteObjects(const QSet<ObjectId> &ids)
{
    QVector<MapObject> v;
    for (ObjectId id : ids)
        if (const MapObject *o = find(id))
            v.push_back(*o);
    if (!v.isEmpty())
        m_undo.push(new DeleteCmd(this, v));
}

void MapDocument::duplicateSelection(const QVector3D &offset)
{
    QVector<MapObject> copies;
    for (ObjectId id : selectionList())
    {
        if (const MapObject *o = find(id))
        {
            MapObject c = *o;
            c.origin += offset;
            if (!c.name.isEmpty())
            {
                QString base = c.name;
                const int us = base.lastIndexOf('_');
                bool numeric = false;
                if (us > 0)
                    base.mid(us + 1).toInt(&numeric);
                c.name = uniqueName(numeric ? base.left(us) : base);
            }
            copies.push_back(c);
        }
    }
    addObjects(copies, tr("Duplicate Objects"));
}

void MapDocument::modify(const QVector<MapObject> &after, const QString &text, const QString &mergeKey)
{
    QVector<MapObject> b, a;
    for (const MapObject &n : after)
    {
        const MapObject *cur = find(n.id);
        if (!cur || *cur == n)
            continue;
        b.push_back(*cur);
        a.push_back(n);
    }
    if (!a.isEmpty())
        m_undo.push(new ModifyCmd(this, b, a, text, mergeKey));
}

void MapDocument::previewReplace(const QVector<MapObject> &objs)
{
    for (const MapObject &o : objs)
        rawReplace(o);
    notifyChanged();
}

void MapDocument::commitFromPreview(const QVector<MapObject> &before, const QString &text)
{
    QVector<MapObject> b, a;
    for (const MapObject &o : before)
    {
        const MapObject *cur = find(o.id);
        if (!cur || *cur == o)
            continue;
        b.push_back(o);
        a.push_back(*cur);
    }
    if (!a.isEmpty())
        m_undo.push(new ModifyCmd(this, b, a, text, QString()));
}

void MapDocument::syncEntityBounds(const EntityRegistry &reg)
{
    bool any = false;
    for (auto &kvp : m_objects)
    {
        MapObject &o = kvp.second;
        if (o.solid || o.classname.isEmpty())
            continue;
        if (const ClassDef *d = reg.resolved(o.classname))
        {
            if (d->hasSize && (o.mins != d->mins || o.maxs != d->maxs))
            {
                o.mins = d->mins;
                o.maxs = d->maxs;
                any = true;
            }
        }
    }
    if (any)
        notifyChanged();
}

void MapDocument::clear()
{
    m_objects.clear();
    m_groupVisibility.clear();
    m_visgroupVisibility.clear();
    m_selection.clear();
    m_undo.clear();
    m_undo.setClean();
    m_nextId = 1;
    m_path.clear();
    notifyChanged();
    emit selectionChanged();
    emit pathChanged();
    emit modifiedChanged();
}

QByteArray MapDocument::serialize(const QSet<ObjectId> &only) const
{
    QJsonArray objs;
    for (const auto &kvp : m_objects)
        if (only.isEmpty() || only.contains(kvp.first))
            objs.append(kvp.second.toJson());
    QJsonObject groups, visgroups;
    for (auto it = m_groupVisibility.cbegin(); it != m_groupVisibility.cend(); ++it)
        groups[it.key()] = it.value();
    for (auto it = m_visgroupVisibility.cbegin(); it != m_visgroupVisibility.cend(); ++it)
        visgroups[it.key()] = it.value();
    QJsonObject root{{"format", "forge-map"}, {"version", 1}, {"objects", objs}};
    if (only.isEmpty())
    {
        root["groupVisibility"] = groups;
        root["visgroupVisibility"] = visgroups;
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool MapDocument::saveCopy(const QString &path, QString *error) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(serialize());
    if (!f.commit())
    {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool MapDocument::save(const QString &path, QString *error)
{
    if (!saveCopy(path, error))
        return false;
    m_path = path;
    m_undo.setClean();
    emit pathChanged();
    emit modifiedChanged();
    return true;
}

bool MapDocument::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || doc.object()["format"].toString() != "forge-map")
    {
        if (error)
            *error = pe.error != QJsonParseError::NoError ? pe.errorString() : tr("Not a Forge map file");
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonArray arr = root["objects"].toArray();
    QMap<QString, bool> groupVisibility, visgroupVisibility;
    const QJsonObject groups = root["groupVisibility"].toObject();
    for (auto it = groups.begin(); it != groups.end(); ++it)
        groupVisibility[it.key()] = it.value().toBool(true);
    const QJsonObject visgroups = root["visgroupVisibility"].toObject();
    for (auto it = visgroups.begin(); it != visgroups.end(); ++it)
        visgroupVisibility[it.key()] = it.value().toBool(true);
    clear();
    m_groupVisibility = groupVisibility;
    m_visgroupVisibility = visgroupVisibility;
    ObjectId maxId = 0;
    for (const QJsonValue &v : arr)
        maxId = std::max(maxId, v.toObject()["id"].toInt());
    QSet<QUuid> guids;
    QHash<QString, QUuid> byName;
    for (const QJsonValue &v : arr)
    {
        MapObject o = MapObject::fromJson(v.toObject());
        if (o.id <= 0 || m_objects.count(o.id))
            o.id = ++maxId;
        // Maps from before MapScript have no GUIDs; they are saved with the next save.
        if (o.guid.isNull() || guids.contains(o.guid))
            o.guid = QUuid::createUuid();
        guids.insert(o.guid);
        if (!o.name.isEmpty() && !byName.contains(o.name))
            byName.insert(o.name, o.guid);
        m_objects[o.id] = o;
    }
    for (auto &kvp : m_objects)
        for (Connection &c : kvp.second.outputs)
            if (c.target.isNull() && byName.contains(c.legacyTarget))
            {
                c.target = byName.value(c.legacyTarget);
                c.legacyTarget.clear();
            }
    m_nextId = maxId + 1;
    m_path = path;
    m_undo.setClean();
    notifyChanged();
    emit pathChanged();
    emit modifiedChanged();
    return true;
}
