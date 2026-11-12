#include "ui/PropertiesPanel.h"
#include "core/EditorContext.h"
#include "ui/AssetDrag.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QPair>
#include <QSet>
#include <QSharedPointer>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

namespace
{
    QString formatCoord(float v)
    {
        QString s = QLocale::c().toString(double(v), 'f', 2);
        if (s.contains('.'))
        {
            while (s.endsWith('0'))
                s.chop(1);
            if (s.endsWith('.'))
                s.chop(1);
        }
        return s == "-0" ? QString("0") : s;
    }

    bool parseCoord(QString text, float &out)
    {
        bool ok = false;
        const double v = QLocale::c().toDouble(text.trimmed().replace(',', '.'), &ok);
        if (ok)
            out = float(v);
        return ok;
    }
}

namespace
{
    class AssetLineEdit : public QLineEdit
    {
    public:
        AssetLineEdit(const QString &text, QString contentDir)
            : QLineEdit(text), m_contentDir(std::move(contentDir)) {}
        void setOnDrop(std::function<void()> fn) { m_onDrop = std::move(fn); }

    protected:
        void dragEnterEvent(QDragEnterEvent *e) override
        {
            if (!isReadOnly() && !draggedAssetPath(e->mimeData(), m_contentDir).isEmpty())
            {
                e->setDropAction(Qt::CopyAction);
                e->accept();
            }
            else
                QLineEdit::dragEnterEvent(e);
        }
        void dragMoveEvent(QDragMoveEvent *e) override
        {
            if (!isReadOnly() && !draggedAssetPath(e->mimeData(), m_contentDir).isEmpty())
            {
                e->setDropAction(Qt::CopyAction);
                e->accept();
            }
            else
                QLineEdit::dragMoveEvent(e);
        }
        void dropEvent(QDropEvent *e) override
        {
            const QString path = isReadOnly() ? QString() : draggedAssetPath(e->mimeData(), m_contentDir);
            if (path.isEmpty())
            {
                QLineEdit::dropEvent(e);
                return;
            }
            e->setDropAction(Qt::CopyAction);
            e->accept();
            setText(path);
            if (m_onDrop)
                m_onDrop();
        }

    private:
        QString m_contentDir;
        std::function<void()> m_onDrop;
    };
}

OutputsEditor::OutputsEditor(EditorContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({tr("My Output"), tr("Target Entity"), tr("Target Input"), tr("Parameter"), tr("Delay"), tr("Once")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    for (int c = 0; c < 4; ++c)
        m_table->setColumnWidth(c, 110);
    m_table->setColumnWidth(4, 60);
    m_table->setColumnWidth(5, 40);
    m_add = new QPushButton(tr("Add"), this);
    m_remove = new QPushButton(tr("Delete"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_add);
    buttons->addWidget(m_remove);
    buttons->addStretch();
    lay->addWidget(m_hint);
    lay->addWidget(m_table, 1);
    lay->addLayout(buttons);

    connect(m_add, &QPushButton::clicked, this, [this]
            {
        if (!m_id) return;
        Connection c;
        if (const MapObject* o = m_ctx->doc()->find(m_id))
            if (const ClassDef* d = m_ctx->registry()->resolved(o->classname))
                if (!d->outputs.isEmpty()) c.output = d->outputs.first().name;
        m_loading = true;
        addRow(c);
        m_loading = false;
        commit(); });
    connect(m_remove, &QPushButton::clicked, this, [this]
            {
        const int r = m_table->currentRow();
        if (r < 0) return;
        m_table->removeRow(r);
        commit(); });
    reload();
}

void OutputsEditor::fillInputs(QComboBox *box, const QUuid &target)
{
    const QString keep = box->currentText();
    QSignalBlocker b(box);
    box->clear();
    if (const MapObject *o = m_ctx->doc()->findByGuid(target))
        if (const ClassDef *d = m_ctx->registry()->resolved(o->classname))
            for (const IODef &io : d->inputs)
                box->addItem(io.name);
    box->setCurrentText(keep);
}

void OutputsEditor::fillTargets(QComboBox *box, const Connection &c)
{
    QSignalBlocker b(box);
    box->clear();
    // Only entities take inputs; world geometry has no class.
    QVector<const MapObject *> targets;
    for (const auto &kvp : m_ctx->doc()->objects())
        if (!kvp.second.classname.isEmpty())
            targets.push_back(&kvp.second);
    std::sort(targets.begin(), targets.end(), [](const MapObject *a, const MapObject *b)
              { return a->displayName().compare(b->displayName(), Qt::CaseInsensitive) < 0; });
    box->addItem(tr("(none)"), QString());
    for (const MapObject *o : targets)
    {
        box->addItem(o->name.isEmpty() ? o->displayName() : QString("%1  (%2)").arg(o->name, o->classname),
                     o->guid.toString(QUuid::WithoutBraces));
        box->setItemData(box->count() - 1, o->guid.toString(QUuid::WithoutBraces), Qt::ToolTipRole);
    }
    int index = c.target.isNull() ? 0 : box->findData(c.target.toString(QUuid::WithoutBraces));
    if (index < 0 || (c.target.isNull() && !c.legacyTarget.isEmpty()))
    {
        // Keep links to deleted objects (or unresolved names) visible instead of dropping them.
        const QString missing = c.target.isNull() ? c.legacyTarget : c.target.toString(QUuid::WithoutBraces);
        box->addItem(tr("(missing) %1").arg(missing), c.target.isNull() ? QString() : missing);
        index = box->count() - 1;
    }
    box->setCurrentIndex(index);
}

void OutputsEditor::addRow(const Connection &c)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto *out = new QComboBox;
    out->setEditable(true);
    if (const MapObject *o = m_ctx->doc()->find(m_id))
        if (const ClassDef *d = m_ctx->registry()->resolved(o->classname))
            for (const IODef &io : d->outputs)
                out->addItem(io.name);
    out->setCurrentText(c.output);

    auto *tgt = new QComboBox;
    fillTargets(tgt, c);
    tgt->setProperty("legacyTarget", c.target.isNull() ? c.legacyTarget : QString());

    auto *inp = new QComboBox;
    inp->setEditable(true);
    inp->setCurrentText(c.input);
    fillInputs(inp, c.target);
    inp->setCurrentText(c.input);

    auto *par = new QLineEdit(c.param);
    auto *dly = new QDoubleSpinBox;
    dly->setRange(0, 9999);
    dly->setDecimals(2);
    dly->setValue(c.delay);
    auto *once = new QCheckBox;
    once->setChecked(c.once);

    m_table->setCellWidget(row, 0, out);
    m_table->setCellWidget(row, 1, tgt);
    m_table->setCellWidget(row, 2, inp);
    m_table->setCellWidget(row, 3, par);
    m_table->setCellWidget(row, 4, dly);
    m_table->setCellWidget(row, 5, once);

    connect(out, &QComboBox::currentTextChanged, this, [this]
            { commit(); });
    connect(tgt, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, tgt, inp]
            {
        tgt->setProperty("legacyTarget", QString());
        fillInputs(inp, QUuid::fromString(tgt->currentData().toString()));
        commit(); });
    connect(inp, &QComboBox::currentTextChanged, this, [this]
            { commit(); });
    connect(par, &QLineEdit::textChanged, this, [this]
            { commit(); });
    connect(dly, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]
            { commit(); });
    connect(once, &QCheckBox::toggled, this, [this]
            { commit(); });
}

QList<Connection> OutputsEditor::readRows() const
{
    QList<Connection> list;
    for (int r = 0; r < m_table->rowCount(); ++r)
    {
        Connection c;
        c.output = static_cast<QComboBox *>(m_table->cellWidget(r, 0))->currentText();
        const auto *target = static_cast<QComboBox *>(m_table->cellWidget(r, 1));
        c.target = QUuid::fromString(target->currentData().toString());
        c.legacyTarget = target->property("legacyTarget").toString();
        c.input = static_cast<QComboBox *>(m_table->cellWidget(r, 2))->currentText();
        c.param = static_cast<QLineEdit *>(m_table->cellWidget(r, 3))->text();
        c.delay = float(static_cast<QDoubleSpinBox *>(m_table->cellWidget(r, 4))->value());
        c.once = static_cast<QCheckBox *>(m_table->cellWidget(r, 5))->isChecked();
        list.push_back(c);
    }
    return list;
}

void OutputsEditor::commit()
{
    if (m_loading || !m_id)
        return;
    const MapObject *o = m_ctx->doc()->find(m_id);
    if (!o)
        return;
    MapObject c = *o;
    c.outputs = readRows();
    m_committing = true;
    m_ctx->doc()->modify({c}, tr("Edit Outputs"), "outputs");
    m_committing = false;
}

void OutputsEditor::reload()
{
    if (m_committing)
        return;
    const auto sel = m_ctx->doc()->selectionList();
    const MapObject *o = sel.size() == 1 ? m_ctx->doc()->find(sel.first()) : nullptr;
    if (o && o->id == m_id && readRows() == o->outputs)
    {
        // Same links, but other objects may have been added, renamed or deleted.
        const QList<Connection> rows = readRows();
        for (int r = 0; r < rows.size(); ++r)
            fillTargets(static_cast<QComboBox *>(m_table->cellWidget(r, 1)), rows[r]);
        return;
    }
    m_loading = true;
    m_table->setRowCount(0);
    m_id = o ? o->id : 0;
    if (o)
        for (const Connection &c : o->outputs)
            addRow(c);
    m_loading = false;
    m_add->setEnabled(o != nullptr);
    m_remove->setEnabled(o != nullptr);
    m_hint->setText(o ? tr("MapScript: outputs fire when something happens to this entity and send an input to the target.")
                      : (sel.isEmpty() ? tr("Nothing selected.") : tr("Select a single entity to edit its outputs.")));
}

PropertiesPanel::PropertiesPanel(EditorContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget(this);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_outputs = new OutputsEditor(ctx, this);
    m_tabs->addTab(m_scroll, tr("Properties"));
    m_tabs->addTab(m_outputs, tr("Outputs"));
    lay->addWidget(m_tabs);

    connect(ctx->doc(), &MapDocument::selectionChanged, this, [this]
            { rebuild(); });
    connect(ctx->doc(), &MapDocument::objectsChanged, this, [this]
            { if (!m_editing) refresh(); });
    connect(ctx->registry(), &EntityRegistry::changed, this, [this]
            { rebuild(); });
    rebuild();
}

void PropertiesPanel::editSelected(const std::function<void(MapObject &)> &fn, const QString &text, const QString &mergeKey)
{
    if (m_building)
        return;
    QVector<MapObject> after;
    for (ObjectId id : m_ctx->doc()->selectionList())
    {
        if (const MapObject *o = m_ctx->doc()->find(id))
        {
            MapObject c = *o;
            fn(c);
            after.push_back(c);
        }
    }
    if (after.isEmpty())
        return;
    m_editing = true;
    m_ctx->doc()->modify(after, text, mergeKey);
    m_editing = false;
}

void PropertiesPanel::setKeyValue(const QString &key, const QString &value)
{
    editSelected([&](MapObject &c)
                 { c.kv[key] = value; }, tr("Edit %1").arg(key), "kv:" + key);
}

QString PropertiesPanel::currentValue(const QString &key, const QString &def) const
{
    const auto sel = m_ctx->doc()->selectionList();
    if (sel.isEmpty())
        return def;
    const MapObject *o = m_ctx->doc()->find(sel.first());
    return o ? o->kv.value(key, def) : def;
}

void PropertiesPanel::refresh()
{
    if (m_building)
        return;
    const auto sel = m_ctx->doc()->selectionList();
    if (sel.isEmpty())
        return;
    const MapObject *o = m_ctx->doc()->find(sel.first());
    if (!o)
        return;
    if (o->classname != m_builtClass)
    {
        rebuild();
        return;
    }

    m_building = true;
    if (m_name && !m_name->hasFocus())
    {
        QSignalBlocker b(m_name);
        m_name->setText(o->name);
    }
    for (int i = 0; i < 3; ++i)
    {
        if (m_origin[i] && !m_origin[i]->hasFocus())
        {
            QSignalBlocker b(m_origin[i]);
            m_origin[i]->setText(formatCoord(o->origin[i]));
        }
        if (m_angles[i] && !m_angles[i]->hasFocus())
        {
            QSignalBlocker b(m_angles[i]);
            m_angles[i]->setText(formatCoord(o->angles[i]));
        }
    }
    for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it)
        it->set(o->kv.value(it.key(), it->def));
    m_building = false;
    m_outputs->reload();
}

QWidget *PropertiesPanel::makeEditor(const PropertyDef &p, const MapObject &o)
{
    const QString key = p.key;
    const QString def = p.defaultValue;
    const QString val = o.kv.value(key, def);
    QWidget *result = nullptr;

    auto bind = [this, key, def](std::function<void(const QString &)> fn)
    { m_bindings[key] = {std::move(fn), def}; };
    auto asBool = [](const QString &s)
    { return s == "1" || s.compare("true", Qt::CaseInsensitive) == 0; };

    switch (p.type)
    {
    case PropType::Boolean:
    {
        auto *w = new QCheckBox;
        w->setChecked(asBool(val));
        connect(w, &QCheckBox::toggled, this, [this, key](bool on)
                { setKeyValue(key, on ? "1" : "0"); });
        bind([w, asBool](const QString &v)
             { QSignalBlocker b(w); w->setChecked(asBool(v)); });
        result = w;
        break;
    }
    case PropType::Integer:
    {
        auto *w = new QSpinBox;
        w->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        w->setValue(val.toInt());
        connect(w, qOverload<int>(&QSpinBox::valueChanged), this, [this, key](int v)
                { setKeyValue(key, QString::number(v)); });
        bind([w](const QString &v)
             { QSignalBlocker b(w); w->setValue(v.toInt()); });
        result = w;
        break;
    }
    case PropType::Float:
    {
        auto *w = new QDoubleSpinBox;
        w->setRange(-1e9, 1e9);
        w->setDecimals(3);
        w->setValue(val.toDouble());
        connect(w, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, key](double v)
                { setKeyValue(key, QString::number(v, 'g', 9)); });
        bind([w](const QString &v)
             { QSignalBlocker b(w); w->setValue(v.toDouble()); });
        result = w;
        break;
    }
    case PropType::Choices:
    {
        auto *w = new QComboBox;
        for (const ChoiceItem &c : p.choices)
            w->addItem(c.label, c.value);
        auto select = [w](const QString &v)
        {
            int i = w->findData(v);
            if (i < 0)
            {
                w->addItem(v, v);
                i = w->count() - 1;
            }
            w->setCurrentIndex(i);
        };
        select(val);
        connect(w, qOverload<int>(&QComboBox::activated), this, [this, key, w](int i)
                { setKeyValue(key, w->itemData(i).toString()); });
        bind([w, select](const QString &v)
             { QSignalBlocker b(w); select(v); });
        result = w;
        break;
    }
    case PropType::Flags:
    {
        auto *box = new QWidget;
        auto *v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        auto checks = QSharedPointer<QVector<QPair<int, QCheckBox *>>>::create();
        const int cur = val.toInt();
        for (const FlagItem &f : p.flags)
        {
            auto *cb = new QCheckBox(f.label);
            cb->setChecked(cur & f.bit);
            const int bit = f.bit;
            connect(cb, &QCheckBox::toggled, this, [this, key, def, bit](bool on)
                    { editSelected([&](MapObject &c)
                                   {
                    int fl = c.kv.value(key, def).toInt();
                    fl = on ? (fl | bit) : (fl & ~bit);
                    c.kv[key] = QString::number(fl); }, tr("Edit Flags"), "kv:" + key); });
            v->addWidget(cb);
            checks->push_back({bit, cb});
        }
        bind([checks](const QString &s)
             {
            const int fl = s.toInt();
            for (auto& c : *checks) { QSignalBlocker b(c.second); c.second->setChecked(fl & c.first); } });
        result = box;
        break;
    }
    case PropType::Color:
    {
        auto *w = new QPushButton;
        auto parse = [](const QString &s)
        {
            const QStringList l = s.split(' ', Qt::SkipEmptyParts);
            return QColor(l.value(0).toInt(), l.value(1).toInt(), l.value(2).toInt());
        };
        auto paint = [w, parse](const QString &s)
        {
            QPixmap pm(36, 14);
            pm.fill(parse(s));
            w->setIcon(QIcon(pm));
            w->setText(s);
        };
        paint(val);
        connect(w, &QPushButton::clicked, this, [this, key, def, parse, paint, w]
                {
            const QColor c = QColorDialog::getColor(parse(currentValue(key, def)), this, tr("Pick Color"));
            if (!c.isValid()) return;
            const QString s = QString("%1 %2 %3").arg(c.red()).arg(c.green()).arg(c.blue());
            paint(s);
            setKeyValue(key, s); });
        bind([paint](const QString &v)
             { paint(v); });
        result = w;
        break;
    }
    case PropType::TargetDest:
    {
        auto *w = new QComboBox;
        w->setEditable(true);
        QSet<QString> names;
        for (const auto &kvp : m_ctx->doc()->objects())
            if (!kvp.second.name.isEmpty())
                names.insert(kvp.second.name);
        QStringList sorted = names.values();
        sorted.sort();
        w->addItems(sorted);
        w->setCurrentText(val);
        connect(w, qOverload<int>(&QComboBox::activated), this, [this, key, w]
                { setKeyValue(key, w->currentText()); });
        connect(w->lineEdit(), &QLineEdit::editingFinished, this, [this, key, w]
                { setKeyValue(key, w->currentText()); });
        bind([w](const QString &v)
             { QSignalBlocker b(w); w->setCurrentText(v); });
        result = w;
        break;
    }
    case PropType::Asset:
    {
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto *e = new AssetLineEdit(val, m_ctx->projectDir());
        auto *b = new QToolButton;
        b->setText("...");
        b->setToolTip(tr("Browse, or drag an asset here from the Asset Browser"));
        h->addWidget(e, 1);
        h->addWidget(b);
        e->setReadOnly(p.readOnly);
        connect(e, &QLineEdit::editingFinished, this, [this, key, e]
                { setKeyValue(key, e->text()); });
        e->setOnDrop([this, key, e]
                     { setKeyValue(key, e->text()); });
        connect(b, &QToolButton::clicked, this, [this, key, e]
                {
            const QString f = QFileDialog::getOpenFileName(this, tr("Select Asset"), m_ctx->projectDir());
            if (f.isEmpty()) return;
            const QString rel = QDir(m_ctx->projectDir()).relativeFilePath(f);
            e->setText(rel.startsWith("..") ? f : rel);
            setKeyValue(key, e->text()); });
        bind([e](const QString &v)
             { QSignalBlocker bl(e); e->setText(v); });
        result = row;
        break;
    }
    default:
    {
        auto *w = new QLineEdit(val);
        w->setReadOnly(p.readOnly);
        connect(w, &QLineEdit::editingFinished, this, [this, key, w]
                { setKeyValue(key, w->text()); });
        bind([w](const QString &v)
             { if (!w->hasFocus()) { QSignalBlocker b(w); w->setText(v); } });
        result = w;
        break;
    }
    }
    result->setToolTip(p.help.isEmpty() ? QString("%1 (%2)").arg(p.key, p.typeName) : p.help);
    return result;
}

void PropertiesPanel::rebuild()
{
    m_building = true;
    m_bindings.clear();
    m_class = nullptr;
    m_name = nullptr;
    for (int i = 0; i < 3; ++i)
        m_origin[i] = m_angles[i] = nullptr;

    if (QWidget *old = m_scroll->takeWidget())
        old->deleteLater();
    auto *form = new QWidget;
    auto *lay = new QVBoxLayout(form);
    lay->setContentsMargins(8, 8, 8, 8);

    const auto sel = m_ctx->doc()->selectionList();
    const MapObject *o = sel.isEmpty() ? nullptr : m_ctx->doc()->find(sel.first());
    if (!o)
    {
        m_builtClass.clear();
        lay->addWidget(new QLabel(tr("Nothing selected.")));
        lay->addStretch();
        m_scroll->setWidget(form);
        m_building = false;
        m_outputs->reload();
        return;
    }
    m_builtClass = o->classname;

    const QString title = !o->name.isEmpty() ? o->name : (!o->classname.isEmpty() ? o->classname : tr("Block"));
    auto *header = new QLabel(sel.size() == 1 ? QString("<b>%1</b>").arg(title.toHtmlEscaped())
                                              : tr("<b>%1 objects selected</b>").arg(sel.size()));
    lay->addWidget(header);

    auto *f = new QFormLayout;
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    lay->addLayout(f);

    m_class = new QComboBox;
    m_class->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_class->setMinimumContentsLength(8);
    const ClassKind kind = o->solid ? ClassKind::Solid : ClassKind::Point;
    if (o->solid)
        m_class->addItem(tr("(static geometry)"), QString());
    for (const ClassDef *c : m_ctx->registry()->classes(kind))
        m_class->addItem(c->name, c->name);
    int ci = m_class->findData(o->classname);
    if (ci < 0)
    {
        m_class->addItem(o->classname + tr(" (undefined)"), o->classname);
        ci = m_class->count() - 1;
    }
    m_class->setCurrentIndex(ci);
    f->addRow(tr("Class"), m_class);
    connect(m_class, qOverload<int>(&QComboBox::activated), this, [this](int i)
            {
        const QString cls = m_class->itemData(i).toString();
        const ClassDef* def = cls.isEmpty() ? nullptr : m_ctx->registry()->resolved(cls);
        editSelected([&](MapObject& c) { if (def) applyClassDefaults(c, *def); else c.classname.clear(); }, tr("Change Class"), QString());
        QTimer::singleShot(0, this, [this] { rebuild(); }); });

    m_name = new QLineEdit(o->name);
    m_name->setEnabled(sel.size() == 1);
    f->addRow(tr("Name"), m_name);
    connect(m_name, &QLineEdit::editingFinished, this, [this]
            {
        const QString n = m_name->text();
        editSelected([&](MapObject& c) { c.name = n; }, tr("Rename"), "name"); });

    // Applied on Enter / focus out. With several objects selected the first one's value is shown,
    // and the change is applied to all of them as an offset so they keep their layout.
    auto makeVec = [this](QLineEdit **arr, const QVector3D &v, bool angles)
    {
        static const char *axes[3] = {"X", "Y", "Z"};
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(3);
        for (int i = 0; i < 3; ++i)
        {
            auto *e = new QLineEdit(formatCoord(v[i]));
            e->setPlaceholderText(axes[i]);
            e->setToolTip(QString("%1 %2").arg(angles ? tr("Angle") : tr("Position"), axes[i]));
            e->setMinimumWidth(30);
            arr[i] = e;
            h->addWidget(e, 1);
            connect(e, &QLineEdit::editingFinished, this, [this, e, i, angles]
                    {
                const auto sel = m_ctx->doc()->selectionList();
                const MapObject *first = sel.isEmpty() ? nullptr : m_ctx->doc()->find(sel.first());
                float value = 0.0f;
                if (!first || !parseCoord(e->text(), value))
                {
                    if (first)
                        e->setText(formatCoord((angles ? first->angles : first->origin)[i]));
                    return;
                }
                const float delta = value - (angles ? first->angles : first->origin)[i];
                if (delta == 0.0f)
                {
                    e->setText(formatCoord(value));
                    return;
                }
                editSelected([&](MapObject &c)
                             { (angles ? c.angles : c.origin)[i] += delta; },
                             angles ? tr("Change Angles") : tr("Move Objects"), QString());
                e->setText(formatCoord(value)); });
        }
        return row;
    };
    f->addRow(o->solid ? tr("Position") : tr("Origin"), makeVec(m_origin, o->origin, false));
    if (!o->solid)
        f->addRow(tr("Angles"), makeVec(m_angles, o->angles, true));

    const ClassDef *def = o->classname.isEmpty() ? nullptr : m_ctx->registry()->resolved(o->classname);
    if (def)
    {
        auto *desc = new QLabel(def->description);
        desc->setWordWrap(true);
        desc->setStyleSheet("color:#9a9aa2;");
        lay->insertWidget(1, desc);
        for (const PropertyDef &p : def->props)
        {
            if (isBuiltinKey(p.key))
                continue;
            f->addRow(p.type == PropType::Flags ? tr("Spawn Flags") : p.label, makeEditor(p, *o));
        }
    }
    else if (!o->classname.isEmpty())
    {
        auto *warn = new QLabel(tr("No definition found for class '%1'.\nAdd it to an .dec file (Tools > New Entity Class...).").arg(o->classname));
        warn->setStyleSheet("color:#f0c040;");
        warn->setWordWrap(true);
        lay->addWidget(warn);
    }
    lay->addStretch();
    m_scroll->setWidget(form);
    m_building = false;
    m_outputs->reload();
}
