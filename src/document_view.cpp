#include "document_view.h"
#include <QResizeEvent>
#include <QScrollBar>
#include <algorithm>

namespace {
constexpr int PageMargin = 18;
constexpr int PageGap = 16;
}

DocumentView::DocumentView(QWidget *parent) : QScrollArea(parent), pagesWidget_(new QWidget)
{
    setObjectName("pageScroll");
    setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    setFocusPolicy(Qt::StrongFocus);
    setWidget(pagesWidget_);
    renderTimer_.setSingleShot(true);
    connect(&renderTimer_, &QTimer::timeout, this, &DocumentView::updateVisiblePages);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (positioning_) return;
        updateCurrentPage();
        renderTimer_.start(0);
    });
    viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(viewport(), &QWidget::customContextMenuRequested, this, [this](QPoint point) {
        emit annotationToolsRequested(viewport()->mapToGlobal(point));
    });
}

void DocumentView::setDocument(const Document *document)
{
    renderTimer_.stop();
    positioning_ = true;
    qDeleteAll(pages_);
    pages_.clear();
    pageSizes_.clear();
    document_ = document;
    currentPage_ = -1;
    for (int i = 0; document_ && i < document_->pageCount(); ++i) {
        auto *canvas = new PageCanvas(pagesWidget_);
        pageSizes_.append(document_->pageSize(i));
        canvas->setTheme(theme_);
        canvas->setTool(tool_);
        canvas->setColor(color_);
        canvas->setPenWidth(penWidth_);
        canvas->setAnnotations(marks_);
        canvas->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(canvas, &QWidget::customContextMenuRequested, this, [this, canvas](QPoint point) {
            setCurrentPage(canvas->pageNumber());
            emit annotationToolsRequested(canvas->mapToGlobal(point));
        });
        connect(canvas, &PageCanvas::pageActivated, this, &DocumentView::setCurrentPage);
        connect(canvas, &PageCanvas::annotationCreated, this, &DocumentView::annotationCreated);
        connect(canvas, &PageCanvas::textBoxRequested, this, &DocumentView::textBoxRequested);
        connect(canvas, &PageCanvas::annotationEraseRequested, this, &DocumentView::annotationEraseRequested);
        pages_.append(canvas);
    }
    layoutPages();
    positioning_ = false;
    if (!pages_.isEmpty()) goToPage(0);
}

void DocumentView::layoutPages()
{
    int widest = 0;
    for (int i = 0; i < pages_.size(); ++i) {
        const QSize size = Document::rasterSize(pageSizes_[i], zoom_);
        pages_[i]->preparePage(i, size);
        widest = std::max(widest, size.width());
    }
    int top = PageMargin;
    for (auto *canvas : pages_) {
        canvas->move(PageMargin + (widest - canvas->width()) / 2, top);
        canvas->show();
        top += canvas->height() + PageGap;
    }
    pagesWidget_->setFixedSize(std::max(1, widest + 2 * PageMargin),
        std::max(1, top - PageGap + PageMargin));
}

PageCanvas *DocumentView::pageCanvas(int page) const
{
    return page >= 0 && page < pages_.size() ? pages_[page] : nullptr;
}

void DocumentView::goToPage(int page, bool bottom)
{
    auto *canvas = pageCanvas(page);
    if (!canvas) return;
    positioning_ = true;
    const int top = bottom ? canvas->y() + canvas->height() + PageMargin - viewport()->height() : canvas->y();
    verticalScrollBar()->setValue(top);
    positioning_ = false;
    setCurrentPage(page);
    updateVisiblePages();
    setFocus();
}

void DocumentView::setZoom(double zoom)
{
    zoom = std::clamp(zoom, 0.5, 3.0);
    if (zoom_ == zoom) return;
    const auto *anchor = pageCanvas(currentPage_);
    const double relativeTop = anchor ? double(verticalScrollBar()->value() - anchor->y()) / anchor->height() : 0;
    const double horizontalFraction = double(horizontalScrollBar()->value()) / std::max(1, horizontalScrollBar()->maximum());
    positioning_ = true;
    zoom_ = zoom;
    layoutPages();
    if (anchor) verticalScrollBar()->setValue(qRound(anchor->y() + relativeTop * anchor->height()));
    horizontalScrollBar()->setValue(qRound(horizontalFraction * horizontalScrollBar()->maximum()));
    positioning_ = false;
    updateVisiblePages();
}

void DocumentView::updateVisiblePages()
{
    if (!document_ || pages_.isEmpty()) return;
    const int top = verticalScrollBar()->value();
    const int bottom = top + viewport()->height();
    int first = -1;
    int last = -1;
    for (int i = 0; i < pages_.size(); ++i) {
        const auto *canvas = pages_[i];
        if (canvas->y() < bottom && canvas->y() + canvas->height() > top) {
            if (first < 0) first = i;
            last = i;
        }
    }
    if (first < 0) return;
    // Include one neighbor on either side so crossing a page boundary is smooth.
    first = std::max(0, first - 1);
    last = std::min(int(pages_.size()) - 1, last + 1);
    for (int i = 0; i < pages_.size(); ++i) {
        auto *canvas = pages_[i];
        if (i < first || i > last) {
            if (!canvas->hasImage()) continue;
            // A mouse drag owns its page until release, even when it leaves view.
            if (!canvas->isDrawing()) canvas->releaseImage();
        } else if (!canvas->hasImage()) {
            const auto image = document_->render(i, zoom_);
            if (image.isNull()) emit renderFailed();
            else canvas->setImage(image);
        }
    }
}

void DocumentView::setCurrentPage(int page)
{
    if (page == currentPage_ || !pageCanvas(page)) return;
    currentPage_ = page;
    emit currentPageChanged(page);
}

void DocumentView::updateCurrentPage()
{
    const int top = verticalScrollBar()->value();
    const int bottom = top + viewport()->height();
    int bestPage = -1;
    int bestHeight = 0;
    for (int i = 0; i < pages_.size(); ++i) {
        const auto *canvas = pages_[i];
        const int visibleHeight = std::min(bottom, canvas->y() + canvas->height()) - std::max(top, canvas->y());
        if (visibleHeight > bestHeight) { bestPage = i; bestHeight = visibleHeight; }
    }
    if (bestPage >= 0) setCurrentPage(bestPage);
}

void DocumentView::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    if (!positioning_) renderTimer_.start(0);
}

void DocumentView::setTheme(PageCanvas::Theme theme)
{
    theme_ = theme;
    for (auto *canvas : pages_) canvas->setTheme(theme);
}

void DocumentView::setTool(PageCanvas::Tool tool)
{
    tool_ = tool;
    for (auto *canvas : pages_) canvas->setTool(tool);
}

void DocumentView::setColor(QColor color)
{
    color_ = color;
    for (auto *canvas : pages_) canvas->setColor(color);
}

void DocumentView::setPenWidth(double width)
{
    penWidth_ = width;
    for (auto *canvas : pages_) canvas->setPenWidth(width);
}

void DocumentView::setAnnotations(QVector<Annotation> marks)
{
    marks_ = std::move(marks);
    for (auto *canvas : pages_) canvas->setAnnotations(marks_);
}
