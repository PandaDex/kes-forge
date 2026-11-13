#include "ui/Theme.h"
#include "ui/KesStyle.h"
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>

namespace Theme
{

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
            const QColor c = state ? kes::style::accent() : QColor(210, 210, 215);
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
