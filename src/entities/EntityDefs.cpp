#include "entities/EntityDefs.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

namespace
{

    struct Tok
    {
        enum Type
        {
            Ident,
            String,
            Sym,
            End
        } type = End;
        QString s;
        int line = 0;
    };

    QVector<Tok> tokenize(const QString &src)
    {
        QVector<Tok> out;
        int i = 0, line = 1;
        const int n = src.size();
        auto isSym = [](QChar c)
        { return QString("@=[](),:+").contains(c); };
        while (i < n)
        {
            const QChar c = src[i];
            if (c == '\n')
            {
                ++line;
                ++i;
                continue;
            }
            if (c.isSpace())
            {
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < n && src[i + 1] == '/')
            {
                while (i < n && src[i] != '\n')
                    ++i;
                continue;
            }
            if (c == '"')
            { // string (may span lines)
                int j = i + 1;
                QString s;
                while (j < n && src[j] != '"')
                {
                    if (src[j] == '\n')
                        ++line;
                    s += src[j];
                    ++j;
                }
                out.push_back({Tok::String, s, line});
                i = j + 1;
                continue;
            }
            if (isSym(c))
            {
                out.push_back({Tok::Sym, QString(c), line});
                ++i;
                continue;
            }
            int j = i;
            while (j < n && !src[j].isSpace() && !isSym(src[j]) && src[j] != '"')
                ++j;
            out.push_back({Tok::Ident, src.mid(i, j - i), line});
            i = j;
        }
        out.push_back({Tok::End, {}, line});
        return out;
    }

    PropType typeFromName(const QString &t)
    {
        const QString s = t.toLower();
        if (s == "integer")
            return PropType::Integer;
        if (s == "float")
            return PropType::Float;
        if (s == "boolean")
            return PropType::Boolean;
        if (s == "choices")
            return PropType::Choices;
        if (s == "flags")
            return PropType::Flags;
        if (s == "color255" || s == "color1")
            return PropType::Color;
        if (s == "vector" || s == "origin" || s == "vecline" || s == "axis")
            return PropType::Vector;
        if (s == "angle" || s == "angles")
            return PropType::Angles;
        if (s == "target_source")
            return PropType::TargetSource;
        if (s == "target_destination" || s == "target_name_or_class" || s == "node_dest")
            return PropType::TargetDest;
        if (s == "studio" || s == "model" || s == "sprite" || s == "decal" || s == "material" || s == "sound" ||
            s == "particlesystem" || s == "scene" || s == "vmat" || s == "vmdl")
            return PropType::Asset;
        return PropType::String;
    }

    struct Parser
    {
        QVector<Tok> t;
        int p = 0;
        QString file;
        QVector<ClassDef> *out = nullptr;
        QStringList *errors = nullptr;
        QSet<QString> *visited = nullptr;
        int depth = 0;

        const Tok &peek(int o = 0) const { return t[std::min(p + o, int(t.size()) - 1)]; }
        Tok next()
        {
            Tok k = peek();
            if (p < t.size() - 1)
                ++p;
            return k;
        }
        bool atEnd() const { return peek().type == Tok::End; }
        bool isSym(char c, int o = 0) const
        {
            const Tok &k = peek(o);
            return k.type == Tok::Sym && k.s.size() == 1 && k.s[0] == QLatin1Char(c);
        }
        void err(const QString &m)
        {
            errors->push_back(QString("%1:%2: %3").arg(QFileInfo(file).fileName()).arg(peek().line).arg(m));
        }

        void skipBalanced(char open, char close)
        {
            int d = 0;
            do
            {
                if (isSym(open))
                    ++d;
                else if (isSym(close))
                    --d;
                next();
            } while (d > 0 && !atEnd());
        }
        void skipDirective()
        {
            if (isSym('('))
                skipBalanced('(', ')');
            if (isSym('='))
            {
                next();
                if (peek().type == Tok::String)
                    next();
            }
            if (isSym('['))
                skipBalanced('[', ']');
        }

        // "(a b c, d e f)" -> {{a,b,c},{d,e,f}}
        QList<QStringList> parseArgGroups()
        {
            QList<QStringList> groups;
            QStringList cur;
            next();
            int d = 1;
            while (!atEnd() && d > 0)
            {
                const Tok k = next();
                if (k.type == Tok::Sym)
                {
                    if (k.s == "(")
                        ++d;
                    else if (k.s == ")")
                    {
                        if (--d == 0)
                            break;
                    }
                    else if (k.s == "," && d == 1)
                    {
                        groups.push_back(cur);
                        cur.clear();
                    }
                }
                else
                    cur << k.s;
            }
            groups.push_back(cur);
            return groups;
        }

        // "string" + "string" | bare-word | (nothing)
        QString parseValue()
        {
            const Tok k = peek();
            if (k.type == Tok::String)
            {
                next();
                QString s = k.s;
                while (isSym('+'))
                {
                    next();
                    if (peek().type == Tok::String)
                        s += next().s;
                }
                return s;
            }
            if (k.type == Tok::Ident)
            {
                next();
                return k.s;
            }
            return {};
        }

        static QVector3D vec(const QStringList &l)
        {
            return QVector3D(l.value(0).toFloat(), l.value(1).toFloat(), l.value(2).toFloat());
        }

        void run()
        {
            while (!atEnd())
            {
                if (!isSym('@'))
                {
                    err("unexpected '" + peek().s + "'");
                    next();
                    continue;
                }
                next();
                const QString dir = next().s.toLower();
                if (dir == "baseclass")
                    parseClass(ClassKind::Base);
                else if (dir == "solidclass")
                    parseClass(ClassKind::Solid);
                else if (dir == "pointclass" || dir == "npcclass" || dir == "keyframeclass" ||
                         dir == "moveclass" || dir == "filterclass")
                    parseClass(ClassKind::Point);
                else if (dir == "include")
                    parseInclude();
                else
                    skipDirective(); // @mapsize, @AutoVisGroup, @MaterialExclusion, ...
            }
        }

        void parseInclude();

        void parseClass(ClassKind kind)
        {
            ClassDef c;
            c.kind = kind;
            c.sourceFile = file;
            while (!atEnd() && !isSym('='))
            {
                const Tok m = next();
                if (m.type != Tok::Ident)
                {
                    err("unexpected '" + m.s + "' in class header");
                    continue;
                }
                QList<QStringList> args;
                if (isSym('('))
                    args = parseArgGroups();
                const QString k = m.s.toLower();
                if (k == "base")
                {
                    for (const auto &g : args)
                        for (const QString &s : g)
                            c.bases << s;
                }
                else if (k == "color" && !args.isEmpty() && args[0].size() >= 3)
                {
                    c.color = QColor(args[0][0].toInt(), args[0][1].toInt(), args[0][2].toInt());
                    c.hasColor = true;
                }
                else if (k == "size" && !args.isEmpty())
                {
                    c.hasSize = true;
                    if (args.size() >= 2)
                    {
                        c.mins = vec(args[0]);
                        c.maxs = vec(args[1]);
                    }
                    else
                    {
                        const QVector3D h = vec(args[0]) * 0.5f;
                        c.mins = -h;
                        c.maxs = h;
                    }
                }
                else if ((k == "studio" || k == "model") && !args.isEmpty() && !args[0].isEmpty())
                {
                    c.model = args[0][0];
                }
                else if ((k == "iconsprite" || k == "sprite") && !args.isEmpty() && !args[0].isEmpty())
                {
                    c.sprite = args[0][0];
                }
            }
            if (!isSym('='))
            {
                err("expected '='");
                return;
            }
            next();
            c.name = next().s;
            bool first = true;
            while (isSym(':'))
            {
                next();
                const QString v = parseValue();
                if (first)
                    c.description = v;
                first = false;
            }
            if (!isSym('['))
            {
                err("expected '[' after class header of '" + c.name + "'");
                return;
            }
            next();
            parseBody(c);
            if (isSym(']'))
                next();
            else
                err("expected ']' at end of '" + c.name + "'");
            out->push_back(c);
        }

        void parseBody(ClassDef &c)
        {
            while (!atEnd() && !isSym(']'))
            {
                const Tok k = peek();
                if (k.type != Tok::Ident)
                {
                    err("unexpected '" + k.s + "'");
                    next();
                    continue;
                }
                const QString low = k.s.toLower();
                if ((low == "input" || low == "output") && peek(1).type == Tok::Ident)
                {
                    next();
                    IODef io;
                    io.name = next().s;
                    if (isSym('('))
                    {
                        auto g = parseArgGroups();
                        if (!g.isEmpty() && !g[0].isEmpty())
                            io.type = g[0][0];
                    }
                    if (isSym(':'))
                    {
                        next();
                        io.help = parseValue();
                    }
                    (low == "input" ? c.inputs : c.outputs).push_back(io);
                    continue;
                }
                parseProperty(c);
            }
        }

        void parseProperty(ClassDef &c)
        {
            PropertyDef pd;
            pd.key = next().s;
            if (!isSym('('))
            {
                err("expected '(' after property '" + pd.key + "'");
                return;
            }
            auto g = parseArgGroups();
            pd.typeName = (!g.isEmpty() && !g[0].isEmpty()) ? g[0][0].toLower() : "string";
            pd.type = typeFromName(pd.typeName);
            while (peek().type == Tok::Ident)
            {
                const QString m = peek().s.toLower();
                if (m == "readonly")
                {
                    pd.readOnly = true;
                    next();
                }
                else if (m == "report")
                    next();
                else
                    break;
            }
            QStringList f;
            while (isSym(':') && f.size() < 3)
            {
                next();
                if (isSym(':') || isSym('=') || isSym(']'))
                    f << QString();
                else
                    f << parseValue();
            }
            while (isSym(':'))
            {
                next();
                parseValue();
            } // ignore extra fields
            pd.label = f.value(0).isEmpty() ? pd.key : f.value(0);
            pd.defaultValue = f.value(1);
            pd.help = f.value(2);

            if (isSym('='))
            {
                next();
                if (!isSym('['))
                {
                    err("expected '[' after '='");
                    return;
                }
                next();
                while (!atEnd() && !isSym(']'))
                {
                    const QString val = next().s;
                    QString label, extra;
                    if (isSym(':'))
                    {
                        next();
                        label = parseValue();
                        if (isSym(':'))
                        {
                            next();
                            extra = parseValue();
                        }
                    }
                    while (isSym(':'))
                    {
                        next();
                        parseValue();
                    }
                    if (pd.type == PropType::Flags)
                        pd.flags.push_back({val.toInt(), label, extra == "1"});
                    else
                        pd.choices.push_back({val, label.isEmpty() ? val : label});
                }
                if (isSym(']'))
                    next();
            }
            if (pd.type == PropType::Flags)
            {
                int v = 0;
                for (const FlagItem &fi : pd.flags)
                    if (fi.defaultOn)
                        v |= fi.bit;
                pd.defaultValue = QString::number(v);
            }
            c.props.push_back(pd);
        }
    };

    bool parseDecFile(const QString &path, QVector<ClassDef> &out, QStringList &errors,
                      QSet<QString> &visited, int depth);

    void Parser::parseInclude()
    {
        const Tok f = next();
        if (depth > 8)
        {
            err("include depth exceeded");
            return;
        }
        const QString inc = QFileInfo(file).absoluteDir().filePath(f.s);
        parseDecFile(inc, *out, *errors, *visited, depth + 1);
    }

    bool parseDecFile(const QString &path, QVector<ClassDef> &out, QStringList &errors,
                      QSet<QString> &visited, int depth)
    {
        const QString canon = QFileInfo(path).canonicalFilePath();
        if (canon.isEmpty())
        {
            errors << "cannot find " + path;
            return false;
        }
        if (visited.contains(canon))
            return true;
        visited.insert(canon);
        QFile f(canon);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            errors << "cannot open " + path;
            return false;
        }
        Parser p;
        p.t = tokenize(QString::fromUtf8(f.readAll()));
        p.file = canon;
        p.out = &out;
        p.errors = &errors;
        p.visited = &visited;
        p.depth = depth;
        p.run();
        return true;
    }

    int indexOfKey(const QList<PropertyDef> &l, const QString &k)
    {
        for (int i = 0; i < l.size(); ++i)
            if (l[i].key.compare(k, Qt::CaseInsensitive) == 0)
                return i;
        return -1;
    }

}

EntityRegistry::EntityRegistry(QObject *parent) : QObject(parent)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(250);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this]
            {
        emit message(tr("Entity definitions changed on disk - reloading..."), 0);
        reloadAll(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
}

void EntityRegistry::insert(const ClassDef &def)
{
    m_classes[def.name.toLower()] = def;
    m_cache.clear();
}

void EntityRegistry::declare(const ClassDef &def)
{
    insert(def);
    emit changed();
}

void EntityRegistry::addSearchPath(const QString &dir)
{
    if (!QDir(dir).exists() || m_paths.contains(dir))
        return;
    m_paths << dir;
    m_watcher.addPath(dir);
}

void EntityRegistry::reloadAll()
{
    m_classes.clear();
    m_cache.clear();
    QStringList errors;
    QSet<QString> visited;
    int files = 0;
    for (const QString &dir : m_paths)
    {
        const auto list = QDir(dir).entryInfoList({"*.dec"}, QDir::Files, QDir::Name);
        for (const QFileInfo &fi : list)
        {
            QVector<ClassDef> defs;
            parseDecFile(fi.absoluteFilePath(), defs, errors, visited, 0);
            for (const ClassDef &d : defs)
                insert(d);
            if (!m_watcher.files().contains(fi.absoluteFilePath()))
                m_watcher.addPath(fi.absoluteFilePath());
            ++files;
        }
    }
    if (m_classes.empty())
    {
        registerBuiltins();
        emit message(tr("No .dec files found - using built-in entity classes."), 1);
    }
    else
    {
        emit message(tr("Loaded %1 entity classes from %2 file(s).").arg(m_classes.size()).arg(files), 0);
    }
    for (const QString &e : errors)
        emit message("DEC: " + e, 1);
    emit changed();
}

bool EntityRegistry::loadFile(const QString &path, QStringList *errors)
{
    QStringList local;
    QSet<QString> visited;
    QVector<ClassDef> defs;
    const bool ok = parseDecFile(path, defs, local, visited, 0);
    for (const ClassDef &d : defs)
        insert(d);
    if (errors)
        *errors = local;
    for (const QString &e : local)
        emit message("DEC: " + e, 1);
    emit changed();
    return ok;
}

const ClassDef *EntityRegistry::find(const QString &name) const
{
    auto it = m_classes.find(name.toLower());
    return it == m_classes.end() ? nullptr : &it->second;
}

void EntityRegistry::resolveInto(const ClassDef &c, ClassDef &out, QSet<QString> &visiting) const
{
    const QString key = c.name.toLower();
    if (visiting.contains(key))
        return; // inheritance cycle
    visiting.insert(key);

    ClassDef merged;
    auto mergeIn = [&merged](const ClassDef &src)
    {
        for (const PropertyDef &p : src.props)
        {
            const int i = indexOfKey(merged.props, p.key);
            if (i < 0)
            {
                merged.props.push_back(p);
                continue;
            }
            PropertyDef &dst = merged.props[i];
            if (dst.type == PropType::Flags && p.type == PropType::Flags)
            { // flags accumulate
                for (const FlagItem &f : p.flags)
                {
                    bool found = false;
                    for (FlagItem &d : dst.flags)
                        if (d.bit == f.bit)
                        {
                            d = f;
                            found = true;
                        }
                    if (!found)
                        dst.flags.push_back(f);
                }
                int v = 0;
                for (const FlagItem &f : dst.flags)
                    if (f.defaultOn)
                        v |= f.bit;
                dst.defaultValue = QString::number(v);
            }
            else
                dst = p;
        }
        for (const IODef &io : src.inputs)
        {
            bool f = false;
            for (const IODef &e : merged.inputs)
                if (e.name == io.name)
                    f = true;
            if (!f)
                merged.inputs.push_back(io);
        }
        for (const IODef &io : src.outputs)
        {
            bool f = false;
            for (const IODef &e : merged.outputs)
                if (e.name == io.name)
                    f = true;
            if (!f)
                merged.outputs.push_back(io);
        }
        if (src.hasColor)
        {
            merged.color = src.color;
            merged.hasColor = true;
        }
        if (src.hasSize)
        {
            merged.mins = src.mins;
            merged.maxs = src.maxs;
            merged.hasSize = true;
        }
        if (!src.model.isEmpty())
            merged.model = src.model;
        if (!src.sprite.isEmpty())
            merged.sprite = src.sprite;
    };

    for (const QString &b : c.bases)
    {
        auto it = m_classes.find(b.toLower());
        if (it == m_classes.end())
            continue;
        ClassDef tmp;
        resolveInto(it->second, tmp, visiting);
        mergeIn(tmp);
    }
    mergeIn(c);
    merged.kind = c.kind;
    merged.name = c.name;
    merged.description = c.description;
    merged.bases = c.bases;
    merged.sourceFile = c.sourceFile;
    out = merged;
    visiting.remove(key);
}

const ClassDef *EntityRegistry::resolved(const QString &name) const
{
    const QString key = name.toLower();
    auto cached = m_cache.find(key);
    if (cached != m_cache.end())
        return &cached->second;
    auto it = m_classes.find(key);
    if (it == m_classes.end())
        return nullptr;
    QSet<QString> visiting;
    ClassDef r;
    resolveInto(it->second, r, visiting);
    return &(m_cache[key] = r);
}

QVector<const ClassDef *> EntityRegistry::classes(ClassKind kind) const
{
    QVector<const ClassDef *> v;
    for (const auto &kvp : m_classes)
        if (kvp.second.kind == kind)
            v.push_back(&kvp.second);
    return v;
}

void EntityRegistry::registerBuiltins()
{
    ClassDef t;
    t.kind = ClassKind::Base;
    t.name = "Targetname";
    t.props.push_back({"targetname", "target_source", PropType::TargetSource, "Name", "", "", false, {}, {}});
    insert(t);

    ClassDef ps;
    ps.kind = ClassKind::Point;
    ps.name = "info_player_start";
    ps.bases = {"Targetname"};
    ps.description = "Player spawn point.";
    ps.hasColor = true;
    ps.color = QColor(0, 255, 0);
    ps.hasSize = true;
    ps.mins = {-16, -16, 0};
    ps.maxs = {16, 16, 72};
    insert(ps);

    ClassDef l;
    l.kind = ClassKind::Point;
    l.name = "light_omni";
    l.bases = {"Targetname"};
    l.description = "Omni-directional light.";
    l.hasColor = true;
    l.color = QColor(255, 220, 80);
    l.props.push_back({"color", "color255", PropType::Color, "Color", "255 255 255", "", false, {}, {}});
    l.props.push_back({"brightness", "float", PropType::Float, "Brightness", "1", "", false, {}, {}});
    insert(l);

    ClassDef b;
    b.kind = ClassKind::Solid;
    b.name = "func_brush";
    b.bases = {"Targetname"};
    b.description = "Generic solid entity.";
    b.hasColor = true;
    b.color = QColor(0, 120, 220);
    insert(b);

    ClassDef tm;
    tm.kind = ClassKind::Solid;
    tm.name = "trigger_multiple";
    tm.bases = {"Targetname"};
    tm.description = "Trigger volume.";
    tm.hasColor = true;
    tm.color = QColor(255, 120, 0);
    tm.outputs.push_back({"OnStartTouch", "void", ""});
    tm.outputs.push_back({"OnEndTouch", "void", ""});
    insert(tm);
}

bool isBuiltinKey(const QString &key)
{
    const QString k = key.toLower();
    return k == "targetname" || k == "origin" || k == "angles" || k == "classname";
}

void applyClassDefaults(MapObject &o, const ClassDef &def)
{
    o.classname = def.name;
    if (!o.solid)
    {
        o.mins = def.hasSize ? def.mins : QVector3D(-8, -8, -8);
        o.maxs = def.hasSize ? def.maxs : QVector3D(8, 8, 8);
    }
    for (const PropertyDef &p : def.props)
    {
        if (isBuiltinKey(p.key))
            continue;
        if (!o.kv.contains(p.key) && !p.defaultValue.isEmpty())
            o.kv[p.key] = p.defaultValue;
    }
}

QString EntityRegistry::toDec(const ClassDef &d)
{
    QString s;
    QTextStream o(&s);
    o << (d.kind == ClassKind::Solid ? "@SolidClass" : d.kind == ClassKind::Base ? "@BaseClass"
                                                                                 : "@PointClass");
    if (!d.bases.isEmpty())
        o << " base(" << d.bases.join(", ") << ")";
    if (d.hasColor)
        o << " color(" << d.color.red() << " " << d.color.green() << " " << d.color.blue() << ")";
    if (d.hasSize && d.kind == ClassKind::Point)
        o << " size(" << d.mins.x() << " " << d.mins.y() << " " << d.mins.z() << ", "
          << d.maxs.x() << " " << d.maxs.y() << " " << d.maxs.z() << ")";
    o << " = " << d.name << " : \"" << d.description << "\"\n[\n";
    for (const PropertyDef &p : d.props)
    {
        o << "\t" << p.key << "(" << p.typeName << ") : \"" << p.label << "\" : ";
        const bool numeric = p.type == PropType::Integer || p.type == PropType::Float || p.type == PropType::Boolean;
        if (numeric && !p.defaultValue.isEmpty())
            o << p.defaultValue;
        else
            o << "\"" << p.defaultValue << "\"";
        if (!p.help.isEmpty())
            o << " : \"" << p.help << "\"";
        o << "\n";
    }
    for (const IODef &i : d.inputs)
        o << "\tinput " << i.name << "(" << (i.type.isEmpty() ? "void" : i.type) << ") : \"" << i.help << "\"\n";
    for (const IODef &i : d.outputs)
        o << "\toutput " << i.name << "(" << (i.type.isEmpty() ? "void" : i.type) << ") : \"" << i.help << "\"\n";
    o << "]\n";
    return s;
}
