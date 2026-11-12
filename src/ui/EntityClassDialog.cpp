#include "ui/EntityClassDialog.h"
#include "core/EditorContext.h"
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace
{
    const QStringList kTypes = {"string", "integer", "float", "boolean", "color255", "vector", "angle",
                                "target_source", "target_destination", "studio", "material", "sound", "sprite"};
}

EntityClassDialog::EntityClassDialog(EditorContext *ctx, QWidget *parent) : QDialog(parent), m_ctx(ctx)
{
    setWindowTitle(tr("New Entity Class"));
    resize(720, 560);
    auto *lay = new QVBoxLayout(this);
    auto *f = new QFormLayout;
    lay->addLayout(f);

    m_name = new QLineEdit;
    m_name->setPlaceholderText("my_custom_entity");
    m_desc = new QLineEdit;
    m_kind = new QComboBox;
    m_kind->addItem(tr("Point entity (@PointClass)"), int(ClassKind::Point));
    m_kind->addItem(tr("Solid entity (@SolidClass)"), int(ClassKind::Solid));
    m_bases = new QLineEdit("Targetname");
    m_bases->setToolTip(tr("Comma-separated base classes whose properties/IO are inherited."));
    m_size = new QSpinBox;
    m_size->setRange(1, 1024);
    m_size->setValue(16);
    m_size->setToolTip(tr("Edge length of the editor cube for point entities."));
    m_color = new QPushButton(tr("Pick..."));
    auto paintColor = [this]
    { QPixmap pm(36, 14); pm.fill(m_colorValue); m_color->setIcon(QIcon(pm)); };
    paintColor();
    connect(m_color, &QPushButton::clicked, this, [this, paintColor]
            {
        const QColor c = QColorDialog::getColor(m_colorValue, this);
        if (c.isValid()) { m_colorValue = c; paintColor(); } });
    m_inputs = new QLineEdit;
    m_inputs->setPlaceholderText("Enable, Disable, Toggle");
    m_outputs = new QLineEdit;
    m_outputs->setPlaceholderText("OnTrigger, OnTimer");

    f->addRow(tr("Class name"), m_name);
    f->addRow(tr("Description"), m_desc);
    f->addRow(tr("Type"), m_kind);
    f->addRow(tr("Base classes"), m_bases);
    f->addRow(tr("Editor color"), m_color);
    f->addRow(tr("Box size"), m_size);
    f->addRow(tr("Inputs"), m_inputs);
    f->addRow(tr("Outputs"), m_outputs);

    lay->addWidget(new QLabel(tr("Properties")));
    m_props = new QTableWidget(0, 5);
    m_props->setHorizontalHeaderLabels({tr("Key"), tr("Type"), tr("Display name"), tr("Default"), tr("Help")});
    m_props->horizontalHeader()->setStretchLastSection(true);
    m_props->verticalHeader()->hide();
    lay->addWidget(m_props, 1);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add Property"));
    auto *rem = new QPushButton(tr("Remove"));
    row->addWidget(add);
    row->addWidget(rem);
    row->addStretch();
    lay->addLayout(row);
    connect(add, &QPushButton::clicked, this, [this]
            { addPropertyRow(); });
    connect(rem, &QPushButton::clicked, this, [this]
            { if (m_props->currentRow() >= 0) m_props->removeRow(m_props->currentRow()); });

    auto *note = new QLabel(tr("Saved to %1/user.dec and reloaded immediately. For choices/spawnflags, edit the .dec directly.").arg(ctx->userEntityDir()));
    note->setWordWrap(true);
    note->setStyleSheet("color:#9a9aa2;");
    lay->addWidget(note);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    addPropertyRow();
}

void EntityClassDialog::addPropertyRow(const QString &key, const QString &type, const QString &label, const QString &def, const QString &help)
{
    const int r = m_props->rowCount();
    m_props->insertRow(r);
    m_props->setItem(r, 0, new QTableWidgetItem(key));
    auto *t = new QComboBox;
    t->addItems(kTypes);
    t->setCurrentText(type);
    m_props->setCellWidget(r, 1, t);
    m_props->setItem(r, 2, new QTableWidgetItem(label));
    m_props->setItem(r, 3, new QTableWidgetItem(def));
    m_props->setItem(r, 4, new QTableWidgetItem(help));
}

void EntityClassDialog::accept()
{
    const QString name = m_name->text().trimmed();
    if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(name).hasMatch())
    {
        QMessageBox::warning(this, tr("New Entity Class"), tr("Class names may only contain letters, digits and underscores."));
        return;
    }
    if (m_ctx->registry()->find(name))
    {
        QMessageBox::warning(this, tr("New Entity Class"), tr("A class named '%1' already exists.").arg(name));
        return;
    }
    ClassDef d;
    d.kind = ClassKind(m_kind->currentData().toInt());
    d.name = name;
    d.description = m_desc->text();
    for (const QString &b : m_bases->text().split(',', Qt::SkipEmptyParts))
        d.bases << b.trimmed();
    d.hasColor = true;
    d.color = m_colorValue;
    if (d.kind == ClassKind::Point)
    {
        const float h = m_size->value() * 0.5f;
        d.hasSize = true;
        d.mins = {-h, -h, -h};
        d.maxs = {h, h, h};
    }
    for (int r = 0; r < m_props->rowCount(); ++r)
    {
        const QString key = m_props->item(r, 0) ? m_props->item(r, 0)->text().trimmed() : QString();
        if (key.isEmpty())
            continue;
        PropertyDef p;
        p.key = key;
        p.typeName = static_cast<QComboBox *>(m_props->cellWidget(r, 1))->currentText();
        const QString label = m_props->item(r, 2)->text();
        p.label = label.isEmpty() ? key : label;
        p.defaultValue = m_props->item(r, 3)->text();
        p.help = m_props->item(r, 4)->text();
        const QString t = p.typeName;
        p.type = t == "integer" ? PropType::Integer : t == "float"  ? PropType::Float
                                                  : t == "boolean"  ? PropType::Boolean
                                                  : t == "color255" ? PropType::Color
                                                  : t == "vector"   ? PropType::Vector
                                                  : t == "angle"    ? PropType::Angles
                                                                    : PropType::String;
        d.props.push_back(p);
    }
    for (const QString &n : m_inputs->text().split(',', Qt::SkipEmptyParts))
        d.inputs.push_back({n.trimmed(), "void", {}});
    for (const QString &n : m_outputs->text().split(',', Qt::SkipEmptyParts))
        d.outputs.push_back({n.trimmed(), "void", {}});

    QDir().mkpath(m_ctx->userEntityDir());
    QFile f(m_ctx->userEntityDir() + "/user.dec");
    if (!f.open(QIODevice::Append | QIODevice::Text))
    {
        QMessageBox::critical(this, tr("New Entity Class"), tr("Cannot write %1").arg(f.fileName()));
        return;
    }
    QTextStream(&f) << "\n"
                    << EntityRegistry::toDec(d);
    f.close();
    m_ctx->log(tr("Declared entity class '%1' in %2").arg(name, f.fileName()));
    m_ctx->registry()->reloadAll();
    if (d.kind == ClassKind::Point)
        m_ctx->setEntityClass(name);
    QDialog::accept();
}
