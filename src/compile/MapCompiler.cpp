#include "compile/MapCompiler.h"
#include "core/MaterialLibrary.h"
#include "entities/EntityDefs.h"
#include <QMap>
#include <QDataStream>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    bool finite(const QVector3D &v)
    {
        return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
    }

    quint32 materialIndex(CompileContext &ctx, QHash<QString, quint32> &lookup, const QString &material)
    {
        auto it = lookup.constFind(material);
        if (it != lookup.cend())
            return it.value();
        const quint32 index = quint32(ctx.materialNames.size());
        ctx.materialNames << material;
        lookup.insert(material, index);
        return index;
    }

    // Appends a brush's faces to the per-(owner, material) meshes. `originOffset` is
    // subtracted from positions (entity geometry is stored relative to the entity).
    void addBrushGeometry(CompileContext &ctx, QHash<QString, quint32> &materialLookup,
                          QMap<QPair<qint32, quint32>, int> &meshLookup,
                          const Brush &b, qint32 owner, const QVector3D &originOffset)
    {
        for (const BrushFace &f : b.faces)
        {
            if (f.points.size() < 3)
                continue;
            const quint32 material = materialIndex(ctx, materialLookup, f.material);
            const QPair<qint32, quint32> key(owner, material);
            int meshIndex = meshLookup.value(key, -1);
            if (meshIndex < 0)
            {
                meshIndex = ctx.meshes.size();
                ctx.meshes.push_back({owner, material, {}, {}});
                meshLookup.insert(key, meshIndex);
            }
            CompileMesh &mesh = ctx.meshes[meshIndex];

            const QSize texSize = ctx.materials ? ctx.materials->textureSize(f.material) : QSize(64, 64);
            const QVector3D n = f.normal();
            const quint32 base = quint32(mesh.vertices.size());
            for (const QVector3D &p : f.points)
                mesh.vertices.push_back({p - originOffset, n, f.textureCoord(p, float(texSize.width()), float(texSize.height()))});
            for (int i = 1; i + 1 < f.points.size(); ++i)
                mesh.indices << base << base + quint32(i) << base + quint32(i + 1);
        }
    }

    void diagnostic(CompileContext &ctx, const QString &level, const QString &text)
    {
        ctx.log << QString("%1: %2").arg(level, text);
    }

    bool writeString(QDataStream &stream, const QString &value)
    {
        const QByteArray bytes = value.toUtf8();
        if (quint64(bytes.size()) > std::numeric_limits<quint32>::max())
        {
            stream.setStatus(QDataStream::WriteFailed);
            return false;
        }
        stream << quint32(bytes.size());
        return bytes.isEmpty() || stream.writeRawData(bytes.constData(), bytes.size()) == bytes.size();
    }

    // 16 bytes in RFC 4122 (big-endian) order; a null GUID is all zeros.
    bool writeGuid(QDataStream &stream, const QUuid &guid)
    {
        const QByteArray bytes = guid.toRfc4122();
        return stream.writeRawData(bytes.constData(), 16) == 16;
    }

    bool writeVector(QDataStream &stream, const QVector3D &value)
    {
        stream << value.x() << value.y() << value.z();
        return stream.status() == QDataStream::Ok;
    }

    // Chunk = 4-char id + u32 size + payload, built in memory first so the size is known.
    class ChunkWriter
    {
    public:
        ChunkWriter() : m_stream(&m_bytes, QIODevice::WriteOnly)
        {
            m_stream.setByteOrder(QDataStream::LittleEndian);
            m_stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        }
        QDataStream &stream() { return m_stream; }
        bool writeTo(QDataStream &out, const char id[4]) const
        {
            if (m_stream.status() != QDataStream::Ok || quint64(m_bytes.size()) > std::numeric_limits<quint32>::max())
                return false;
            out.writeRawData(id, 4);
            out << quint32(m_bytes.size());
            out.writeRawData(m_bytes.constData(), int(m_bytes.size()));
            return out.status() == QDataStream::Ok;
        }

    private:
        QByteArray m_bytes;
        QDataStream m_stream;
    };

    class ValidateStage final : public ICompileStage
    {
    public:
        QString name() const override { return QObject::tr("Validate map"); }

        bool run(CompileContext &ctx) override
        {
            if (!ctx.registry)
            {
                diagnostic(ctx, "Error", QObject::tr("Entity registry is unavailable."));
                return false;
            }

            QHash<QString, const MapObject *> namedObjects;
            QHash<QUuid, const MapObject *> guidObjects;
            bool valid = true;
            for (const MapObject &o : ctx.map)
            {
                if (!o.classname.isEmpty())
                {
                    if (o.guid.isNull())
                    {
                        diagnostic(ctx, "Error", QObject::tr("Entity %1 has no GUID.").arg(o.id));
                        valid = false;
                    }
                    else if (guidObjects.contains(o.guid))
                    {
                        diagnostic(ctx, "Error", QObject::tr("Entity %1 has the same GUID as entity %2.")
                                                     .arg(o.id).arg(guidObjects.value(o.guid)->id));
                        valid = false;
                    }
                    else
                    {
                        guidObjects.insert(o.guid, &o);
                    }
                }
                if (o.name.isEmpty())
                    continue;
                if (namedObjects.contains(o.name))
                    diagnostic(ctx, "Warning", QObject::tr("Duplicate entity name '%1'.").arg(o.name));
                else
                    namedObjects.insert(o.name, &o);
            }

            for (const MapObject &o : ctx.map)
            {
                if (ctx.cancelRequested)
                    return false;
                const AABB bounds = o.worldBounds();
                if (!finite(o.origin) || !finite(o.angles) || !finite(o.mins) || !finite(o.maxs) ||
                    !finite(bounds.mn) || !finite(bounds.mx) ||
                    o.mins.x() > o.maxs.x() || o.mins.y() > o.maxs.y() || o.mins.z() > o.maxs.z())
                {
                    diagnostic(ctx, "Error", QObject::tr("Object %1 has invalid or non-finite bounds.").arg(o.id));
                    valid = false;
                }

                if (o.solid)
                {
                    const QVector<Brush> brushes = o.worldBrushes();
                    for (int i = 0; i < brushes.size(); ++i)
                        if (!brushes[i].isValid())
                        {
                            diagnostic(ctx, "Error", QObject::tr("Object %1 has an invalid brush (#%2).").arg(o.id).arg(i));
                            valid = false;
                        }
                    if (brushes.isEmpty())
                    {
                        diagnostic(ctx, "Error", QObject::tr("Solid object %1 has no brushes.").arg(o.id));
                        valid = false;
                    }
                }

                if (o.classname.isEmpty())
                {
                    if (!o.solid)
                    {
                        diagnostic(ctx, "Error", QObject::tr("Point object %1 has no entity class.").arg(o.id));
                        valid = false;
                    }
                    continue;
                }

                const ClassDef *def = ctx.registry->resolved(o.classname);
                if (!def)
                {
                    diagnostic(ctx, "Error", QObject::tr("Object %1 uses unknown entity class '%2'.").arg(o.id).arg(o.classname));
                    valid = false;
                    continue;
                }
                if (def->kind == ClassKind::Base || (def->kind == ClassKind::Solid) != o.solid)
                {
                    diagnostic(ctx, "Error", QObject::tr("Object %1 does not match the point/solid type of class '%2'.")
                                                              .arg(o.id).arg(o.classname));
                    valid = false;
                }

                for (const Connection &connection : o.outputs)
                {
                    if (std::none_of(def->outputs.cbegin(), def->outputs.cend(), [&connection](const IODef &output)
                                     { return output.name.compare(connection.output, Qt::CaseInsensitive) == 0; }))
                        diagnostic(ctx, "Warning", QObject::tr("Class '%1' does not declare output '%2'.")
                                                       .arg(o.classname, connection.output));

                    const MapObject *targetObject = guidObjects.value(connection.target, nullptr);
                    if (!targetObject)
                    {
                        const QString target = connection.target.isNull() ? connection.legacyTarget
                                                                          : connection.target.toString(QUuid::WithoutBraces);
                        diagnostic(ctx, "Warning", QObject::tr("Output '%1' on %2 targets '%3', which is not an entity in this map; the link is left out.")
                                                       .arg(connection.output, o.displayName(), target.isEmpty() ? QObject::tr("(none)") : target));
                        continue;
                    }
                    const ClassDef *targetDef = ctx.registry->resolved(targetObject->classname);
                    if (targetDef && std::none_of(targetDef->inputs.cbegin(), targetDef->inputs.cend(), [&connection](const IODef &input)
                                                  { return input.name.compare(connection.input, Qt::CaseInsensitive) == 0; }))
                        diagnostic(ctx, "Warning", QObject::tr("Target '%1' has no declared input '%2'.")
                                                       .arg(targetObject->displayName(), connection.input));
                }
            }

            if (valid)
                diagnostic(ctx, "Info", QObject::tr("Validated %1 map objects.").arg(ctx.map.size()));
            return valid;
        }
    };

    class GeometryStage final : public ICompileStage
    {
    public:
        QString name() const override { return QObject::tr("Build geometry"); }

        bool run(CompileContext &ctx) override
        {
            QHash<QString, quint32> materialLookup;
            QMap<QPair<qint32, quint32>, int> meshLookup;
            qint32 entityIndex = 0;
            for (const MapObject &o : ctx.map)
            {
                if (ctx.cancelRequested)
                    return false;
                const bool world = o.classname.isEmpty();
                if (o.solid)
                {
                    const qint32 owner = world ? -1 : entityIndex;
                    const QVector3D offset = world ? QVector3D() : o.origin;
                    for (const Brush &b : o.worldBrushes())
                    {
                        addBrushGeometry(ctx, materialLookup, meshLookup, b, owner, offset);

                        CompileBrush hull;
                        hull.entity = owner;
                        const AABB bounds = b.bounds();
                        hull.bounds = AABB(bounds.mn - offset, bounds.mx - offset);
                        for (const Plane &p : b.planes())
                            hull.planes.push_back({p.normal, p.dist - QVector3D::dotProduct(p.normal, offset)});
                        ctx.brushes.push_back(hull);
                    }
                }
                if (!world)
                    ++entityIndex;
            }

            int vertexCount = 0, indexCount = 0;
            for (const CompileMesh &m : ctx.meshes)
            {
                vertexCount += m.vertices.size();
                indexCount += m.indices.size();
            }
            diagnostic(ctx, "Info", QObject::tr("Built %1 meshes (%2 vertices, %3 indices), %4 materials and %5 convex brushes.")
                                           .arg(ctx.meshes.size()).arg(vertexCount).arg(indexCount)
                                           .arg(ctx.materialNames.size()).arg(ctx.brushes.size()));
            for (const QString &material : ctx.materialNames)
            {
                if (!material.isEmpty() && ctx.materials && !ctx.materials->find(material))
                    diagnostic(ctx, "Warning", QObject::tr("Material '%1' was not found in the content directory.").arg(material));
            }
            return true;
        }
    };

    class WriteStage final : public ICompileStage
    {
    public:
        QString name() const override { return QObject::tr("Write compiled map"); }

        bool run(CompileContext &ctx) override
        {
            QSaveFile file(ctx.settings.outputPath);
            if (!file.open(QIODevice::WriteOnly))
            {
                diagnostic(ctx, "Error", QObject::tr("Cannot create '%1': %2").arg(ctx.settings.outputPath, file.errorString()));
                return false;
            }

            QDataStream stream(&file);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            stream.writeRawData("FORGECMP", 8);
            stream << quint32(3) << quint32(0); // version, flags

            bool ok = true;

            // MATL: material paths ("" = engine default).
            {
                ChunkWriter chunk;
                chunk.stream() << quint32(ctx.materialNames.size());
                for (const QString &m : ctx.materialNames)
                    writeString(chunk.stream(), m);
                ok = ok && chunk.writeTo(stream, "MATL");
            }

            {
                ChunkWriter chunk;
                QDataStream &out = chunk.stream();
                out << quint32(ctx.meshes.size());
                for (const CompileMesh &mesh : ctx.meshes)
                {
                    out << mesh.entity << mesh.material << quint32(mesh.vertices.size()) << quint32(mesh.indices.size());
                    for (const CompileVertex &v : mesh.vertices)
                    {
                        writeVector(out, v.position);
                        writeVector(out, v.normal);
                        out << v.uv.x() << v.uv.y();
                    }
                    for (quint32 index : mesh.indices)
                        out << index;
                }
                ok = ok && chunk.writeTo(stream, "GEOM");
            }

            // ENTS: every classed object, in map order (the index meshes/brushes refer to).
            {
                ChunkWriter chunk;
                QDataStream &out = chunk.stream();
                QSet<QUuid> entityGuids;
                for (const MapObject &o : ctx.map)
                    if (!o.classname.isEmpty())
                        entityGuids.insert(o.guid);
                const quint32 entityCount = quint32(std::count_if(ctx.map.cbegin(), ctx.map.cend(), [](const MapObject &o)
                                                                   { return !o.classname.isEmpty(); }));
                out << entityCount;
                for (const MapObject &o : ctx.map)
                {
                    if (o.classname.isEmpty())
                        continue;
                    writeString(out, o.classname);
                    writeString(out, o.name);
                    writeGuid(out, o.guid);
                    out << quint8(o.solid ? 1 : 0);
                    writeVector(out, o.origin);
                    writeVector(out, o.angles);
                    writeVector(out, o.mins);
                    writeVector(out, o.maxs);
                    out << quint32(o.kv.size());
                    for (auto it = o.kv.cbegin(); it != o.kv.cend(); ++it)
                    {
                        writeString(out, it.key());
                        writeString(out, it.value());
                    }
                    // Links to objects that are not compiled entities are left out (warned above).
                    QVector<const Connection *> links;
                    for (const Connection &c : o.outputs)
                        if (entityGuids.contains(c.target) && !c.output.isEmpty() && !c.input.isEmpty())
                            links.push_back(&c);
                    out << quint32(links.size());
                    for (const Connection *link : links)
                    {
                        const Connection &c = *link;
                        writeString(out, c.output);
                        writeGuid(out, c.target);
                        writeString(out, c.input);
                        writeString(out, c.param);
                        out << c.delay << quint8(c.once ? 1 : 0);
                    }
                }
                ok = ok && chunk.writeTo(stream, "ENTS");
            }

            {
                ChunkWriter chunk;
                QDataStream &out = chunk.stream();
                out << quint32(ctx.brushes.size());
                for (const CompileBrush &b : ctx.brushes)
                {
                    out << b.entity;
                    writeVector(out, b.bounds.mn);
                    writeVector(out, b.bounds.mx);
                    out << quint32(b.planes.size());
                    for (const Plane &p : b.planes)
                    {
                        writeVector(out, p.normal);
                        out << p.dist;
                    }
                }
                ok = ok && chunk.writeTo(stream, "BRSH");
            }

            if (!ok || stream.status() != QDataStream::Ok)
            {
                diagnostic(ctx, "Error", QObject::tr("Failed while writing compiled map data."));
                file.cancelWriting();
                return false;
            }
            if (!file.commit())
            {
                diagnostic(ctx, "Error", QObject::tr("Cannot finalize '%1': %2").arg(ctx.settings.outputPath, file.errorString()));
                return false;
            }
            diagnostic(ctx, "Info", QObject::tr("Compiled map written to %1").arg(QFileInfo(ctx.settings.outputPath).absoluteFilePath()));
            return true;
        }
    };

    class StagedMapCompiler final : public IMapCompiler
    {
    public:
        void addStage(std::unique_ptr<ICompileStage> stage) override
        {
            m_stages.push_back(std::move(stage));
        }

        bool compile(CompileContext &ctx) override
        {
            if (ctx.settings.outputPath.isEmpty())
            {
                diagnostic(ctx, "Error", QObject::tr("No output path was specified."));
                return false;
            }
            for (const auto &stage : m_stages)
            {
                if (ctx.cancelRequested)
                {
                    diagnostic(ctx, "Warning", QObject::tr("Compilation cancelled."));
                    return false;
                }
                diagnostic(ctx, "Info", QObject::tr("%1...").arg(stage->name()));
                if (!stage->run(ctx))
                    return false;
            }
            return true;
        }

    private:
        std::vector<std::unique_ptr<ICompileStage>> m_stages;
    };
}

bool validateMap(CompileContext &ctx)
{
    ValidateStage stage;
    return stage.run(ctx);
}

std::unique_ptr<IMapCompiler> createMapCompiler()
{
    auto compiler = std::make_unique<StagedMapCompiler>();
    compiler->addStage(std::make_unique<ValidateStage>());
    compiler->addStage(std::make_unique<GeometryStage>());
    compiler->addStage(std::make_unique<WriteStage>());
    return compiler;
}
