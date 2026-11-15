#include "annotations.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>

bool AnnotationStore::load(const QString &path, const QString &fingerprint, int pageCount, QString &error)
{
    items.clear();
    QFile file(path + ".nightreader.json");
    // Read annotations from earlier versions when no file with the current name exists.
    // Saving always uses the current name and leaves the legacy file intact.
    if (!file.exists()) file.setFileName(path + ".vireo.json");
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) {
        error = QStringLiteral("Could not read the annotation file (limit: 32 MB).");
        return false;
    }
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto root = json.object();
    if (parseError.error != QJsonParseError::NoError || root["version"].toInt() != 1 ||
        root["sha256"].toString() != fingerprint || !root["annotations"].isArray()) {
        error = QStringLiteral("The annotation file is invalid or belongs to another PDF. It will not be overwritten.");
        return false;
    }
    QVector<Annotation> loaded;
    for (const auto &value : root["annotations"].toArray()) {
        const auto object = value.toObject();
        Annotation mark;
        mark.page = object["page"].toInt(-1);
        const auto kind = object["kind"].toString();
        mark.kind = kind == "highlight" ? Annotation::Kind::Highlight :
            kind == "text" ? Annotation::Kind::Text : Annotation::Kind::Ink;
        mark.text = object["text"].toString();
        mark.color = QColor(object["color"].toString());
        mark.width = object["width"].toDouble(-1);
        bool valid = object["points"].isArray() && mark.page >= 0 && mark.page < pageCount &&
            (kind == "highlight" || kind == "ink" || kind == "text") && mark.color.isValid() &&
            std::isfinite(mark.width) && mark.width > 0 && mark.width <= 0.1;
        for (const auto &pointValue : object["points"].toArray()) {
            const auto point = pointValue.toArray();
            const double x = point.size() == 2 ? point[0].toDouble(-1) : -1;
            const double y = point.size() == 2 ? point[1].toDouble(-1) : -1;
            valid = valid && x >= 0 && x <= 1 && y >= 0 && y <= 1;
            mark.points.append({x, y});
        }
        valid = valid && (mark.kind != Annotation::Kind::Ink ? mark.points.size() == 2 : mark.points.size() >= 2)
            && (mark.kind != Annotation::Kind::Text || (!mark.text.trimmed().isEmpty() && mark.text.size() <= 10000));
        if (!valid) {
            error = QStringLiteral("The annotations contain invalid data. The file will not be overwritten.");
            return false;
        }
        loaded.append(mark);
    }
    items = std::move(loaded);
    return true;
}

bool AnnotationStore::save(const QString &path, const QString &fingerprint, QString &error) const
{
    QJsonArray marks;
    for (const auto &mark : items) {
        QJsonArray points;
        for (const auto &point : mark.points) points.append(QJsonArray{point.x(), point.y()});
        marks.append(QJsonObject{{"page", mark.page},
            {"kind", mark.kind == Annotation::Kind::Highlight ? "highlight" :
                mark.kind == Annotation::Kind::Text ? "text" : "ink"},
            {"text", mark.text}, {"color", mark.color.name(QColor::HexArgb)}, {"width", mark.width}, {"points", points}});
    }
    QSaveFile file(path + ".nightreader.json");
    const QByteArray data = QJsonDocument(QJsonObject{{"version", 1}, {"sha256", fingerprint},
        {"annotations", marks}}).toJson();
    if (data.size() > 32 * 1024 * 1024) {
        error = QStringLiteral("Could not save annotations: ") + QStringLiteral("The annotation file exceeds the 32 MB limit.");
        return false;
    }
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = QStringLiteral("Could not save annotations: ") + file.errorString();
        return false;
    }
    return true;
}
