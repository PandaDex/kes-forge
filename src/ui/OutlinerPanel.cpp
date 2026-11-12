#include "ui/OutlinerPanel.h"
#include "core/EditorContext.h"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace
{
    constexpr int kIdRole = Qt::UserRole + 1;
    constexpr int kKindRole = Qt::UserRole + 2;
    constexpr int kPathRole = Qt::UserRole + 3;
    constexpr int kObject = 1;
    constexpr int kGroup = 2;
    constexpr int kVisgroup = 3;

    QTreeWidget *makeTree(QWidget *parent, const QStringList &headers)
    {
        auto *tree = new QTreeWidget(parent);
        tree->setColumnCount(headers.size());
        tree->setHeaderLabels(headers);
        tree->header()->setStretchLastSection(true);
        tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        tree->setAlternatingRowColors(true);
        tree->setUniformRowHeights(true);
        return tree;
    }

    QTreeWidgetItem *appendObject(QTreeWidgetItem *parent, const MapObject &o, const QString &label)
    {
        auto *item = new QTreeWidgetItem(parent, {label, o.classname});
        item->setData(0, kKindRole, kObject);
        item->setData(0, kIdRole, o.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, o.visible ? Qt::Checked : Qt::Unchecked);
        return item;
    }
}

OutlinerPanel::OutlinerPanel(EditorContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Search..."));
    m_filter->setClearButtonEnabled(true);
    layout->addWidget(m_filter);

    auto *buttons = new QHBoxLayout;
    auto *groupButton = new QPushButton(tr("Group Selection..."), this);
    auto *ungroupButton = new QPushButton(tr("Ungroup"), this);
    auto *visgroupButton = new QPushButton(tr("Assign Visgroup..."), this);
    buttons->addWidget(groupButton);
    buttons->addWidget(ungroupButton);
    buttons->addWidget(visgroupButton);
    layout->addLayout(buttons);

    m_tabs = new QTabWidget(this);
    m_hierarchy = makeTree(m_tabs, {tr("Name"), tr("Class")});
    m_visgroups = makeTree(m_tabs, {tr("Visgroup / Object"), tr("Class")});
    m_tabs->addTab(m_hierarchy, tr("Hierarchy"));
    m_tabs->addTab(m_visgroups, tr("Visgroups"));
    layout->addWidget(m_tabs);

    connect(groupButton, &QPushButton::clicked, this, &OutlinerPanel::setSelectionGroup);
    connect(ungroupButton, &QPushButton::clicked, this, [this]
            {
        QVector<MapObject> changed;
        for (ObjectId id : m_ctx->doc()->selection())
            if (const MapObject *o = m_ctx->doc()->find(id); o && !o->groupPath.isEmpty()) {
                MapObject copy = *o;
                copy.groupPath.clear();
                changed.push_back(copy);
            }
        m_ctx->doc()->modify(changed, tr("Ungroup Objects")); });
    connect(visgroupButton, &QPushButton::clicked, this, &OutlinerPanel::setSelectionVisgroup);
    connect(m_filter, &QLineEdit::textChanged, this, [this] { rebuild(true); });

    connect(ctx->doc(), &MapDocument::objectsChanged, this, [this]
            { if (!m_updatingVisibility) rebuild(); });
    connect(ctx->doc(), &MapDocument::selectionChanged, this, &OutlinerPanel::syncSelectionFromDoc);
    for (QTreeWidget *tree : {m_hierarchy, m_visgroups})
    {
        connect(tree, &QTreeWidget::itemSelectionChanged, this, [this]
                { if (!m_syncing) updateSelection({m_hierarchy, m_visgroups}); });
        connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int)
                {
            if (item->data(0, kKindRole).toInt() != kObject)
                return;
            if (const MapObject *object = m_ctx->doc()->find(item->data(0, kIdRole).toInt()))
                m_ctx->focusOn(object->worldBounds()); });
        connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column)
                {
            if (m_syncing || column != 0) return;
            const int kind = item->data(0, kKindRole).toInt();
            const bool visible = item->checkState(0) == Qt::Checked;
            m_updatingVisibility = true;
            if (kind == kObject) {
                const MapObject *object = m_ctx->doc()->find(item->data(0, kIdRole).toInt());
                if (object && object->visible != visible) {
                    MapObject copy = *object;
                    copy.visible = visible;
                    m_ctx->doc()->modify({copy}, tr("Toggle Object Visibility"));
                }
            } else if (kind == kGroup) {
                m_ctx->doc()->setGroupVisible(item->data(0, kPathRole).toString(), visible);
            } else if (kind == kVisgroup) {
                m_ctx->doc()->setVisgroupVisible(item->data(0, kPathRole).toString(), visible);
            }
            m_updatingVisibility = false; });
    }
    rebuild(true);
}

void OutlinerPanel::setSelectionGroup()
{
    if (m_ctx->doc()->selection().isEmpty())
        return;
    bool accepted = false;
    const QString path = QInputDialog::getText(this, tr("Group Selection"), tr("Group path (use / for nested groups):"),
                                               QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted)
        return;
    QStringList parts;
    for (const QString &part : path.split('/', Qt::SkipEmptyParts))
        if (!part.trimmed().isEmpty())
            parts << part.trimmed();
    if (parts.isEmpty())
        return;
    const QString normalized = parts.join('/');
    QVector<MapObject> changed;
    for (ObjectId id : m_ctx->doc()->selection())
        if (const MapObject *o = m_ctx->doc()->find(id); o && o->groupPath != normalized)
        {
            MapObject copy = *o;
            copy.groupPath = normalized;
            changed.push_back(copy);
        }
    m_ctx->doc()->modify(changed, tr("Group Objects"));
}

void OutlinerPanel::setSelectionVisgroup()
{
    if (m_ctx->doc()->selection().isEmpty())
        return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Assign Visgroup"),
                                                tr("Visgroup name (leave empty to clear the assignment):"),
                                                QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted)
        return;
    QVector<MapObject> changed;
    for (ObjectId id : m_ctx->doc()->selection())
        if (const MapObject *o = m_ctx->doc()->find(id); o && o->visgroup != name)
        {
            MapObject copy = *o;
            copy.visgroup = name;
            changed.push_back(copy);
        }
    m_ctx->doc()->modify(changed, tr("Assign Visgroup"));
}

void OutlinerPanel::updateSelection(const QList<QTreeWidget *> &trees)
{
    QSet<ObjectId> selection;
    for (QTreeWidget *tree : trees)
        for (QTreeWidgetItem *item : tree->selectedItems())
            if (item->data(0, kKindRole).toInt() == kObject)
                selection.insert(item->data(0, kIdRole).toInt());
    m_syncing = true;
    m_ctx->doc()->setSelection(selection);
    m_syncing = false;
}

void OutlinerPanel::rebuild(bool force)
{
    quint64 sig = 1469598103934665603ULL;
    auto mix = [&sig](quint64 value) { sig = (sig ^ value) * 1099511628211ULL; };
    for (const auto &entry : m_ctx->doc()->objects())
    {
        const MapObject &object = entry.second;
        mix(quint64(object.id));
        mix(qHash(object.name));
        mix(qHash(object.classname));
        mix(qHash(object.groupPath));
        mix(qHash(object.visgroup));
        mix(object.visible);
    }
    for (const QString &name : m_ctx->doc()->visgroups())
    {
        mix(qHash(name));
        mix(m_ctx->doc()->visgroupVisible(name));
    }
    QSet<QString> groupPaths;
    for (const auto &entry : m_ctx->doc()->objects())
    {
        QString path;
        for (const QString &part : entry.second.groupPath.split('/', Qt::SkipEmptyParts))
        {
            path = path.isEmpty() ? part : path + '/' + part;
            groupPaths.insert(path);
        }
    }
    QStringList sortedGroupPaths = groupPaths.values();
    sortedGroupPaths.sort(Qt::CaseSensitive);
    for (const QString &path : sortedGroupPaths)
    {
        mix(qHash(path));
        mix(m_ctx->doc()->groupVisible(path));
    }
    if (!force && sig == m_signature)
        return;
    m_signature = sig;

    m_syncing = true;
    m_hierarchy->clear();
    m_visgroups->clear();
    QHash<QString, QTreeWidgetItem *> groupItems;
    QHash<QString, QTreeWidgetItem *> visgroupItems;
    const QString filter = m_filter->text().trimmed();

    for (const auto &entry : m_ctx->doc()->objects())
    {
        const MapObject &object = entry.second;
        const QString label = !object.name.isEmpty() ? object.name :
                              (object.classname.isEmpty() ? tr("block_%1").arg(object.id) : object.classname);
        if (!filter.isEmpty() && !label.contains(filter, Qt::CaseInsensitive) &&
            !object.classname.contains(filter, Qt::CaseInsensitive) &&
            !object.groupPath.contains(filter, Qt::CaseInsensitive) &&
            !object.visgroup.contains(filter, Qt::CaseInsensitive))
            continue;

        const int category = object.solid ? (object.classname.isEmpty() ? 0 : 1) : 2;
        const QString categoryName = category == 0 ? tr("Geometry") :
                                     category == 1 ? tr("Solid Entities") : tr("Point Entities");
        QString key = QString::number(category) + ':';
        QTreeWidgetItem *parent = nullptr;
        QStringList parts = object.groupPath.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty())
        {
            auto items = m_hierarchy->findItems(categoryName, Qt::MatchExactly, 0);
            parent = items.isEmpty() ? new QTreeWidgetItem(m_hierarchy, {categoryName}) : items.first();
        }
        else
        {
            auto items = m_hierarchy->findItems(categoryName, Qt::MatchExactly, 0);
            parent = items.isEmpty() ? new QTreeWidgetItem(m_hierarchy, {categoryName}) : items.first();
            QString groupPath;
            for (const QString &part : parts)
            {
                groupPath = groupPath.isEmpty() ? part : groupPath + '/' + part;
                key += part + '/';
                if (!groupItems.contains(key))
                {
                    auto *group = new QTreeWidgetItem(parent, {part});
                    group->setData(0, kKindRole, kGroup);
                    group->setData(0, kPathRole, groupPath);
                    group->setFlags((group->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsSelectable);
                    group->setCheckState(0, m_ctx->doc()->groupVisible(group->data(0, kPathRole).toString()) ?
                                               Qt::Checked : Qt::Unchecked);
                    groupItems.insert(key, group);
                }
                parent = groupItems.value(key);
            }
        }
        appendObject(parent, object, label);

        const QString visgroup = object.visgroup.isEmpty() ? tr("Unassigned") : object.visgroup;
        if (!visgroupItems.contains(visgroup))
        {
            auto *group = new QTreeWidgetItem(m_visgroups, {visgroup});
            group->setData(0, kKindRole, object.visgroup.isEmpty() ? 0 : kVisgroup);
            group->setData(0, kPathRole, object.visgroup);
            if (!object.visgroup.isEmpty())
            {
                group->setFlags((group->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsSelectable);
                group->setCheckState(0, m_ctx->doc()->visgroupVisible(object.visgroup) ? Qt::Checked : Qt::Unchecked);
            }
            else
            {
                group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
            }
            visgroupItems.insert(visgroup, group);
        }
        appendObject(visgroupItems.value(visgroup), object, label);
    }

    for (int i = 0; i < m_hierarchy->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *category = m_hierarchy->topLevelItem(i);
        category->setFlags(category->flags() & ~Qt::ItemIsSelectable);
        int objectCount = 0;
        QTreeWidgetItemIterator item(category);
        while (*item)
        {
            if ((*item)->data(0, kKindRole).toInt() == kObject)
                ++objectCount;
            ++item;
        }
        category->setText(0, QString("%1 (%2)").arg(category->text(0)).arg(objectCount));
        category->setExpanded(true);
    }
    for (int i = 0; i < m_visgroups->topLevelItemCount(); ++i)
        m_visgroups->topLevelItem(i)->setExpanded(true);
    m_hierarchy->expandAll();
    m_syncing = false;
    syncSelectionFromDoc();
}

void OutlinerPanel::syncSelectionFromDoc()
{
    if (m_syncing)
        return;
    m_syncing = true;
    const QSet<ObjectId> &selection = m_ctx->doc()->selection();
    for (QTreeWidget *tree : {m_hierarchy, m_visgroups})
    {
        tree->clearSelection();
        QTreeWidgetItemIterator it(tree);
        while (*it)
        {
            QTreeWidgetItem *item = *it;
            if (item->data(0, kKindRole).toInt() == kObject &&
                selection.contains(item->data(0, kIdRole).toInt()))
                item->setSelected(true);
            ++it;
        }
    }
    m_syncing = false;
}
