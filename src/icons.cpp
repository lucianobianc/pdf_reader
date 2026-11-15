#include "icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {
void drawIcon(QPainter &painter, ReaderIcon icon, QColor foreground)
{
    painter.setPen(QPen(foreground, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    switch (icon) {
    case ReaderIcon::Navigate: {
        QPolygonF pointer{{5, 3}, {5, 19}, {9, 15}, {12, 21}, {15, 19}, {12, 13}, {18, 12}};
        painter.drawPolygon(pointer);
        break;
    }
    case ReaderIcon::Highlight:
        painter.drawPolygon(QPolygonF{{5, 12}, {14, 3}, {20, 9}, {11, 18}});
        painter.drawPolyline(QPolygonF{{5, 12}, {4, 18}, {8, 19}, {11, 18}});
        painter.drawLine(4, 22, 20, 22);
        break;
    case ReaderIcon::Draw:
        painter.drawPolygon(QPolygonF{{4, 16}, {16, 4}, {20, 8}, {8, 20}, {3, 21}});
        painter.drawLine(13, 7, 17, 11);
        break;
    case ReaderIcon::Text:
        painter.drawLine(5, 5, 19, 5);
        painter.drawLine(12, 5, 12, 20);
        painter.drawLine(8, 20, 16, 20);
        painter.drawLine(5, 5, 5, 8);
        painter.drawLine(19, 5, 19, 8);
        break;
    case ReaderIcon::Eraser:
        painter.drawPolygon(QPolygonF{{3, 14}, {13, 4}, {21, 12}, {13, 20}, {9, 20}});
        painter.drawLine(8, 9, 17, 17);
        painter.drawLine(10, 21, 21, 21);
        break;
    case ReaderIcon::Color:
        painter.drawEllipse(QRectF(3, 3, 18, 18));
        for (const auto &dot : {QPair<QPointF, QColor>{{8, 8}, QColor("#e25e6c")},
                {{16, 8}, QColor("#56bfa1")}, {{8, 16}, QColor("#e5b93f")}, {{16, 16}, QColor("#679ee8")}}) {
            painter.setBrush(dot.second);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(dot.first, 2.5, 2.5);
        }
        break;
    case ReaderIcon::Undo: {
        QPainterPath path;
        path.moveTo(5, 9);
        path.cubicTo(21, 3, 24, 21, 10, 20);
        painter.drawPath(path);
        painter.drawPolyline(QPolygonF{{9, 4}, {4, 9}, {9, 13}});
        break;
    }
    case ReaderIcon::Save:
        painter.drawPolygon(QPolygonF{{4, 3}, {18, 3}, {21, 6}, {21, 21}, {4, 21}});
        painter.drawRect(QRectF(8, 3, 8, 6));
        painter.drawRect(QRectF(8, 14, 9, 7));
        break;
    case ReaderIcon::Outline:
        for (int y : {6, 12, 18}) { painter.drawPoint(4, y); painter.drawLine(9, y, 20, y); }
        break;
    case ReaderIcon::Help:
        painter.drawEllipse(QRectF(3, 3, 18, 18));
        painter.drawArc(QRectF(9, 6, 6, 7), 0, 180 * 16);
        painter.drawPolyline(QPolygonF{{15, 10}, {12, 13}, {12, 14}});
        painter.drawPoint(12, 18);
        break;
    case ReaderIcon::Light:
        painter.drawEllipse(QRectF(8, 8, 8, 8));
        for (int rotation = 0; rotation < 360; rotation += 45) {
            painter.save(); painter.translate(12, 12); painter.rotate(rotation);
            painter.drawLine(0, -8, 0, -10); painter.restore();
        }
        break;
    case ReaderIcon::Dark: {
        QPainterPath moon;
        moon.moveTo(15, 3);
        moon.cubicTo(1, 0, 0, 22, 15, 21);
        moon.cubicTo(20, 21, 22, 17, 21, 15);
        moon.cubicTo(10, 19, 7, 8, 15, 3);
        painter.drawPath(moon);
        break;
    }
    case ReaderIcon::Sepia:
        painter.setBrush(QColor("#d2b787"));
        painter.drawRoundedRect(QRectF(5, 3, 14, 18), 2, 2);
        painter.drawLine(8, 8, 16, 8);
        painter.drawLine(8, 12, 16, 12);
        painter.drawLine(8, 16, 13, 16);
        break;
    case ReaderIcon::Burgundy:
        painter.setBrush(QColor("#a75571"));
        painter.drawRoundedRect(QRectF(6, 3, 12, 11), 4, 4);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(12, 14, 12, 21);
        painter.drawLine(8, 21, 16, 21);
        break;
    case ReaderIcon::ThinStroke:
    case ReaderIcon::MediumStroke:
    case ReaderIcon::ThickStroke:
        painter.setPen(QPen(foreground, icon == ReaderIcon::ThinStroke ? 1 : icon == ReaderIcon::MediumStroke ? 2.5 : 4.5,
            Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(4, 17, 20, 7);
        break;
    }
}
}

QIcon readerIcon(ReaderIcon icon, QColor foreground)
{
    QIcon result;
    for (int size : {24, 32, 48, 64, 96}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 24.0, size / 24.0);
        drawIcon(painter, icon, foreground);
        painter.end();
        result.addPixmap(pixmap);
    }
    return result;
}
