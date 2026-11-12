#pragma once
#include <QWidget>

class EditorContext;
class QLineEdit;
class QTabWidget;
class QTreeWidget;

class OutlinerPanel : public QWidget
{
    Q_OBJECT
public:
    explicit OutlinerPanel(EditorContext *ctx, QWidget *parent = nullptr);

private:
    void rebuild(bool force = false);
    void syncSelectionFromDoc();
    void setSelectionGroup();
    void setSelectionVisgroup();
    void updateSelection(const QList<QTreeWidget *> &trees);

    EditorContext *m_ctx;
    QLineEdit *m_filter;
    QTabWidget *m_tabs;
    QTreeWidget *m_hierarchy;
    QTreeWidget *m_visgroups;
    bool m_syncing = false;
    bool m_updatingVisibility = false;
    quint64 m_signature = 0;
};
