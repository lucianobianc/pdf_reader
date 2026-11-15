#pragma once
#include "annotations.h"
#include <QWidget>
#include <QImage>

class PageCanvas : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Navigate, Highlight, Ink, Text, Eraser };
    enum class Theme { Light, Dark, Sepia, Burgundy };
    explicit PageCanvas(QWidget *parent = nullptr);
    void setPage(QImage image, int page);
    void preparePage(int page, QSize size);
    void setImage(QImage image);
    void releaseImage();
    bool hasImage() const { return !original_.isNull(); }
    bool isDrawing() const { return drawing_; }
    int pageNumber() const { return page_; }
    void setTheme(Theme theme);
    void setTool(Tool tool);
    void setColor(QColor color) { color_ = color; }
    void setPenWidth(double width) { penWidth_ = width; }
    void setAnnotations(QVector<Annotation> marks);
    static QImage applyTheme(const QImage &source, Theme theme);
signals:
    void pageActivated(int page);
    void annotationCreated(const Annotation &mark);
    void textBoxRequested(const Annotation &box);
    void annotationEraseRequested(int index);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    void eraseAt(QPointF point);
    QPointF normalized(const QPointF &point) const;
    void paintAnnotation(QPainter &painter, const Annotation &mark) const;
    QImage original_, display_;
    QVector<Annotation> marks_;
    Annotation draft_;
    Theme theme_ = Theme::Light;
    Tool tool_ = Tool::Navigate;
    QColor color_ = QColor("#e5b93f");
    double penWidth_ = 0.004;
    int page_ = 0;
    bool drawing_ = false;
};
