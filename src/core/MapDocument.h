#pragma once
#include "core/MapObject.h"
#include <QObject>
#include <QSet>
#include <QMap>
#include <QUndoStack>
#include <QVector>
#include <map>

class EntityRegistry;

class MapDocument : public QObject
{
    Q_OBJECT
public:
    explicit MapDocument(QObject *parent = nullptr);

    const std::map<ObjectId, MapObject> &objects() const { return m_objects; }
    const MapObject *find(ObjectId id) const;
    const MapObject *findByGuid(const QUuid &guid) const;
    bool isVisible(const MapObject &o) const;
    QString uniqueName(const QString &base) const;
    QStringList visgroups() const;
    bool groupVisible(const QString &path) const;
    bool visgroupVisible(const QString &name) const;
    void setGroupVisible(const QString &path, bool visible);
    void setVisgroupVisible(const QString &name, bool visible);

    QVector<MapObject> snapshot() const;

    const QSet<ObjectId> &selection() const { return m_selection; }
    QList<ObjectId> selectionList() const;
    void setSelection(const QSet<ObjectId> &s);
    void selectAll();
    void clearSelection() { setSelection({}); }
    AABB selectionBounds() const;

    QList<ObjectId> addObjects(QVector<MapObject> objs, const QString &text);
    ObjectId addObject(const MapObject &o, const QString &text = QString());
    void deleteObjects(const QSet<ObjectId> &ids);
    void duplicateSelection(const QVector3D &offset);
    // `after` holds full replacement states (ids must exist). mergeKey != empty lets consecutive
    // edits of the same objects collapse into one undo step (spin boxes, typing...).
    void modify(const QVector<MapObject> &after, const QString &text, const QString &mergeKey = QString());

    // Interactive drags: preview without undo, then commit with the pre-drag state.
    void previewReplace(const QVector<MapObject> &objs);
    void commitFromPreview(const QVector<MapObject> &before, const QString &text);

    QUndoStack *undoStack() { return &m_undo; }

    // Registry changed -> refresh point-entity bounds from class sizes (not undoable).
    void syncEntityBounds(const EntityRegistry &reg);

    void clear();
    bool save(const QString &path, QString *error = nullptr);
    bool load(const QString &path, QString *error = nullptr);
    // Writes the map without changing the document path or clean state (autosave / backups).
    bool saveCopy(const QString &path, QString *error = nullptr) const;
    QByteArray serialize(const QSet<ObjectId> &only = {}) const;
    QString filePath() const { return m_path; }
    bool isModified() const { return !m_undo.isClean(); }

    // Internal: used by undo commands. Call notifyChanged() once after a batch.
    void rawInsert(const MapObject &o) { m_objects[o.id] = o; }
    void rawRemove(ObjectId id);
    void rawReplace(const MapObject &o);
    void rawSetLayerVisibility(bool visgroup, const QString &name, bool visible, bool existed);
    void notifyChanged();

signals:
    void objectsChanged();
    void selectionChanged();
    void modifiedChanged();
    void pathChanged();

private:
    std::map<ObjectId, MapObject> m_objects;
    QSet<ObjectId> m_selection;
    QUndoStack m_undo;
    QMap<QString, bool> m_groupVisibility;
    QMap<QString, bool> m_visgroupVisibility;
    QString m_path;
    ObjectId m_nextId = 1;
    bool m_selDirty = false;
};
