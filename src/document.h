#pragma once
#include <QImage>
#include <QString>
#include <memory>
#include <poppler-qt6.h>

struct Chapter {
    QString title;
    int page; // Zero-based, like Document::render().
};

class Document {
public:
    bool open(const QString &path, QString &error);
    int pageCount() const;
    QImage render(int page, double zoom) const;
    QSizeF pageSize(int page) const;
    static QSize rasterSize(QSizeF points, double zoom);
    QVector<Poppler::OutlineItem> outline() const;
    QVector<Chapter> chapters() const;
    QString path() const { return path_; }
    QString fingerprint() const { return fingerprint_; }
private:
    std::unique_ptr<Poppler::Document> pdf_;
    QString path_;
    QString fingerprint_;
};
