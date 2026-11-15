#include "document.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <cmath>

bool Document::open(const QString &path, QString &error)
{
    auto candidate = Poppler::Document::load(path);
    if (!candidate || candidate->isLocked() || candidate->numPages() == 0) {
        error = QStringLiteral("Could not open the PDF. Check that it is valid and does not require a password.");
        return false;
    }
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
        error = QStringLiteral("Could not read the document.");
        return false;
    }
    candidate->setRenderHint(Poppler::Document::Antialiasing);
    candidate->setRenderHint(Poppler::Document::TextAntialiasing);
    pdf_ = std::move(candidate);
    path_ = QFileInfo(path).absoluteFilePath();
    fingerprint_ = QString::fromLatin1(hash.result().toHex());
    return true;
}

int Document::pageCount() const { return pdf_ ? pdf_->numPages() : 0; }

QImage Document::render(int index, double zoom) const
{
    if (!pdf_ || index < 0 || index >= pageCount()) return {};
    auto page = pdf_->page(index);
    if (!page) return {};
    const QSize size = rasterSize(page->pageSizeF(), zoom);
    const QSizeF points = page->pageSizeF();
    if (points.isEmpty()) return {};
    const double dpi = std::min(size.width() * 72 / points.width(), size.height() * 72 / points.height());
    return page->renderToImage(dpi, dpi);
}

QSizeF Document::pageSize(int index) const
{
    if (!pdf_ || index < 0 || index >= pageCount()) return {};
    const auto page = pdf_->page(index);
    return page ? page->pageSizeF() : QSizeF{};
}

QSize Document::rasterSize(QSizeF points, double zoom)
{
    if (points.isEmpty()) return {1, 1};
    double scale = 96 * zoom / 72;
    const double pixels = points.width() * points.height() * scale * scale;
    if (pixels > 16000000) scale *= std::sqrt(16000000 / pixels);
    return (points * scale).toSize().expandedTo(QSize(1, 1));
}

QVector<Poppler::OutlineItem> Document::outline() const
{
    return pdf_ ? pdf_->outline() : QVector<Poppler::OutlineItem>{};
}

namespace {
// Some outlines use a non-clickable top-level heading to group chapter sections.
// In that case, use the first local page destination in that heading's subtree.
int firstChapterPage(const Poppler::OutlineItem &entry, int pageCount)
{
    const auto destination = entry.destination();
    if (destination && entry.externalFileName().isEmpty() && entry.uri().isEmpty()) {
        const int page = destination->pageNumber() - 1;
        if (page >= 0 && page < pageCount) return page;
    }
    for (const auto &child : entry.children()) {
        const int page = firstChapterPage(child, pageCount);
        if (page >= 0) return page;
    }
    return -1;
}
}

QVector<Chapter> Document::chapters() const
{
    QVector<Chapter> chapters;
    for (const auto &entry : outline()) {
        const int page = firstChapterPage(entry, pageCount());
        if (page >= 0) chapters.append({entry.name(), page});
    }
    return chapters;
}
