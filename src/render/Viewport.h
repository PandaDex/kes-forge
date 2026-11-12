#pragma once
#include "core/MapObject.h"
#include "render/Batch.h"
#include "render/Camera.h"
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QPoint>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QOpenGLTexture>
#include <memory>
#include <QSet>
#include <QTimer>

class EditorContext;

class QMimeData;

class Viewport : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    Viewport(EditorContext *ctx, ViewKind kind, QWidget *parent = nullptr);
    ~Viewport() override;

    Camera &camera() { return m_cam; }
    const Camera &camera() const { return m_cam; }
    ViewKind kind() const { return m_cam.kind; }
    bool isOrtho() const { return m_cam.isOrtho(); }
    QString title() const;

    ObjectId pickObject(const QPointF &p) const;
    bool planePoint(const QPointF &p, int axis, float value, QVector3D &out) const;
    bool pickPoint(const QPointF &p, QVector3D &out) const;

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
    bool focusNextPrevChild(bool) override { return false; } // Tab belongs to the tools

private:
    QString droppedMaterial(const QMimeData *mime) const;
    void drawGrid2D(Batch &b);
    void drawGrid3D(Batch &b);
    void drawScene(Batch &b);
    void drawSelection(Batch &b);
    void drawVerts(const QVector<Vertex> &v, GLenum mode);
    void drawCachedVerts(int first, int count, GLenum mode);
    void rebuildSceneCache();
    void paintOverlay();
    void flyTick();
    QColor objectColor(const MapObject &o) const;
    // Material-coloured / textured faces (3D view only). Returns false when the face has no
    // known material and should be drawn the old way.
    bool addMaterialFace(const BrushFace &f);
    QOpenGLTexture *materialTexture(const QString &material);
    void releaseMaterialTextures();
    QString assetPath(const QString &path) const;
    const QVector<QVector3D> *modelTriangles(const QString &path);
    const QImage *spriteImage(const QString &path);
    QString entityAsset(const MapObject &o, bool sprite) const;

    EditorContext *m_ctx;
    Camera m_cam;

    struct TexVertex
    {
        float x, y, z, u, v, r, g, b, a;
    };
    struct MaterialRange
    {
        QString material;
        int first = 0;
        int count = 0;
    };

    QOpenGLShaderProgram m_prog;
    QOpenGLShaderProgram m_texProg;
    QOpenGLBuffer m_texVbo{QOpenGLBuffer::VertexBuffer};
    QHash<QString, QVector<TexVertex>> m_pendingMaterialTris;
    QVector<MaterialRange> m_materialRanges;
    QHash<QString, std::shared_ptr<QOpenGLTexture>> m_materialTextures;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_sceneVbo{QOpenGLBuffer::VertexBuffer};
    bool m_sceneDirty = true;
    int m_sceneTriCount = 0;
    int m_sceneLineFirst = 0, m_sceneLineCount = 0;
    int m_sceneOverlayTriFirst = 0, m_sceneOverlayTriCount = 0;
    int m_sceneOverlayLineFirst = 0, m_sceneOverlayLineCount = 0;

    QPoint m_lastPos;
    QPoint m_lookAnchorGlobal;
    bool m_look = false, m_pan = false, m_space = false;
    float m_moveSpeed = 500.f;
    QSet<int> m_keys;
    QHash<QString, QVector<QVector3D>> m_modelCache;
    QSet<QString> m_failedModels;
    QHash<QString, QImage> m_spriteCache;
    QSet<QString> m_failedSprites;
    QTimer m_flyTimer;
    QElapsedTimer m_clock;
};
