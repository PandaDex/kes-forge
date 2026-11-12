#pragma once
#include "core/MapObject.h"
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <map>

enum class PropType
{
    String,
    Integer,
    Float,
    Boolean,
    Choices,
    Flags,
    Color,
    Vector,
    Angles,
    TargetSource,
    TargetDest,
    Asset
};

struct ChoiceItem
{
    QString value, label;
};
struct FlagItem
{
    int bit = 1;
    QString label;
    bool defaultOn = false;
};

struct PropertyDef
{
    QString key;
    QString typeName; // original DEC type string ("studio", "color255", ...)
    PropType type = PropType::String;
    QString label, defaultValue, help;
    bool readOnly = false;
    QList<ChoiceItem> choices;
    QList<FlagItem> flags;
};

struct IODef
{
    QString name, type, help;
};

enum class ClassKind
{
    Base,
    Point,
    Solid
};

struct ClassDef
{
    ClassKind kind = ClassKind::Point;
    QString name, description, sourceFile;
    QStringList bases;
    bool hasColor = false;
    QColor color{80, 160, 255};
    bool hasSize = false;
    QVector3D mins{-8, -8, -8}, maxs{8, 8, 8};
    QString model, sprite;
    QList<PropertyDef> props;
    QList<IODef> inputs, outputs;
};

class EntityRegistry : public QObject
{
    Q_OBJECT
public:
    explicit EntityRegistry(QObject *parent = nullptr);

    void declare(const ClassDef &def);

    void addSearchPath(const QString &dir);
    void reloadAll();
    bool loadFile(const QString &path, QStringList *errors = nullptr);

    const ClassDef *find(const QString &name) const;
    // Fully resolved definition: bases flattened, properties/IO merged. Pointer valid until the
    // registry next changes.
    const ClassDef *resolved(const QString &name) const;
    QVector<const ClassDef *> classes(ClassKind kind) const;

    static QString toDec(const ClassDef &def);

signals:
    void changed();
    void message(const QString &text, int level); // 0 info, 1 warning, 2 error

private:
    void insert(const ClassDef &def);
    void resolveInto(const ClassDef &c, ClassDef &out, QSet<QString> &visiting) const;
    void registerBuiltins();

    std::map<QString, ClassDef> m_classes; // key = lower-case name
    mutable std::map<QString, ClassDef> m_cache;
    QStringList m_paths;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
};

void applyClassDefaults(MapObject &o, const ClassDef &resolvedDef);
bool isBuiltinKey(const QString &key);
