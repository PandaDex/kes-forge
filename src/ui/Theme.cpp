#include "ui/Theme.h"
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>
#include <QStyleFactory>

namespace Theme
{

    const char *kAccent = "#f0963a";

    void apply(QApplication &app)
    {
        app.setStyle(QStyleFactory::create("Fusion"));
        QPalette p;
        const QColor window(45, 45, 48), base(30, 30, 33), alt(38, 38, 41), text(220, 220, 224), accent(240, 150, 58);
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, alt);
        p.setColor(QPalette::ToolTipBase, QColor(25, 25, 28));
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, QColor(58, 58, 62));
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, Qt::white);
        p.setColor(QPalette::Link, accent);
        p.setColor(QPalette::Highlight, QColor(200, 115, 30));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, QColor(130, 130, 136));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(110, 110, 115));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(110, 110, 115));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(110, 110, 115));
        app.setPalette(p);

        app.setStyleSheet(QString(R"(
        * { outline: 0; }
        QWidget { font-size: 9pt; }
        QToolTip { background:#19191c; color:#e4e4e8; border:1px solid %1; padding:4px 6px; border-radius:3px; }
        QMainWindow::separator { background:#1a1a1d; width:4px; height:4px; }
        QMainWindow::separator:hover { background:%1; }
        QDockWidget { titlebar-close-icon:none; font-weight:600; color:#cfcfd4; }
        QDockWidget::title { background:#242427; padding:6px 10px; border-bottom:1px solid #161618; text-align:left; }
        QTabWidget::pane { border:1px solid #1d1d20; top:-1px; }
        QTabBar::tab { background:#29292c; color:#a8a8ae; padding:6px 14px; border:1px solid #1d1d20; border-bottom:none; margin-right:1px; }
        QTabBar::tab:selected { background:#38383c; color:#f0f0f2; border-top:2px solid %1; }
        QTabBar::tab:hover:!selected { background:#323236; color:#dcdce0; }
        QToolBar { background:#29292c; border:none; spacing:4px; padding:4px; }
        QToolBar::separator { background:#3a3a3f; width:1px; height:1px; margin:4px 6px; }
        QToolBar#toolStrip { border-right:1px solid #1a1a1d; padding:6px 4px; spacing:6px; }
        QToolButton { border:1px solid transparent; border-radius:4px; padding:4px 6px; color:#dcdce0; }
        QToolButton:hover { background:#3b3b40; border-color:#525258; }
        QToolButton:pressed { background:#2f2f33; }
        QToolButton:checked { background:#4a3624; border-color:%1; color:#ffe2c4; }
        QToolButton:disabled { color:#6a6a70; }
        QMenuBar { background:#29292c; padding:2px; }
        QMenuBar::item { padding:4px 10px; border-radius:3px; }
        QMenuBar::item:selected { background:#3d3d42; }
        QMenu { background:#2b2b2e; border:1px solid #18181a; padding:4px; }
        QMenu::item { padding:5px 28px 5px 22px; border-radius:3px; }
        QMenu::item:selected { background:#b8681d; color:white; }
        QMenu::item:disabled { color:#6a6a70; }
        QMenu::separator { height:1px; background:#3a3a3f; margin:4px 8px; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit { background:#1d1d20; border:1px solid #434348; border-radius:3px; padding:3px 5px; selection-background-color:#b8681d; }
        QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover { border-color:#5a5a60; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border-color:%1; }
        QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color:#6a6a70; background:#232326; }
        QComboBox::drop-down { border:none; width:18px; }
        QComboBox QAbstractItemView { background:#252528; border:1px solid #18181a; selection-background-color:#b8681d; padding:2px; }
        QPushButton { background:#38383c; border:1px solid #4a4a50; border-radius:4px; padding:5px 14px; color:#e4e4e8; }
        QPushButton:hover { background:#404045; border-color:%1; }
        QPushButton:pressed { background:#2f2f33; }
        QPushButton:default { border-color:%1; }
        QPushButton:disabled { color:#6a6a70; border-color:#3a3a3f; }
        QCheckBox, QRadioButton { spacing:6px; }
        QCheckBox::indicator, QRadioButton::indicator { width:13px; height:13px; border:1px solid #55555b; background:#1d1d20; }
        QCheckBox::indicator { border-radius:3px; }
        QRadioButton::indicator { border-radius:7px; }
        QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color:%1; }
        QCheckBox::indicator:checked, QRadioButton::indicator:checked { background:%1; border-color:%1; }
        QHeaderView::section { background:#29292c; color:#b8b8be; border:none; border-right:1px solid #1d1d20; border-bottom:1px solid #1d1d20; padding:4px 6px; }
        QTreeView, QListView, QTableWidget, QTableView { background:#1d1d20; alternate-background-color:#222225; border:1px solid #1a1a1d; selection-background-color:#7a4a1c; selection-color:white; }
        QTreeView::item, QListView::item { padding:2px 0; }
        QTreeView::item:hover, QListView::item:hover { background:#2c2c30; }
        QTreeView::item:selected, QListView::item:selected { background:#8a5420; color:white; }
        QScrollArea { border:none; }
        QScrollBar:vertical { background:#1d1d20; width:11px; margin:0; }
        QScrollBar:horizontal { background:#1d1d20; height:11px; margin:0; }
        QScrollBar::handle:vertical { background:#46464c; min-height:24px; border-radius:4px; margin:2px; }
        QScrollBar::handle:horizontal { background:#46464c; min-width:24px; border-radius:4px; margin:2px; }
        QScrollBar::handle:hover { background:#5e5e66; }
        QScrollBar::add-line, QScrollBar::sub-line { width:0; height:0; }
        QScrollBar::add-page, QScrollBar::sub-page { background:none; }
        QStatusBar { background:#232326; color:#b8b8be; border-top:1px solid #18181a; }
        QStatusBar::item { border:none; }
        QStatusBar QLabel { padding:0 8px; border-left:1px solid #34343a; }
        QSplitter::handle { background:#161618; }
        QSplitter::handle:hover { background:%1; }
        QLabel#hint { color:#9a9aa2; }
        QDialog { background:#2d2d30; }
    )")
                              .arg(kAccent));
    }

    QIcon toolIcon(const QString &id)
    {
        QIcon icon;
        for (int state = 0; state < 2; ++state)
        {
            QPixmap pm(48, 48);
            pm.setDevicePixelRatio(2.0);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const QColor c = state ? QColor(kAccent) : QColor(210, 210, 215);
            p.setPen(QPen(c, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            if (id == "select")
            {
                p.setBrush(c.darker(300));
                p.drawPolygon(QPolygonF({{6, 3}, {6, 19}, {10.5, 15}, {14, 21}, {16.5, 19.5}, {13, 14}, {19, 13.5}}));
            }
            else if (id == "block")
            {
                p.drawRect(QRectF(4, 8, 12, 12));
                p.drawLine(QPointF(4, 8), QPointF(9, 3));
                p.drawLine(QPointF(16, 8), QPointF(21, 3));
                p.drawLine(QPointF(16, 20), QPointF(21, 15));
                p.drawLine(QPointF(9, 3), QPointF(21, 3));
                p.drawLine(QPointF(21, 3), QPointF(21, 15));
            }
            else if (id == "entity")
            {
                p.setBrush(c.darker(300));
                p.drawEllipse(QPointF(12, 12), 4.2, 4.2);
                p.drawLine(QPointF(12, 2), QPointF(12, 6));
                p.drawLine(QPointF(12, 18), QPointF(12, 22));
                p.drawLine(QPointF(2, 12), QPointF(6, 12));
                p.drawLine(QPointF(18, 12), QPointF(22, 12));
            }
            else if (id == "clip")
            {
                p.drawPolygon(QPolygonF({{3, 20}, {3, 9}, {11, 5}, {11, 20}}));
                p.drawPolygon(QPolygonF({{14, 20}, {14, 5}, {21, 9}, {21, 20}}));
                QPen dash(c, 1.4, Qt::DashLine, Qt::RoundCap);
                p.setPen(dash);
                p.drawLine(QPointF(12.5, 1.5), QPointF(12.5, 23));
            }
            else if (id == "vertex")
            {
                const QPolygonF poly({{4, 19}, {6, 6}, {18, 4}, {20, 17}});
                p.drawPolygon(poly);
                p.setBrush(c);
                for (const QPointF &pt : poly)
                    p.drawRect(QRectF(pt.x() - 2, pt.y() - 2, 4, 4));
            }
            else
            {
                p.drawRect(QRectF(5, 5, 14, 14));
            }
            icon.addPixmap(pm, QIcon::Normal, state ? QIcon::On : QIcon::Off);
        }
        return icon;
    }

}
