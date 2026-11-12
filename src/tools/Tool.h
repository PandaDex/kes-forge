#pragma once
#include <QIcon>
#include <QObject>
#include <QString>
#include <QVector>

class QMouseEvent;
class QKeyEvent;
class QPainter;
class QWidget;
class Viewport;
class EditorContext;
struct Batch;

// New tools are registered in EditorContext's constructor; the toolbar, shortcuts and Tool Options
// dock pick them up from there.
class Tool : public QObject
{
    Q_OBJECT
public:
    explicit Tool(EditorContext *ctx) : QObject(nullptr), m_ctx(ctx) {}

    virtual QString id() const = 0;
    virtual QString title() const = 0;
    virtual QString shortcut() const { return {}; }
    virtual QString statusHint() const { return {}; }

    virtual void activate() {}
    virtual void deactivate() {}

    virtual void mousePress(Viewport *, QMouseEvent *) {}
    virtual void mouseMove(Viewport *, QMouseEvent *) {}
    virtual void mouseRelease(Viewport *, QMouseEvent *) {}
    virtual void keyPress(Viewport *, QKeyEvent *) {}
    virtual void draw(Viewport *, Batch &) {}            // world-space preview geometry
    virtual void paintOverlay(Viewport *, QPainter &) {} // screen-space overlay
    virtual QWidget *createOptionsPanel(QWidget * /*parent*/) { return nullptr; }
    // True while an interactive drag holds uncommitted preview state. Document-changing commands
    // (undo, delete, paste...) are refused until it ends, so the preview can't desync the history.
    virtual bool isBusy() const { return false; }
    virtual void cancel() {}

protected:
    EditorContext *m_ctx;
};

class ToolManager : public QObject
{
    Q_OBJECT
public:
    explicit ToolManager(QObject *parent = nullptr) : QObject(parent) {}

    void add(Tool *t)
    {
        t->setParent(this);
        m_tools.push_back(t);
    }
    const QVector<Tool *> &tools() const { return m_tools; }
    Tool *current() const { return m_current; }
    Tool *byId(const QString &id) const
    {
        for (Tool *t : m_tools)
            if (t->id() == id)
                return t;
        return nullptr;
    }
    bool isBusy() const { return m_current && m_current->isBusy(); }
    void setCurrent(const QString &id)
    {
        Tool *t = byId(id);
        if (!t || t == m_current || isBusy())
            return;
        if (m_current)
            m_current->deactivate();
        m_current = t;
        m_current->activate();
        emit currentChanged(m_current);
    }

signals:
    void currentChanged(Tool *tool);

private:
    QVector<Tool *> m_tools;
    Tool *m_current = nullptr;
};
