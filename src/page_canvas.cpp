#include "page_canvas.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

PageCanvas::PageCanvas(QWidget *parent) : QWidget(parent)
{
    setObjectName("pageCanvas");
    setFixedSize(1, 1);
}

void PageCanvas::setPage(QImage image, int page)
{
    preparePage(page, image.isNull() ? QSize(1, 1) : image.size());
    setImage(std::move(image));
}

void PageCanvas::preparePage(int page, QSize size)
{
    releaseImage();
    page_ = page;
    setFixedSize(size.expandedTo(QSize(1, 1)));
}

void PageCanvas::setImage(QImage image)
{
    original_ = std::move(image);
    display_ = applyTheme(original_, theme_);
    update();
}

void PageCanvas::releaseImage()
{
    drawing_ = false;
    original_ = {};
    display_ = {};
    update();
}

QImage PageCanvas::applyTheme(const QImage &source, Theme theme)
{
    auto result = source.convertToFormat(QImage::Format_RGB32);
    if (theme == Theme::Light) return result;
    const QColor paper = theme == Theme::Dark ? QColor("#202329") :
        theme == Theme::Sepia ? QColor("#ead9b5") : QColor("#301b29");
    const QColor ink = theme == Theme::Dark ? QColor("#dadde3") :
        theme == Theme::Sepia ? QColor("#463627") : QColor("#e4bcc5");
    // Map luminance to paper/ink colors. Only the PDF raster is transformed.
    for (int y = 0; y < result.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const double light = (0.2126 * qRed(row[x]) + 0.7152 * qGreen(row[x]) + 0.0722 * qBlue(row[x])) / 255;
            row[x] = qRgb(qRound(ink.red() + light * (paper.red() - ink.red())),
                qRound(ink.green() + light * (paper.green() - ink.green())),
                qRound(ink.blue() + light * (paper.blue() - ink.blue())));
        }
    }
    return result;
}

void PageCanvas::setTheme(Theme theme)
{
    theme_ = theme;
    display_ = applyTheme(original_, theme_);
    update();
}

void PageCanvas::setTool(Tool tool)
{
    drawing_ = false;
    tool_ = tool;
    setCursor(tool == Tool::Navigate ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void PageCanvas::setAnnotations(QVector<Annotation> marks) { marks_ = std::move(marks); update(); }

void PageCanvas::paintAnnotation(QPainter &painter, const Annotation &mark) const
{
    if (mark.page != page_ || mark.points.size() < 2) return;
    auto pixel = [this](QPointF p) { return QPointF(p.x() * width(), p.y() * height()); };
    if (mark.kind == Annotation::Kind::Highlight) {
        QColor color = mark.color;
        color.setAlpha(85);
        painter.fillRect(QRectF(pixel(mark.points.first()), pixel(mark.points.last())).normalized(), color);
    } else if (mark.kind == Annotation::Kind::Text) {
        const QRectF box = QRectF(pixel(mark.points.first()), pixel(mark.points.last())).normalized();
        painter.setPen(QPen(mark.color, 1, Qt::DashLine));
        painter.drawRect(box);
        QFont font = painter.font();
        font.setPixelSize(std::max(8, qRound(mark.width * width())));
        painter.setFont(font);
        painter.setPen(mark.color);
        painter.drawText(box.adjusted(4, 4, -4, -4), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, mark.text);
    } else {
        painter.setPen(QPen(mark.color, mark.width * width(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path(pixel(mark.points.first()));
        for (int i = 1; i < mark.points.size(); ++i) path.lineTo(pixel(mark.points[i]));
        painter.drawPath(path);
    }
}

void PageCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (display_.isNull()) painter.fillRect(rect(), palette().base());
    else painter.drawImage(rect(), display_);
    for (const auto &mark : marks_) paintAnnotation(painter, mark);
    if (drawing_) paintAnnotation(painter, draft_);
}

QPointF PageCanvas::normalized(const QPointF &point) const
{
    return {std::clamp(point.x() / width(), 0.0, 1.0), std::clamp(point.y() / height(), 0.0, 1.0)};
}

void PageCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) emit pageActivated(page_);
    if (event->button() != Qt::LeftButton || tool_ == Tool::Navigate || original_.isNull()) return;
    if (tool_ == Tool::Eraser) { eraseAt(event->position()); return; }
    drawing_ = true;
    draft_ = Annotation{page_, tool_ == Tool::Highlight ? Annotation::Kind::Highlight :
        tool_ == Tool::Text ? Annotation::Kind::Text : Annotation::Kind::Ink,
        color_, tool_ == Tool::Text ? 0.022 : penWidth_,
        {normalized(event->position()), normalized(event->position())}, {}};
    update();
}

void PageCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (tool_ == Tool::Eraser && event->buttons().testFlag(Qt::LeftButton)) {
        eraseAt(event->position());
        return;
    }
    if (!drawing_) return;
    const QPointF point = normalized(event->position());
    if (draft_.kind != Annotation::Kind::Ink) draft_.points.last() = point;
    else draft_.points.append(point);
    update();
}

void PageCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (!drawing_ || event->button() != Qt::LeftButton) return;
    mouseMoveEvent(event);
    drawing_ = false;
    if (draft_.kind == Annotation::Kind::Ink ||
        (std::abs(draft_.points.first().x() - draft_.points.last().x()) > 0.002 &&
         std::abs(draft_.points.first().y() - draft_.points.last().y()) > 0.002)) {
        if (draft_.kind == Annotation::Kind::Text) emit textBoxRequested(draft_);
        else emit annotationCreated(draft_);
    }
    update();
}

void PageCanvas::eraseAt(QPointF point)
{
    auto pixel = [this](QPointF p) { return QPointF(p.x() * width(), p.y() * height()); };
    // Walk backwards so overlapping annotations are erased from top to bottom.
    for (int i = marks_.size() - 1; i >= 0; --i) {
        const auto &mark = marks_[i];
        if (mark.page != page_ || mark.points.size() < 2) continue;
        bool hit = false;
        if (mark.kind != Annotation::Kind::Ink) {
            hit = QRectF(pixel(mark.points.first()), pixel(mark.points.last())).normalized()
                .adjusted(-4, -4, 4, 4).contains(point);
        } else {
            const double radius = 7 + mark.width * width() / 2;
            for (int j = 1; j < mark.points.size() && !hit; ++j) {
                const QPointF start = pixel(mark.points[j - 1]);
                const QPointF segment = pixel(mark.points[j]) - start;
                const double lengthSquared = QPointF::dotProduct(segment, segment);
                const double t = lengthSquared > 0 ? std::clamp(
                    QPointF::dotProduct(point - start, segment) / lengthSquared, 0.0, 1.0) : 0;
                const QPointF delta = point - (start + t * segment);
                hit = QPointF::dotProduct(delta, delta) <= radius * radius;
            }
        }
        if (hit) { emit annotationEraseRequested(i); return; }
    }
}
