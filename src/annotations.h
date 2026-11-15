#pragma once
#include <QCoreApplication>
#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>

// Coordinates and pen width are fractions of the page, independent of zoom.
struct Annotation {
    enum class Kind { Highlight, Ink, Text };
    int page = 0;
    Kind kind = Kind::Ink;
    QColor color = QColor("#e5b93f");
    double width = 0.004;
    QVector<QPointF> points;
    QString text;
};

class AnnotationStore {
    Q_DECLARE_TR_FUNCTIONS(AnnotationStore)
public:
    QVector<Annotation> items;
    bool load(const QString &pdfPath, const QString &fingerprint, int pageCount, QString &error);
    bool save(const QString &pdfPath, const QString &fingerprint, QString &error) const;
};

Q_DECLARE_METATYPE(Annotation)
