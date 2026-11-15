#pragma once
#include "document.h"
#include "page_canvas.h"
#include <QScrollArea>
#include <QTimer>

// Lays out every page but only keeps raster images near the viewport in memory.
class DocumentView : public QScrollArea {
    Q_OBJECT
public:
    explicit DocumentView(QWidget *parent = nullptr);
    void setDocument(const Document *document);
    void goToPage(int page, bool bottom = false);
    void setZoom(double zoom);
    void setTheme(PageCanvas::Theme theme);
    void setTool(PageCanvas::Tool tool);
    void setColor(QColor color);
    void setPenWidth(double width);
    void setAnnotations(QVector<Annotation> marks);
    int currentPage() const { return currentPage_; }
    PageCanvas *pageCanvas(int page) const;
signals:
    void currentPageChanged(int page);
    void annotationCreated(const Annotation &mark);
    void textBoxRequested(const Annotation &box);
    void annotationEraseRequested(int index);
    void annotationToolsRequested(QPoint globalPosition);
    void renderFailed();
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void layoutPages();
    void updateVisiblePages();
    void updateCurrentPage();
    void setCurrentPage(int page);
    QWidget *pagesWidget_;
    const Document *document_ = nullptr;
    QVector<PageCanvas *> pages_;
    QVector<QSizeF> pageSizes_;
    QVector<Annotation> marks_;
    QTimer renderTimer_;
    double zoom_ = 1;
    PageCanvas::Theme theme_ = PageCanvas::Theme::Light;
    PageCanvas::Tool tool_ = PageCanvas::Tool::Navigate;
    QColor color_ = QColor("#e5b93f");
    double penWidth_ = 0.004;
    int currentPage_ = -1;
    bool positioning_ = false;
};
