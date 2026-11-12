#pragma once
#include <QColor>
#include <QDialog>

class EditorContext;
class QLineEdit;
class QComboBox;
class QPushButton;
class QSpinBox;
class QTableWidget;

class EntityClassDialog : public QDialog {
    Q_OBJECT
public:
    explicit EntityClassDialog(EditorContext* ctx, QWidget* parent = nullptr);

private:
    void addPropertyRow(const QString& key = {}, const QString& type = "string", const QString& label = {},
                        const QString& def = {}, const QString& help = {});
    void accept() override;

    EditorContext* m_ctx;
    QLineEdit* m_name;
    QLineEdit* m_desc;
    QComboBox* m_kind;
    QPushButton* m_color;
    QSpinBox* m_size;
    QLineEdit* m_bases;
    QLineEdit* m_outputs;
    QLineEdit* m_inputs;
    QTableWidget* m_props;
    QColor m_colorValue{80, 160, 255};
};
