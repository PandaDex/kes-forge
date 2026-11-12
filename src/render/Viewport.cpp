#include "render/Viewport.h"
#include "core/EditorContext.h"
#include "tools/MaterialTool.h"
#include "ui/AssetDrag.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTextStream>
#include <cmath>

namespace
{
    const QColor kOrange(255, 150, 30);
    constexpr float kDeg2Rad = 3.14159265358979f / 180.f;
}

Viewport::Viewport(EditorContext *ctx, ViewKind kind, QWidget *parent) : QOpenGLWidget(parent), m_ctx(ctx)
{
    m_cam.kind = kind;
    if (kind != ViewKind::Perspective)
        m_cam.zoom = 0.35f;
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setMinimumSize(120, 90);
    ctx->registerViewport(this);

    auto refresh = [this]
    { update(); };
    connect(ctx->doc(), &MapDocument::objectsChanged, this, [this]
            { m_sceneDirty = true; update(); });
    connect(ctx->doc(), &MapDocument::selectionChanged, this, refresh);
    connect(ctx, &EditorContext::gridChanged, this, refresh);
    connect(ctx, &EditorContext::activeViewportChanged, this, refresh);
    connect(ctx->registry(), &EntityRegistry::changed, this, [this]
            { m_sceneDirty = true; update(); });
    connect(ctx, &EditorContext::projectDirChanged, this, [this]
            { m_sceneDirty = true; update(); });
    connect(ctx->materials(), &MaterialLibrary::changed, this, [this]
            {
        if (context())
        {
            makeCurrent();
            releaseMaterialTextures();
            doneCurrent();
        }
        m_sceneDirty = true;
        update(); });
    connect(ctx, &EditorContext::focusRequested, this, [this](const AABB &b)
            { m_cam.focus(b); update(); });

    m_flyTimer.setInterval(16);
    connect(&m_flyTimer, &QTimer::timeout, this, &Viewport::flyTick);
}

Viewport::~Viewport()
{
    // GL objects must be released with this widget's context current.
    if (context())
    {
        makeCurrent();
        releaseMaterialTextures();
        m_vbo.destroy();
        m_sceneVbo.destroy();
        m_texVbo.destroy();
        m_vao.destroy();
        m_prog.removeAllShaders();
        m_texProg.removeAllShaders();
        doneCurrent();
    }
}

QString Viewport::title() const
{
    switch (m_cam.kind)
    {
    case ViewKind::Top:
        return tr("Top (X/Y)");
    case ViewKind::Front:
        return tr("Front (X/Z)");
    case ViewKind::Side:
        return tr("Side (Y/Z)");
    default:
        return tr("3D Perspective");
    }
}

void Viewport::initializeGL()
{
    initializeOpenGLFunctions();
    m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                   "#version 330 core\n"
                                   "layout(location=0) in vec3 aPos;\n"
                                   "layout(location=1) in vec4 aCol;\n"
                                   "uniform mat4 uMvp;\n"
                                   "out vec4 vCol;\n"
                                   "void main(){ vCol = aCol; gl_Position = uMvp * vec4(aPos, 1.0); }\n");
    m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                   "#version 330 core\n"
                                   "in vec4 vCol;\n"
                                   "out vec4 oCol;\n"
                                   "void main(){ oCol = vCol; }\n");
    if (!m_prog.link())
        m_ctx->log("Viewport shader link failed: " + m_prog.log(), LogLevel::Error);

    m_texProg.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                      "#version 330 core\n"
                                      "layout(location=0) in vec3 aPos;\n"
                                      "layout(location=1) in vec2 aUv;\n"
                                      "layout(location=2) in vec4 aCol;\n"
                                      "uniform mat4 uMvp;\n"
                                      "out vec2 vUv; out vec4 vCol;\n"
                                      "void main(){ vUv = aUv; vCol = aCol; gl_Position = uMvp * vec4(aPos, 1.0); }\n");
    m_texProg.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                      "#version 330 core\n"
                                      "in vec2 vUv; in vec4 vCol;\n"
                                      "uniform sampler2D uTex;\n"
                                      "uniform bool uUseTex;\n"
                                      "out vec4 oCol;\n"
                                      "void main(){ oCol = uUseTex ? texture(uTex, vUv) * vCol : vCol; }\n");
    if (!m_texProg.link())
        m_ctx->log("Viewport material shader link failed: " + m_texProg.log(), LogLevel::Error);

    m_vao.create();
    m_vbo.create();
    m_sceneVbo.create();
    m_texVbo.create();
}

void Viewport::drawVerts(const QVector<Vertex> &v, GLenum mode)
{
    if (v.isEmpty())
        return;
    m_vbo.bind();
    m_vbo.allocate(v.constData(), int(v.size() * sizeof(Vertex)));
    m_prog.enableAttributeArray(0);
    m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(Vertex));
    m_prog.enableAttributeArray(1);
    m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 4, sizeof(Vertex));
    glDrawArrays(mode, 0, int(v.size()));
}

void Viewport::drawCachedVerts(int first, int count, GLenum mode)
{
    if (count <= 0)
        return;
    m_sceneVbo.bind();
    m_prog.enableAttributeArray(0);
    m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(Vertex));
    m_prog.enableAttributeArray(1);
    m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 4, sizeof(Vertex));
    glDrawArrays(mode, first, count);
}

void Viewport::rebuildSceneCache()
{
    Batch scene;
    m_pendingMaterialTris.clear();
    drawScene(scene);

    QVector<TexVertex> texVertices;
    m_materialRanges.clear();
    for (auto it = m_pendingMaterialTris.cbegin(); it != m_pendingMaterialTris.cend(); ++it)
    {
        m_materialRanges.push_back({it.key(), int(texVertices.size()), int(it.value().size())});
        texVertices += it.value();
    }
    m_pendingMaterialTris.clear();
    m_texVbo.bind();
    if (!texVertices.isEmpty())
        m_texVbo.allocate(texVertices.constData(), int(texVertices.size() * sizeof(TexVertex)));
    m_texVbo.release();

    QVector<Vertex> vertices;
    const qsizetype total = scene.tris.size() + scene.lines.size() +
                            scene.otris.size() + scene.olines.size();
    vertices.reserve(int(total));
    vertices += scene.tris;
    m_sceneLineFirst = vertices.size();
    m_sceneLineCount = scene.lines.size();
    vertices += scene.lines;
    m_sceneOverlayTriFirst = vertices.size();
    m_sceneOverlayTriCount = scene.otris.size();
    vertices += scene.otris;
    m_sceneOverlayLineFirst = vertices.size();
    m_sceneOverlayLineCount = scene.olines.size();
    vertices += scene.olines;
    m_sceneTriCount = scene.tris.size();

    m_sceneVbo.bind();
    if (!vertices.isEmpty())
        m_sceneVbo.allocate(vertices.constData(), int(vertices.size() * sizeof(Vertex)));
    m_sceneVbo.release();
    m_sceneDirty = false;
}

QColor Viewport::objectColor(const MapObject &o) const
{
    if (!o.classname.isEmpty())
    {
        if (const ClassDef *d = m_ctx->registry()->resolved(o.classname))
        {
            if (d->hasColor)
                return d->color;
        }
        return o.solid ? QColor(0, 120, 220) : QColor(255, 0, 255);
    }
    return o.solid ? QColor(190, 190, 195) : QColor(80, 160, 255);
}

bool Viewport::addMaterialFace(const BrushFace &f)
{
    if (f.material.isEmpty() || f.points.size() < 3)
        return false;
    const MaterialInfo *info = m_ctx->materials()->find(f.material);
    if (!info)
        return false;

    const float shade = Batch::faceShade(f.normal());
    const QColor c = info->color;
    const float r = float(c.redF()) * shade, g = float(c.greenF()) * shade, b = float(c.blueF()) * shade;
    const QSize size = info->textureSize();
    QVector<TexVertex> &out = m_pendingMaterialTris[f.material];
    auto vertex = [&](const QVector3D &p)
    {
        const QVector2D uv = f.textureCoord(p, float(size.width()), float(size.height()));
        out.push_back({p.x(), p.y(), p.z(), uv.x(), uv.y(), r, g, b, 1.f});
    };
    for (int i = 1; i + 1 < f.points.size(); ++i)
    {
        vertex(f.points[0]);
        vertex(f.points[i]);
        vertex(f.points[i + 1]);
    }
    return true;
}

QOpenGLTexture *Viewport::materialTexture(const QString &material)
{
    auto found = m_materialTextures.constFind(material);
    if (found != m_materialTextures.cend())
        return found.value().get();

    std::shared_ptr<QOpenGLTexture> texture;
    const MaterialInfo *info = m_ctx->materials()->find(material);
    if (info && !info->texture.isNull())
    {
        texture = std::make_shared<QOpenGLTexture>(info->texture, QOpenGLTexture::GenerateMipMaps);
        // Nearest filtering keeps the PSX look the engine renders with.
        texture->setMinificationFilter(QOpenGLTexture::NearestMipMapNearest);
        texture->setMagnificationFilter(QOpenGLTexture::Nearest);
        texture->setWrapMode(QOpenGLTexture::Repeat);
    }
    m_materialTextures.insert(material, texture);   // null = untextured, cached too
    return texture.get();
}

void Viewport::releaseMaterialTextures()
{
    m_materialTextures.clear();
}

QString Viewport::assetPath(const QString &path) const
{
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    return QDir::cleanPath(info.isAbsolute() ? info.absoluteFilePath()
                                             : QDir(m_ctx->projectDir()).absoluteFilePath(path));
}

QString Viewport::entityAsset(const MapObject &o, bool sprite) const
{
    const QString key = sprite ? QStringLiteral("sprite") : QStringLiteral("model");
    const QString objectPath = o.kv.value(key).trimmed();
    if (!objectPath.isEmpty())
        return objectPath;
    if (const ClassDef *def = m_ctx->registry()->resolved(o.classname))
        return sprite ? def->sprite : def->model;
    return {};
}

const QVector<QVector3D> *Viewport::modelTriangles(const QString &path)
{
    const QString resolved = assetPath(path);
    if (resolved.isEmpty())
        return nullptr;
    auto found = m_modelCache.constFind(resolved);
    if (found != m_modelCache.cend())
        return &found.value();
    if (m_failedModels.contains(resolved))
        return nullptr;

    const QFileInfo info(resolved);
    if (info.suffix().compare("obj", Qt::CaseInsensitive) != 0)
    {
        m_failedModels.insert(resolved);
        m_ctx->log(tr("Unsupported model format (OBJ only): %1").arg(resolved), LogLevel::Warning);
        return nullptr;
    }
    QFile file(resolved);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_failedModels.insert(resolved);
        m_ctx->log(tr("Could not open model asset %1: %2").arg(resolved, file.errorString()), LogLevel::Warning);
        return nullptr;
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> triangles;
    QTextStream stream(&file);
    while (!stream.atEnd())
    {
        const QString line = stream.readLine().section('#', 0, 0).simplified();
        if (line.startsWith("v "))
        {
            const QStringList fields = line.mid(2).split(' ');
            if (fields.size() < 3)
                continue;
            bool xOk = false, yOk = false, zOk = false;
            const float x = fields[0].toFloat(&xOk);
            const float y = fields[1].toFloat(&yOk);
            const float z = fields[2].toFloat(&zOk);
            if (xOk && yOk && zOk)
                positions.push_back({x, y, z});
        }
        else if (line.startsWith("f "))
        {
            QVector<int> face;
            const QStringList fields = line.mid(2).split(' ');
            for (const QString &field : fields)
            {
                bool ok = false;
                const int raw = field.section('/', 0, 0).toInt(&ok);
                const int index = raw < 0 ? positions.size() + raw : raw - 1;
                if (!ok || index < 0 || index >= positions.size())
                {
                    face.clear();
                    break;
                }
                face.push_back(index);
            }
            for (int i = 1; i + 1 < face.size(); ++i)
                triangles << positions[face[0]] << positions[face[i]] << positions[face[i + 1]];
        }
    }
    if (triangles.isEmpty())
    {
        m_failedModels.insert(resolved);
        m_ctx->log(tr("Model asset contains no supported faces: %1").arg(resolved), LogLevel::Warning);
        return nullptr;
    }
    m_modelCache.insert(resolved, std::move(triangles));
    return &m_modelCache[resolved];
}

const QImage *Viewport::spriteImage(const QString &path)
{
    const QString resolved = assetPath(path);
    if (resolved.isEmpty())
        return nullptr;
    auto found = m_spriteCache.constFind(resolved);
    if (found != m_spriteCache.cend())
        return &found.value();
    if (m_failedSprites.contains(resolved))
        return nullptr;

    const QString suffix = QFileInfo(resolved).suffix().toLower();
    if (suffix != "png" && suffix != "jpg" && suffix != "jpeg" && suffix != "bmp")
    {
        m_failedSprites.insert(resolved);
        m_ctx->log(tr("Unsupported sprite image format (PNG, JPEG, or BMP only): %1").arg(resolved),
                   LogLevel::Warning);
        return nullptr;
    }
    QImage image(resolved);
    if (image.isNull())
    {
        m_failedSprites.insert(resolved);
        m_ctx->log(tr("Could not load sprite image (PNG, JPEG, or BMP): %1").arg(resolved), LogLevel::Warning);
        return nullptr;
    }
    m_spriteCache.insert(resolved, image);
    return &m_spriteCache[resolved];
}

void Viewport::paintGL()
{
    m_cam.size = size();
    m_cam.update();
    const bool ortho = isOrtho();

    if (ortho)
        glClearColor(0.105f, 0.105f, 0.115f, 1.f);
    else
        glClearColor(0.19f, 0.21f, 0.25f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_sceneDirty)
        rebuildSceneCache();

    Batch b;
    if (ortho)
        drawGrid2D(b);
    else
        drawGrid3D(b);
    drawSelection(b);
    if (Tool *t = m_ctx->tools()->current())
        t->draw(this, b);

    {
        m_prog.bind();
        m_prog.setUniformValue("uMvp", m_cam.viewProj);
        QOpenGLVertexArrayObject::Binder vao(&m_vao);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (ortho)
        {
            glDisable(GL_DEPTH_TEST);
        }
        else
        {
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.f, 1.f);
        }
        drawCachedVerts(0, m_sceneTriCount, GL_TRIANGLES);
        if (!ortho && !m_materialRanges.isEmpty())
        {
            m_prog.release();
            m_texProg.bind();
            m_texProg.setUniformValue("uMvp", m_cam.viewProj);
            m_texProg.setUniformValue("uTex", 0);
            m_texVbo.bind();
            m_texProg.enableAttributeArray(0);
            m_texProg.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(TexVertex));
            m_texProg.enableAttributeArray(1);
            m_texProg.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 2, sizeof(TexVertex));
            m_texProg.enableAttributeArray(2);
            m_texProg.setAttributeBuffer(2, GL_FLOAT, 5 * sizeof(float), 4, sizeof(TexVertex));
            glActiveTexture(GL_TEXTURE0);
            for (const MaterialRange &range : m_materialRanges)
            {
                QOpenGLTexture *texture = materialTexture(range.material);
                m_texProg.setUniformValue("uUseTex", texture != nullptr);
                if (texture)
                    texture->bind(0);
                glDrawArrays(GL_TRIANGLES, range.first, range.count);
                if (texture)
                    texture->release(0);
            }
            m_texProg.disableAttributeArray(2);
            m_texVbo.release();
            m_texProg.release();
            m_prog.bind();
        }
        drawVerts(b.tris, GL_TRIANGLES);
        glDisable(GL_POLYGON_OFFSET_FILL);
        drawCachedVerts(m_sceneLineFirst, m_sceneLineCount, GL_LINES);
        drawVerts(b.lines, GL_LINES);
        glDisable(GL_DEPTH_TEST);
        drawCachedVerts(m_sceneOverlayTriFirst, m_sceneOverlayTriCount, GL_TRIANGLES);
        drawVerts(b.otris, GL_TRIANGLES);
        drawCachedVerts(m_sceneOverlayLineFirst, m_sceneOverlayLineCount, GL_LINES);
        drawVerts(b.olines, GL_LINES);
        glDisable(GL_BLEND);
        m_vbo.release();
        m_sceneVbo.release();
        m_prog.release();
    }
    paintOverlay();
}

void Viewport::drawGrid2D(Batch &b)
{
    const int h = m_cam.axisH(), v = m_cam.axisV(), d = m_cam.axisD();
    float step = float(std::max(1, m_ctx->gridSize()));
    while (step * m_cam.zoom < 7.f)
        step *= 2.f;

    const float halfW = m_cam.size.width() * 0.5f / m_cam.zoom;
    const float halfH = m_cam.size.height() * 0.5f / m_cam.zoom;
    const float x0 = m_cam.center.x() - halfW, x1 = m_cam.center.x() + halfW;
    const float y0 = m_cam.center.y() - halfH, y1 = m_cam.center.y() + halfH;

    auto P = [&](float a, float c)
    { QVector3D p; p[h] = a; p[v] = c; p[d] = 0.f; return p; };
    const QColor minor(255, 255, 255, 18), major(255, 255, 255, 42);

    for (long i = long(std::floor(x0 / step)); i * step <= x1; ++i)
    {
        const float x = i * step;
        if (i == 0)
            continue;
        b.line(P(x, y0), P(x, y1), (i % 8 == 0) ? major : minor);
    }
    for (long i = long(std::floor(y0 / step)); i * step <= y1; ++i)
    {
        const float y = i * step;
        if (i == 0)
            continue;
        b.line(P(x0, y), P(x1, y), (i % 8 == 0) ? major : minor);
    }
    static const QColor axisCol[3] = {QColor(230, 70, 70, 150), QColor(80, 200, 80, 150), QColor(80, 130, 240, 150)};
    b.line(P(0, y0), P(0, y1), axisCol[v]);
    b.line(P(x0, 0), P(x1, 0), axisCol[h]);
}

void Viewport::drawGrid3D(Batch &b)
{
    const float s = float(std::max(64, m_ctx->gridSize()));
    const float R = 2048.f;
    const QColor minor(255, 255, 255, 26), major(255, 255, 255, 60);
    for (float t = -R; t <= R; t += s)
    {
        const bool mj = std::fmod(std::abs(t), 512.f) < 0.01f;
        b.line({t, -R, 0}, {t, R, 0}, mj ? major : minor);
        b.line({-R, t, 0}, {R, t, 0}, mj ? major : minor);
    }
    b.line({-R, 0, 0}, {R, 0, 0}, QColor(230, 70, 70, 200));
    b.line({0, -R, 0}, {0, R, 0}, QColor(80, 200, 80, 200));
    b.line({0, 0, 0}, {0, 0, R}, QColor(80, 130, 240, 200));
}

void Viewport::drawScene(Batch &b)
{
    const MapDocument *doc = m_ctx->doc();
    const bool ortho = isOrtho();
    for (const auto &kvp : doc->objects())
    {
        const MapObject &o = kvp.second;
        if (!doc->isVisible(o))
            continue;
        const QColor col = objectColor(o);
        const AABB bb = o.worldBounds();
        const bool isPoint = !o.solid;
        const bool hasSprite = isPoint && spriteImage(entityAsset(o, true));
        const QVector<QVector3D> *model = isPoint && !ortho && !hasSprite ? modelTriangles(entityAsset(o, false)) : nullptr;

        if (o.solid)
        {
            for (const Brush &br : o.worldBrushes())
            {
                if (ortho)
                    b.brushEdges(br, col);
                else
                {
                    for (const BrushFace &f : br.faces)
                    {
                        if (addMaterialFace(f))
                            continue;
                        const float shade = Batch::faceShade(f.normal());
                        for (int i = 1; i + 1 < f.points.size(); ++i)
                            b.tri(f.points[0], f.points[i], f.points[i + 1], col, false, shade);
                    }
                    b.brushEdges(br, QColor(15, 15, 15, 170));
                }
            }
            continue;
        }
        if (!hasSprite && (!model || ortho))
        {
            if (ortho)
                b.wireBox(bb, col);
            else
            {
                b.solidBox(bb, col.darker(110));
                b.wireBox(bb, col.lighter(150));
            }
        }
        else if (model && !ortho)
        {
            QMatrix4x4 transform;
            transform.translate(o.origin);
            transform.rotate(o.angles.y(), 0.f, 0.f, 1.f);
            transform.rotate(o.angles.x(), 0.f, 1.f, 0.f);
            transform.rotate(o.angles.z(), 1.f, 0.f, 0.f);
            bool scaleOk = false;
            const float scale = o.kv.value("scale", "1").toFloat(&scaleOk);
            transform.scale(scaleOk ? scale : 1.f);
            for (int i = 0; i + 2 < model->size(); i += 3)
                b.tri(transform * (*model)[i], transform * (*model)[i + 1], transform * (*model)[i + 2], col);
        }
        if (isPoint && !hasSprite && !model)
        {
            const float cp = std::cos(o.angles.x() * kDeg2Rad);
            const QVector3D dir(cp * std::cos(o.angles.y() * kDeg2Rad), cp * std::sin(o.angles.y() * kDeg2Rad), std::sin(o.angles.x() * kDeg2Rad));
            const QVector3D sz = bb.size();
            b.line(o.origin, o.origin + dir * (std::max({sz.x(), sz.y(), sz.z()}) * 0.5f + 12.f), col.lighter(160));
        }
    }
}

void Viewport::drawSelection(Batch &b)
{
    const MapDocument *doc = m_ctx->doc();
    const bool ortho = isOrtho();
    for (ObjectId id : doc->selection())
    {
        const MapObject *o = doc->find(id);
        if (!o || !doc->isVisible(*o))
            continue;
        const AABB bb = o->worldBounds();
        if (o->solid)
        {
            for (const Brush &br : o->worldBrushes())
            {
                b.brushEdges(br, ortho ? QColor(255, 70, 50) : kOrange, true);
                if (!ortho)
                    b.brushFaces(br, QColor(kOrange.red(), kOrange.green(), kOrange.blue(), 50), true, false);
            }
        }
        else
        {
            b.wireBox(bb, ortho ? QColor(255, 70, 50) : kOrange, true);
            if (!ortho)
                b.solidBox(bb, QColor(kOrange.red(), kOrange.green(), kOrange.blue(), 55), true);
        }
        for (const Connection &c : o->outputs)
        {
            if (const MapObject *target = doc->findByGuid(c.target))
                b.line(o->origin, target->origin, QColor(80, 220, 255), true);
        }
    }
}

void Viewport::paintOverlay()
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f = font();
    f.setPointSize(9);
    p.setFont(f);

    auto pill = [&p](const QPointF &at, const QString &text, const QColor &fg, bool accent)
    {
        const QFontMetrics fm(p.font());
        const QRectF r(at.x(), at.y(), fm.horizontalAdvance(text) + 14, fm.height() + 6);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(accent ? QPen(QColor(240, 150, 58, 200), 1) : Qt::NoPen);
        p.setBrush(QColor(18, 19, 22, 190));
        p.drawRoundedRect(r, 4, 4);
        p.setPen(fg);
        p.drawText(r, Qt::AlignCenter, text);
        p.restore();
        return r;
    };
    const bool active = m_ctx->activeViewport() == this;
    pill(QPointF(6, 6), title(), active ? QColor(255, 214, 170) : QColor(225, 225, 230), active);

    if (isOrtho())
    {
        const QString info = tr("Grid %1   Zoom %2%").arg(m_ctx->gridSize()).arg(int(m_cam.zoom * 100));
        pill(QPointF(6, height() - QFontMetrics(p.font()).height() - 12), info, QColor(200, 200, 205), false);
        if (m_cam.zoom >= 0.25f)
        {
            p.setPen(QColor(220, 220, 220, 170));
            for (const auto &kvp : m_ctx->doc()->objects())
            {
                const MapObject &o = kvp.second;
                if (o.solid || !m_ctx->doc()->isVisible(o))
                    continue;
                QPointF s;
                if (!m_cam.project(o.worldBounds().corner(1), s))
                    continue;
                p.drawText(QPointF(s.x() + 4, s.y() + 12), o.name.isEmpty() ? o.classname : o.name);
            }
        }
    }
    else
    {
        pill(QPointF(6, height() - QFontMetrics(p.font()).height() - 12), tr("Speed %1").arg(int(m_moveSpeed)), QColor(200, 200, 205), false);
    }
    const QMatrix4x4 inverseView = m_cam.view.inverted();
    const QVector3D right = inverseView.mapVector({1.f, 0.f, 0.f}).normalized();
    const QVector3D up = inverseView.mapVector({0.f, 1.f, 0.f}).normalized();
    for (const auto &entry : m_ctx->doc()->objects())
    {
        const MapObject &object = entry.second;
        if (object.solid || !m_ctx->doc()->isVisible(object))
            continue;
        const QImage *image = spriteImage(entityAsset(object, true));
        if (!image || image->height() <= 0)
            continue;
        const QVector3D half = object.worldBounds().size() * 0.5f;
        const float span = std::max({half.x(), half.y(), half.z(), 8.f}) * 2.f;
        QPointF center, rightPoint, upPoint;
        if (!m_cam.project(object.origin, center) ||
            !m_cam.project(object.origin + right * (span * 0.5f), rightPoint) ||
            !m_cam.project(object.origin + up * (span * 0.5f), upPoint))
            continue;
        float width = std::max(8.f, float(QLineF(center, rightPoint).length() * 2.0));
        float height = std::max(8.f, float(QLineF(center, upPoint).length() * 2.0));
        const float aspect = float(image->width()) / float(image->height());
        if (aspect > 1.f)
            width *= aspect;
        else
            height /= aspect;
        p.drawImage(QRectF(center.x() - width * 0.5f, center.y() - height * 0.5f, width, height), *image);
    }
    if (active)
    {
        p.setPen(QPen(QColor(240, 150, 58, 200), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
    if (Tool *t = m_ctx->tools()->current())
        t->paintOverlay(this, p);
}

ObjectId Viewport::pickObject(const QPointF &pt) const
{
    const Ray r = m_cam.ray(pt);
    const int h = m_cam.axisH(), v = m_cam.axisV();
    float best = 1e30f;
    ObjectId id = 0;
    for (const auto &kvp : m_ctx->doc()->objects())
    {
        const MapObject &o = kvp.second;
        if (!m_ctx->doc()->isVisible(o))
            continue;
        const AABB bb = o.worldBounds();
        float t = 0.f;
        if (!o.intersectRay(r, &t))
            continue;
        // 2D: prefer the smallest footprint so small objects inside big ones stay clickable.
        const float score = isOrtho() ? bb.size()[h] * bb.size()[v] : t;
        if (score < best)
        {
            best = score;
            id = o.id;
        }
    }
    return id;
}

bool Viewport::planePoint(const QPointF &p, int axis, float value, QVector3D &out) const
{
    const Ray r = m_cam.ray(p);
    if (std::abs(r.dir[axis]) < 1e-6f)
        return false;
    const float t = (value - r.origin[axis]) / r.dir[axis];
    if (!isOrtho() && t < 0.f)
        return false;
    out = r.origin + r.dir * t;
    return true;
}

bool Viewport::pickPoint(const QPointF &p, QVector3D &out) const
{
    if (isOrtho())
        return planePoint(p, m_cam.axisD(), 0.f, out);
    const Ray r = m_cam.ray(p);
    float best = 1e30f;
    bool hit = false;
    for (const auto &kvp : m_ctx->doc()->objects())
    {
        if (!m_ctx->doc()->isVisible(kvp.second))
            continue;
        float t;
        if (kvp.second.intersectRay(r, &t) && t < best && t > 0.f)
        {
            best = t;
            hit = true;
        }
    }
    if (hit)
    {
        out = r.origin + r.dir * best;
        return true;
    }
    return planePoint(p, 2, 0.f, out);
}

void Viewport::enterEvent(QEnterEvent *e)
{
    QOpenGLWidget::enterEvent(e);
    setFocus();
}

void Viewport::focusOutEvent(QFocusEvent *e)
{
    QOpenGLWidget::focusOutEvent(e);

    m_keys.clear();
    m_space = false;
    m_flyTimer.stop();
    // Switching to another application mid-drag can swallow the mouse release; abort cleanly so
    // the tool doesn't stay "busy" and block undo.
    if (e->reason() == Qt::ActiveWindowFocusReason)
        if (Tool *t = m_ctx->tools()->current(); t && t->isBusy())
            t->cancel();

    if (m_look)
    {
        m_look = false;
        m_flyTimer.stop();
        unsetCursor();
    }

    if (m_pan)
    {
        m_pan = false;
        unsetCursor();
    }
}

void Viewport::mousePressEvent(QMouseEvent *e)
{
    m_ctx->setActiveViewport(this);
    setFocus();

    m_lastPos = e->pos();

    if (!isOrtho() && e->button() == Qt::RightButton)
    {
        m_look = true;
        m_lookAnchorGlobal = e->globalPosition().toPoint();
        setCursor(Qt::BlankCursor);
        m_clock.restart();
        m_flyTimer.start();
        return;
    }

    if (isOrtho() &&
        (e->button() == Qt::MiddleButton ||
         e->button() == Qt::RightButton ||
         (e->button() == Qt::LeftButton && m_space)))
    {
        m_pan = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (Tool *t = m_ctx->tools()->current())
        t->mousePress(this, e);
}

void Viewport::mouseDoubleClickEvent(QMouseEvent *e) { mousePressEvent(e); }

QString Viewport::droppedMaterial(const QMimeData *mime) const
{
    return draggedAssetPath(mime, m_ctx->materials()->root(), {"mat"});
}

void Viewport::dragEnterEvent(QDragEnterEvent *e)
{
    if (!droppedMaterial(e->mimeData()).isEmpty())
    {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
}

void Viewport::dragMoveEvent(QDragMoveEvent *e)
{
    if (!droppedMaterial(e->mimeData()).isEmpty())
    {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
}

void Viewport::dropEvent(QDropEvent *e)
{
    const QString material = droppedMaterial(e->mimeData());
    if (material.isEmpty())
        return;
    e->setDropAction(Qt::CopyAction);
    e->accept();
    if (!m_ctx->materials()->find(material))
        m_ctx->materials()->reload();   // a new .mat, or one outside the scanned folder
    m_ctx->setActiveViewport(this);
    const bool wholeObject = QApplication::keyboardModifiers() & Qt::ShiftModifier;
    if (!MaterialOps::dropOnViewport(m_ctx, this, e->position(), material, wholeObject))
        m_ctx->log(tr("No brush under the cursor; %1 is now the current material.").arg(material), LogLevel::Warning);
    setFocus();
}

void Viewport::mouseMoveEvent(QMouseEvent *e)
{
    if (isOrtho())
    {
        QVector3D w;

        if (planePoint(e->position(), m_cam.axisD(), 0.f, w))
        {
            const char *names = "XYZ";

            m_ctx->setCursorText(
                QString("%1 %2   %3 %4")
                    .arg(names[m_cam.axisH()])
                    .arg(int(std::round(w[m_cam.axisH()])))
                    .arg(names[m_cam.axisV()])
                    .arg(int(std::round(w[m_cam.axisV()]))));
        }
    }

    if (m_look)
    {
        const QPoint delta =
            e->globalPosition().toPoint() - m_lookAnchorGlobal;
        QCursor::setPos(m_lookAnchorGlobal);

        if (!delta.isNull())
        {
            constexpr float sensitivity = 0.15f;

            m_cam.yaw -= float(delta.x()) * sensitivity;
            m_cam.pitch = std::clamp(
                m_cam.pitch - float(delta.y()) * sensitivity,
                -89.f,
                89.f);

            update();
        }

        return;
    }

    if (m_pan)
    {
        const QPoint current = e->pos();
        const QPoint delta = current - m_lastPos;
        m_lastPos = current;

        m_cam.center -= QVector2D(
            float(delta.x()) / m_cam.zoom,
            -float(delta.y()) / m_cam.zoom);

        update();
        return;
    }

    if (Tool *t = m_ctx->tools()->current())
        t->mouseMove(this, e);
}

void Viewport::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_look && e->button() == Qt::RightButton)
    {
        m_look = false;
        m_flyTimer.stop();
        unsetCursor();
        return;
    }

    if (m_pan)
    {
        m_pan = false;
        unsetCursor();
        return;
    }

    if (Tool *t = m_ctx->tools()->current())
        t->mouseRelease(this, e);
}

void Viewport::wheelEvent(QWheelEvent *e)
{
    const float steps = e->angleDelta().y() / 120.f;
    if (isOrtho())
    {
        QVector3D before;
        planePoint(e->position(), m_cam.axisD(), 0.f, before);
        m_cam.zoom = std::clamp(m_cam.zoom * std::pow(1.2f, steps), 0.01f, 32.f);
        m_cam.update();
        QVector3D after;
        if (planePoint(e->position(), m_cam.axisD(), 0.f, after))
        {
            m_cam.center += QVector2D(before[m_cam.axisH()] - after[m_cam.axisH()],
                                      before[m_cam.axisV()] - after[m_cam.axisV()]);
        }
    }
    else
    {
        m_moveSpeed = std::clamp(m_moveSpeed * std::pow(1.2f, steps), 50.f, 5000.f);
    }
    update();
}

void Viewport::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && isOrtho())
    {
        m_space = true;
        setCursor(m_look ? Qt::BlankCursor : Qt::OpenHandCursor);
    }
    if (!e->isAutoRepeat())
        m_keys.insert(e->key());
    if (Tool *t = m_ctx->tools()->current())
        t->keyPress(this, e);
    QOpenGLWidget::keyPressEvent(e);
}

void Viewport::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat())
    {
        m_space = false;
        if (m_look)
            setCursor(Qt::BlankCursor);
        else
            unsetCursor();
    }
    if (!e->isAutoRepeat())
        m_keys.remove(e->key());
    QOpenGLWidget::keyReleaseEvent(e);
}

void Viewport::flyTick()
{
    const float dt = std::min(0.05f, m_clock.restart() / 1000.f);
    QVector3D move;
    const QVector3D fwd = m_cam.forward(), right = m_cam.right();
    if (m_keys.contains(Qt::Key_W))
        move += fwd;
    if (m_keys.contains(Qt::Key_S))
        move -= fwd;
    if (m_keys.contains(Qt::Key_D))
        move += right;
    if (m_keys.contains(Qt::Key_A))
        move -= right;
    if (m_keys.contains(Qt::Key_E) || m_keys.contains(Qt::Key_Space))
        move += QVector3D(0, 0, 1);
    if (m_keys.contains(Qt::Key_Q) || m_keys.contains(Qt::Key_C))
        move -= QVector3D(0, 0, 1);
    if (move.isNull())
        return;
    m_cam.pos += move.normalized() * m_moveSpeed * dt;
    update();
}
