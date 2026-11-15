#include "annotations.h"
#include "document.h"
#include "page_canvas.h"
#include "reader_window.h"
#include <QAction>
#include <QAbstractButton>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QFile>
#include <QInputDialog>
#include <QDialog>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <QDir>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPdfWriter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <functional>

class ReaderTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir directory_;
    QString pdf_;
    static QByteArray contents(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
    static bool writePdf(const QString &path, const QList<QByteArray> &objects) {
        QByteArray pdf = "%PDF-1.4\n";
        QList<int> offsets;
        for (int i = 0; i < objects.size(); ++i) {
            offsets.append(pdf.size());
            pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
        }
        const int xref = pdf.size();
        pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
        for (int offset : offsets) pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
        pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) +
            " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(pdf) == pdf.size();
    }
    static void command(ReaderWindow &window, const QString &text, std::function<void()> dialogAction = {}) {
        auto *scroll = window.findChild<QScrollArea *>("pageScroll");
        auto *input = window.findChild<QLineEdit *>("pageCommand");
        window.activateWindow();
        QApplication::processEvents();
        scroll->setFocus();
        QTest::keyClicks(scroll, ":");
        QTest::keyClicks(input, text);
        if (dialogAction) QTimer::singleShot(0, std::move(dialogAction));
        QTest::keyClick(input, Qt::Key_Return);
        QApplication::processEvents();
    }
    static QAction *action(ReaderWindow &window, const QString &text) {
        for (auto *candidate : window.findChildren<QAction *>())
            if (candidate->text() == text) return candidate;
        return nullptr;
    }
    static void activate(ReaderWindow &window) {
        window.show();
        window.activateWindow();
        window.raise();
        QApplication::processEvents();
    }
    static void drag(PageCanvas &canvas, QPoint from, QPoint to) {
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(&canvas, to);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, to);
    }
private slots:
    void initTestCase() {
        QVERIFY(directory_.isValid());
        pdf_ = directory_.filePath("sample.pdf");
        QPdfWriter writer(pdf_);
        writer.setResolution(96);
        QPainter painter(&writer);
        for (int i = 0; i < 3; ++i) {
            if (i) QVERIFY(writer.newPage());
            painter.drawText(50, 80, QString("Readable PDF page %1").arg(i + 1));
        }
        painter.end();
    }
    void renderAndPreserveOriginal() {
        const auto before = contents(pdf_);
        Document document;
        QString error;
        QVERIFY2(document.open(pdf_, error), qPrintable(error));
        QCOMPARE(document.pageCount(), 3);
        QVERIFY(!document.render(0, 1).isNull());
        QVERIFY(document.render(-1, 1).isNull());
        QVERIFY(document.render(3, 1).isNull());
        QCOMPARE(document.fingerprint(), QString::fromLatin1(QCryptographicHash::hash(before, QCryptographicHash::Sha256).toHex()));
        QVERIFY(!document.open(directory_.filePath("missing.pdf"), error));
        QCOMPARE(document.pageCount(), 3); // Failed opens preserve the current document.
        QCOMPARE(contents(pdf_), before);
    }
    void annotationRoundTripAndValidation() {
        const QString path = directory_.filePath("marks.pdf");
        AnnotationStore original;
        original.items = {
            {0, Annotation::Kind::Highlight, QColor("yellow"), .004, {{.1, .1}, {.4, .2}}, {}},
            {1, Annotation::Kind::Ink, QColor("red"), .004, {{.1, .1}, {.2, .3}, {.4, .2}}, {}},
            {2, Annotation::Kind::Text, QColor("blue"), .022, {{.1, .1}, {.5, .4}}, "Text\nAção ✓"}
        };
        QString error;
        QVERIFY2(original.save(path, "fingerprint", error), qPrintable(error));
        const QByteArray before = contents(path + ".nightreader.json");
        AnnotationStore loaded;
        QVERIFY(loaded.load(path, "fingerprint", 3, error));
        QCOMPARE(loaded.items.size(), 3);
        QCOMPARE(loaded.items[2].text, original.items[2].text);
        QCOMPARE(loaded.items[1].points, original.items[1].points);
        QCOMPARE(loaded.items[0].color, original.items[0].color);
        QVERIFY(!loaded.load(path, "different-document", 3, error));
        QVERIFY(loaded.items.isEmpty());
        QCOMPARE(contents(path + ".nightreader.json"), before);
        QVERIFY(!loaded.load(path, "fingerprint", 1, error));
        QVERIFY(!original.save(directory_.filePath("missing/marks.pdf"), "fingerprint", error));
    }
    void legacyAnnotationFiles() {
        const QString path = directory_.filePath("legacy.pdf");
        AnnotationStore original;
        original.items = {{0, Annotation::Kind::Text, QColor("blue"), .022,
            {{.1, .1}, {.5, .4}}, "Existing note"}};
        QString error;
        QVERIFY(original.save(path, "fingerprint", error));
        QVERIFY(QFile::rename(path + ".nightreader.json", path + ".vireo.json"));
        const auto legacyContents = contents(path + ".vireo.json");
        AnnotationStore loaded;
        QVERIFY(loaded.load(path, "fingerprint", 1, error));
        QCOMPARE(loaded.items.size(), 1);
        QCOMPARE(loaded.items[0].text, QString("Existing note"));
        loaded.items[0].text = "Updated note";
        QVERIFY(loaded.save(path, "fingerprint", error));
        QCOMPARE(contents(path + ".vireo.json"), legacyContents);
        QVERIFY(loaded.load(path, "fingerprint", 1, error));
        QCOMPARE(loaded.items[0].text, QString("Updated note"));
        QFile corrupted(path + ".nightreader.json");
        QVERIFY(corrupted.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corrupted.write("invalid json");
        corrupted.close();
        QVERIFY(!loaded.load(path, "fingerprint", 1, error));
        QVERIFY(loaded.items.isEmpty()); // Never silently fall back to stale notes.
    }
    void readingThemes() {
        QImage source(2, 1, QImage::Format_RGB32);
        source.setPixelColor(0, 0, Qt::white);
        source.setPixelColor(1, 0, Qt::black);
        QCOMPARE(PageCanvas::applyTheme(source, PageCanvas::Theme::Light), source);
        const auto dark = PageCanvas::applyTheme(source, PageCanvas::Theme::Dark);
        QCOMPARE(dark.pixelColor(0, 0), QColor("#202329"));
        QCOMPARE(dark.pixelColor(1, 0), QColor("#dadde3"));
        QCOMPARE(PageCanvas::applyTheme(source, PageCanvas::Theme::Sepia).pixelColor(0, 0), QColor("#ead9b5"));
        QCOMPARE(PageCanvas::applyTheme(source, PageCanvas::Theme::Burgundy).pixelColor(0, 0), QColor("#301b29"));
        QCOMPARE(source.pixelColor(0, 0), QColor(Qt::white));
    }
    void drawTextAndEraseHitTesting() {
        PageCanvas canvas;
        QImage paper(500, 700, QImage::Format_RGB32);
        paper.fill(Qt::white);
        canvas.setPage(paper, 0);
        canvas.show();
        QSignalSpy marks(&canvas, &PageCanvas::annotationCreated);
        QSignalSpy text(&canvas, &PageCanvas::textBoxRequested);
        QSignalSpy erases(&canvas, &PageCanvas::annotationEraseRequested);
        canvas.setTool(PageCanvas::Tool::Highlight);
        drag(canvas, {50, 70}, {200, 140});
        QCOMPARE(marks.count(), 1);
        const auto highlight = qvariant_cast<Annotation>(marks.takeFirst()[0]);
        QCOMPARE(highlight.points.first(), QPointF(.1, .1));
        QCOMPARE(highlight.points.last(), QPointF(.4, .2));
        canvas.setTool(PageCanvas::Tool::Text);
        drag(canvas, {250, 350}, {450, 550});
        QCOMPARE(text.count(), 1);
        auto box = qvariant_cast<Annotation>(text.takeFirst()[0]);
        QCOMPARE(box.kind, Annotation::Kind::Text);
        box.text = "Note";
        canvas.setAnnotations({highlight, box,
            {0, Annotation::Kind::Ink, Qt::red, .004, {{.1,.8},{.4,.8}}, {}}});
        canvas.setTool(PageCanvas::Tool::Eraser);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {300, 400});
        QCOMPARE(erases.takeFirst()[0].toInt(), 1);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QCOMPARE(erases.takeFirst()[0].toInt(), 0);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {100, 560});
        QCOMPARE(erases.takeFirst()[0].toInt(), 2);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {480, 680});
        QCOMPARE(erases.count(), 0);
        canvas.setPage(paper.scaled(1000,1400), 0);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {600, 800});
        QCOMPARE(erases.takeFirst()[0].toInt(), 1); // Hit positions remain correct after zoom.
    }
    void viNavigationAndPanels() {
        ReaderWindow window;
        activate(window);
        auto *dock = window.findChild<QDockWidget *>("outlineDock");
        QVERIFY(!dock->isVisible());
        QVERIFY(!dock->toggleViewAction()->isEnabled());
        QTest::keyClick(&window, Qt::Key_Tab);
        QVERIFY(!dock->isVisible());
        QVERIFY(window.openDocument(pdf_));
        QApplication::processEvents();
        auto *scroll = window.findChild<QScrollArea *>("pageScroll");
        QTest::keyClick(scroll, Qt::Key_G, Qt::ShiftModifier);
        QCOMPARE(window.currentPage(), 2);
        QTest::keyClicks(scroll, "gg");
        QCOMPARE(window.currentPage(), 0);
        QTest::keyClicks(scroll, ":");
        auto *command = window.findChild<QLineEdit *>("pageCommand");
        QVERIFY(command->isVisible());
        QTest::keyClicks(command, "#2");
        QTest::keyClick(command, Qt::Key_Return);
        QCOMPARE(window.currentPage(), 1);
        QTest::keyClicks(scroll, ":");
        QTest::keyClicks(command, "999");
        QTest::keyClick(command, Qt::Key_Return);
        QCOMPARE(window.currentPage(), 1);
        QVERIFY(command->isVisible());
        QTest::keyClick(command, Qt::Key_Escape);
        QVERIFY(!command->isVisible());
        QApplication::processEvents();
        scroll->verticalScrollBar()->setValue(0);
        QTest::keyClicks(scroll, "j");
        QVERIFY(scroll->verticalScrollBar()->value() > 0);
        QTest::keyClicks(scroll, "k");
        QCOMPARE(scroll->verticalScrollBar()->value(), 0);
        QTest::keyClicks(scroll, "d");
        QVERIFY(scroll->verticalScrollBar()->value() > 0);
        QTest::keyClicks(scroll, "u");
        QCOMPARE(scroll->verticalScrollBar()->value(), 0);
        QVERIFY(!dock->isVisible());
        QVERIFY(!dock->toggleViewAction()->isEnabled());
        QTest::keyClick(scroll, Qt::Key_Tab);
        QVERIFY(!dock->isVisible());
        QTest::keyClick(scroll, Qt::Key_Tab);
        QVERIFY(!dock->isVisible());
    }
    void addTextEraseUndoAndPreservePdf() {
        const QString path = directory_.filePath("interactive.pdf");
        QVERIFY(QFile::copy(pdf_, path));
        const auto before = contents(path);
        ReaderWindow window;
        activate(window);
        QVERIFY(window.openDocument(path));
        QApplication::processEvents();
        auto *canvas = window.findChild<PageCanvas *>("pageCanvas");
        auto *textAction = action(window, "Text");
        QVERIFY(textAction);
        textAction->trigger();
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (dialog) { dialog->setTextValue("A note\nSecond line"); dialog->accept(); }
        });
        drag(*canvas, {50, 70}, {350, 240});
        QString error;
        Document document;
        QVERIFY(document.open(path, error));
        AnnotationStore saved;
        QVERIFY(saved.load(path, document.fingerprint(), 3, error));
        QCOMPARE(saved.items.size(), 1);
        QCOMPARE(saved.items[0].text, QString("A note\nSecond line"));
        action(window, "Eraser")->trigger();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QVERIFY(saved.load(path, document.fingerprint(), 3, error));
        QCOMPARE(saved.items.size(), 0);
        action(window, "Undo")->trigger();
        QVERIFY(saved.load(path, document.fingerprint(), 3, error));
        QCOMPARE(saved.items.size(), 1);
        QCOMPARE(contents(path), before);
        window.close();
        ReaderWindow reopened;
        activate(reopened);
        QVERIFY(reopened.openDocument(path));
        QVERIFY(saved.load(path, document.fingerprint(), 3, error));
        QCOMPARE(saved.items.size(), 1);
    }
    void nestedDocumentOutline() {
        const QString path = directory_.filePath("outline.pdf");
        const QList<QByteArray> objects = {
            "<< /Type /Catalog /Pages 2 0 R /Outlines 5 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>",
            "<< /Type /Outlines /First 6 0 R /Last 6 0 R /Count 2 >>",
            "<< /Title (Chapter) /Parent 5 0 R /Dest [3 0 R /Fit] /First 7 0 R /Last 7 0 R /Count 1 >>",
            "<< /Title (Section) /Parent 6 0 R /Dest [4 0 R /Fit] >>"
        };
        QVERIFY(writePdf(path, objects));
        ReaderWindow window;
        activate(window);
        QVERIFY(window.openDocument(path));
        QApplication::processEvents();
        auto *dock = window.findChild<QDockWidget *>("outlineDock");
        QVERIFY(dock->isVisible());
        QVERIFY(dock->toggleViewAction()->isEnabled());
        auto *scroll = window.findChild<DocumentView *>();
        QTest::keyClick(scroll, Qt::Key_Tab);
        QVERIFY(!dock->isVisible());
        QTest::keyClick(scroll, Qt::Key_Tab);
        QVERIFY(dock->isVisible());
        auto *tree = window.findChild<QTreeWidget *>();
        QCOMPARE(tree->topLevelItemCount(), 1);
        auto *chapter = tree->topLevelItem(0);
        QCOMPARE(chapter->text(0), QString("Chapter"));
        QCOMPARE(chapter->childCount(), 1);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
            tree->visualItemRect(chapter->child(0)).center());
        QCOMPARE(window.currentPage(), 1);
        QVERIFY(window.openDocument(pdf_));
        QVERIFY(!dock->isVisible());
        QVERIFY(!dock->toggleViewAction()->isEnabled());
        QTest::keyClick(scroll, Qt::Key_Tab);
        QVERIFY(!dock->isVisible());
        QVERIFY(window.openDocument(path));
        QVERIFY(dock->isVisible());
    }
    void fullPageKeysAndPageAliases() {
        ReaderWindow window;
        activate(window);
        QVERIFY(window.openDocument(pdf_));
        QApplication::processEvents();
        auto *scroll = window.findChild<QScrollArea *>("pageScroll");
        for (int expected : {1, 2, 2}) {
            QTest::keyClick(scroll, Qt::Key_J, Qt::ShiftModifier);
            QApplication::processEvents();
            QCOMPARE(window.currentPage(), expected);
            auto *view = qobject_cast<DocumentView *>(scroll);
            QCOMPARE(view->pageCanvas(expected)->mapTo(scroll->viewport(), QPoint{}).y(), 0);
        }
        for (int expected : {1, 0, 0}) {
            QTest::keyClick(scroll, Qt::Key_K, Qt::ShiftModifier);
            QApplication::processEvents();
            QCOMPARE(window.currentPage(), expected);
            auto *view = qobject_cast<DocumentView *>(scroll);
            QCOMPARE(view->pageCanvas(expected)->mapTo(scroll->viewport(), QPoint{}).y(), 0);
        }
        for (const auto &alias : {"p 2", "page 2", "p #2", "page #2", "#2", "2"}) {
            command(window, "1");
            command(window, alias);
            QCOMPARE(window.currentPage(), 1);
        }
        command(window, "page 999");
        QCOMPARE(window.currentPage(), 1);
        auto *input = window.findChild<QLineEdit *>("pageCommand");
        QVERIFY(input->isVisible());
        QTest::keyClick(input, Qt::Key_Escape);
        command(window, "p");
        QVERIFY(input->isVisible());
        QCOMPARE(window.currentPage(), 1);
    }
    void chapterNavigationAndCommands() {
        const QString path = directory_.filePath("chapters.pdf");
        const QList<QByteArray> objects = {
            "<< /Type /Catalog /Pages 2 0 R /Outlines 8 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R 6 0 R 7 0 R] /Count 5 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>",
            "<< /Type /Outlines /First 9 0 R /Last 16 0 R /Count 8 >>",
            "<< /Title (Opening) /Parent 8 0 R /Next 11 0 R /Dest [3 0 R /Fit] /First 10 0 R /Last 10 0 R /Count 1 >>",
            "<< /Title (Subsection) /Parent 9 0 R /Dest [4 0 R /Fit] >>",
            "<< /Title (Middle) /Parent 8 0 R /Prev 9 0 R /Next 13 0 R /First 12 0 R /Last 12 0 R /Count 1 >>",
            "<< /Title (Middle section) /Parent 11 0 R /Dest [5 0 R /Fit] >>",
            "<< /Title (Final) /Parent 8 0 R /Prev 11 0 R /Next 14 0 R /Dest [7 0 R /Fit] >>",
            "<< /Title (Duplicate) /Parent 8 0 R /Prev 13 0 R /Next 15 0 R /Dest [7 0 R /Fit] >>",
            "<< /Title (External) /Parent 8 0 R /Prev 14 0 R /Next 16 0 R /A << /S /GoToR /F (other.pdf) /D [0 /Fit] >> >>",
            "<< /Title (Broken) /Parent 8 0 R /Prev 15 0 R /Dest [99 0 R /Fit] >>"
        };
        QVERIFY(writePdf(path, objects));
        Document document;
        QString error;
        QVERIFY(document.open(path, error));
        const auto chapters = document.chapters();
        QCOMPARE(chapters.size(), 4); // Sections, external links and broken links are excluded.
        QCOMPARE(chapters[0].title, QString("Opening"));
        QCOMPARE(chapters[1].title, QString("Middle"));
        QCOMPARE(chapters[1].page, 2); // Group heading falls back to its first local section.
        ReaderWindow window;
        activate(window);
        QVERIFY(window.openDocument(path));
        QApplication::processEvents();
        auto *scroll = window.findChild<QScrollArea *>("pageScroll");
        for (int expected : {2, 4, 4}) {
            QTest::keyClick(scroll, Qt::Key_L, Qt::ShiftModifier);
            QCOMPARE(window.currentPage(), expected);
        }
        for (int expected : {2, 0, 0}) {
            QTest::keyClick(scroll, Qt::Key_H, Qt::ShiftModifier);
            QCOMPARE(window.currentPage(), expected);
        }
        command(window, "page 4");
        QTest::keyClick(scroll, Qt::Key_H, Qt::ShiftModifier);
        QCOMPARE(window.currentPage(), 2); // From inside a chapter, return to its start.
        command(window, "c 3");
        QCOMPARE(window.currentPage(), 4);
        command(window, "chapter 2");
        QCOMPARE(window.currentPage(), 2);
        command(window, "c next");
        QCOMPARE(window.currentPage(), 4);
        command(window, "chapter prev");
        QCOMPARE(window.currentPage(), 2);
        command(window, "c 0");
        QCOMPARE(window.currentPage(), 2);
        auto *input = window.findChild<QLineEdit *>("pageCommand");
        QVERIFY(input->isVisible());
        QTest::keyClick(input, Qt::Key_Escape);
        for (const auto &alias : {"c", "chapter"}) {
            QCOMPARE(window.currentPage(), 2);
            bool sawDialog = false;
            int rowCount = -1;
            int selectedRow = -1;
            bool entered = false;
            command(window, alias, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                auto *list = dialog->findChild<QListWidget *>("chapterList");
                if (!list) { dialog->reject(); return; }
                sawDialog = true;
                rowCount = list->count();
                selectedRow = list->currentRow();
                list->setCurrentRow(0);
                QTest::keyClick(list, Qt::Key_Return);
                entered = !dialog->isVisible();
                if (!entered) dialog->reject();
            });
            QVERIFY(sawDialog);
            QCOMPARE(rowCount, 4);
            QCOMPARE(selectedRow, 1);
            QVERIFY(entered);
            QCOMPARE(window.currentPage(), 0);
            command(window, "c 2");
        }
        command(window, "c", [] {
            if (auto *dialog = QApplication::activeModalWidget()) {
                QTest::keyClick(dialog, Qt::Key_J, Qt::ShiftModifier);
                QTest::keyClick(dialog, Qt::Key_Escape);
            }
        });
        QCOMPARE(window.currentPage(), 2); // Cancelling the list does not navigate.
        QVERIFY(window.openDocument(pdf_)); // A different PDF must not reuse the old chapters.
        QTest::keyClick(scroll, Qt::Key_L, Qt::ShiftModifier);
        QCOMPARE(window.currentPage(), 0);
        command(window, "c");
        QVERIFY(input->isVisible());
        QCOMPARE(window.currentPage(), 0);
        QTest::keyClick(input, Qt::Key_Escape);
        command(window, "chapter 1");
        QVERIFY(input->isVisible());
        QCOMPARE(window.currentPage(), 0);
    }
    void continuousPagesAndAnnotations() {
        ReaderWindow window;
        activate(window);
        QVERIFY(window.openDocument(pdf_));
        auto *view = window.findChild<DocumentView *>();
        QApplication::processEvents();
        auto *first = view->pageCanvas(0);
        auto *second = view->pageCanvas(1);
        const int boundary = first->y() + first->height();
        view->verticalScrollBar()->setValue(boundary - view->viewport()->height() / 2);
        QApplication::processEvents();
        const QRect viewport = view->viewport()->rect();
        QVERIFY(viewport.intersects(QRect(first->mapTo(view->viewport(), QPoint{}), first->size())));
        QVERIFY(viewport.intersects(QRect(second->mapTo(view->viewport(), QPoint{}), second->size())));
        QVERIFY(first->hasImage());
        QVERIFY(second->hasImage());
        const int oldScroll = view->verticalScrollBar()->value();
        QTest::keyClicks(view, "j");
        QCOMPARE(view->verticalScrollBar()->value(), oldScroll + 48);
        QTest::keyClicks(view, "k");
        QCOMPARE(view->verticalScrollBar()->value(), oldScroll);
        const QPoint center = view->viewport()->rect().center();
        QWheelEvent wheel(center, view->viewport()->mapToGlobal(center), {}, QPoint(0, -120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &wheel);
        QVERIFY(view->verticalScrollBar()->value() > oldScroll);
        view->verticalScrollBar()->setValue(second->y());
        QApplication::processEvents();
        QCOMPARE(window.currentPage(), 1);
        QSignalSpy annotations(view, &DocumentView::annotationCreated);
        action(window, "Highlight")->trigger();
        drag(*second, {50, 70}, {200, 140});
        QCOMPARE(annotations.count(), 1);
        const auto mark = qvariant_cast<Annotation>(annotations.takeFirst()[0]);
        QCOMPARE(mark.page, 1);
        QCOMPARE(mark.points.first(), QPointF(50.0 / second->width(), 70.0 / second->height()));
        action(window, "Navigate [Esc]")->trigger();
        view->verticalScrollBar()->setValue(second->y() + 100);
        const double offset = double(view->verticalScrollBar()->value() - second->y()) / second->height();
        QTest::keyClicks(view, "+");
        QApplication::processEvents();
        QCOMPARE(window.currentPage(), 1);
        const double resizedOffset = double(view->verticalScrollBar()->value() - second->y()) / second->height();
        QVERIFY(std::abs(offset - resizedOffset) < .003);
        // The annotation remains attached to page 2 after zoom, theme and scrolling.
        command(window, "theme dark");
        QSignalSpy erased(view, &DocumentView::annotationEraseRequested);
        action(window, "Eraser")->trigger();
        QTest::mouseClick(second, Qt::LeftButton, Qt::NoModifier,
            {qRound(.12 * second->width()), qRound(.08 * second->height())});
        QCOMPARE(erased.count(), 1);
        action(window, "Undo")->trigger();
        const auto screenshots = qEnvironmentVariable("NIGHTREADER_TEST_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) {
            view->verticalScrollBar()->setValue(first->y() + first->height() - view->viewport()->height() / 2);
            QApplication::processEvents();
            QVERIFY(window.grab().save(screenshots + "/continuous.png"));
        }
    }
    void nearbyPagesOnlyAndMixedSizes() {
        const QString path = directory_.filePath("long.pdf");
        {
            QPdfWriter writer(path);
            writer.setResolution(96);
            QPainter painter(&writer);
            for (int i = 0; i < 12; ++i) {
                if (i) QVERIFY(writer.newPage());
                painter.drawText(50, 80, QString("Page %1").arg(i + 1));
            }
        }
        Document document;
        QString error;
        QVERIFY(document.open(path, error));
        DocumentView view;
        view.resize(700, 600);
        view.show();
        view.setDocument(&document);
        QApplication::processEvents();
        QVERIFY(view.pageCanvas(0)->hasImage());
        QVERIFY(!view.pageCanvas(10)->hasImage());
        view.goToPage(10);
        QApplication::processEvents();
        QCOMPARE(view.currentPage(), 10);
        QVERIFY(view.pageCanvas(10)->hasImage());
        QVERIFY(!view.pageCanvas(0)->hasImage());
        int loaded = 0;
        for (auto *canvas : view.findChildren<PageCanvas *>()) if (canvas->hasImage()) ++loaded;
        QVERIFY(loaded <= 4);
        view.setZoom(.5);
        view.goToPage(11, true);
        QCOMPARE(view.verticalScrollBar()->value(), view.verticalScrollBar()->maximum());
        QCOMPARE(view.currentPage(), 11);
        const QString mixedPath = directory_.filePath("mixed.pdf");
        QVERIFY(writePdf(mixedPath, {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 500] /Resources << >> >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 250] /Resources << >> >>"
        }));
        QVERIFY(document.open(mixedPath, error));
        view.setDocument(&document);
        QCOMPARE(view.findChildren<PageCanvas *>().size(), 2);
        QVERIFY(view.pageCanvas(0)->height() > view.pageCanvas(1)->height());
        QVERIFY(view.pageCanvas(0)->width() < view.pageCanvas(1)->width());
        QCOMPARE(view.pageCanvas(0)->geometry().center().x(), view.pageCanvas(1)->geometry().center().x());
        view.resize(1000, 900);
        QApplication::processEvents();
        QVERIFY(view.pageCanvas(0)->hasImage());
        QVERIFY(view.pageCanvas(1)->hasImage());
    }
    void floatingIconToolsAndThemes() {
        ReaderWindow window;
        activate(window);
        QVERIFY(window.openDocument(pdf_));
        QApplication::processEvents();
        QVERIFY(window.findChildren<QToolBar *>().isEmpty());
        auto *view = window.findChild<DocumentView *>();
        auto *canvas = view->pageCanvas(0);
        auto *menu = window.findChild<QMenu *>("annotationMenu");
        QVERIFY(menu);
        QVERIFY(!menu->isVisible());
        const QPoint at(100, 100);
        QContextMenuEvent context(QContextMenuEvent::Mouse, at, canvas->mapToGlobal(at));
        QApplication::sendEvent(canvas, &context);
        QApplication::processEvents();
        QVERIFY(menu->isVisible());
        QToolButton *highlight = nullptr;
        for (auto *button : menu->findChildren<QToolButton *>()) {
            QVERIFY(!button->icon().isNull());
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QVERIFY(!button->accessibleName().isEmpty());
            QVERIFY(!button->toolTip().isEmpty());
            if (button->defaultAction()->text() == "Highlight") highlight = button;
        }
        QVERIFY(highlight);
        const auto screenshots = qEnvironmentVariable("NIGHTREADER_TEST_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) QVERIFY(menu->grab().save(screenshots + "/tools.png"));
        QTest::mouseClick(highlight, Qt::LeftButton);
        QVERIFY(!menu->isVisible());
        QVERIFY(action(window, "Highlight")->isChecked());
        QSignalSpy marks(view, &DocumentView::annotationCreated);
        drag(*canvas, {50, 70}, {200, 140});
        QCOMPARE(marks.count(), 1);
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(action(window, "Navigate [Esc]")->isChecked());
        auto *themes = window.findChild<QComboBox *>("readingTheme");
        for (int expected : {1, 2, 3, 0}) {
            QTest::keyClicks(view, "t");
            QCOMPARE(themes->currentIndex(), expected);
        }
        command(window, "t burgundy");
        QCOMPARE(themes->currentIndex(), 3);
        command(window, "theme dark");
        QCOMPARE(themes->currentIndex(), 1);
        command(window, "t");
        QCOMPARE(themes->currentIndex(), 2);
        command(window, "theme unknown");
        QCOMPARE(themes->currentIndex(), 2);
        auto *input = window.findChild<QLineEdit *>("pageCommand");
        QVERIFY(input->isVisible());
        QTest::keyClick(input, Qt::Key_Escape);
        QTest::keyClicks(view, "a");
        QVERIFY(menu->isVisible());
        QTest::keyClick(menu, Qt::Key_Escape);
        QVERIFY(!menu->isVisible());
    }
    void openHelpAndQuitCommands() {
        const QString path = directory_.filePath("with  spaces.pdf");
        QVERIFY(QFile::copy(pdf_, path));
        ReaderWindow window;
        activate(window);
        QVERIFY(window.openDocument(pdf_));
        for (const auto &alias : {"o", "open"}) {
            command(window, QString(alias) + " \"" + path + "\"");
            QVERIFY(window.windowTitle().startsWith("with  spaces.pdf"));
            command(window, "page 2");
            bool sawPicker = false;
            command(window, alias, [&] {
                auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                sawPicker = dialog != nullptr;
                if (auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget())) active->reject();
            });
            QVERIFY(sawPicker);
            QCOMPARE(window.currentPage(), 1);
        }
        for (const auto &alias : {"h", "help"}) {
            bool sawHelp = false;
            command(window, alias, [&] {
                auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                sawHelp = dialog && dialog->text().contains(":open");
                if (auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget())) active->accept();
            });
            QVERIFY(sawHelp);
        }
        for (const auto &alias : {"q", "quit"}) {
            activate(window);
            command(window, alias);
            QVERIFY(!window.isVisible());
        }
    }
    void quitRespectsUnsavedAnnotations() {
        const QString path = directory_.filePath("unsaved.pdf");
        QVERIFY(QFile::copy(pdf_, path));
        const auto original = contents(path);
        ReaderWindow window;
        activate(window);
        QVERIFY(window.openDocument(path));
        // Block sidecar creation without relying on platform-specific permissions.
        QVERIFY(QDir().mkdir(path + ".nightreader.json"));
        action(window, "Highlight")->trigger();
        drag(*window.findChild<DocumentView *>()->pageCanvas(0), {50, 70}, {200, 140});
        bool askedToDiscard = false;
        command(window, "q", [&] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            askedToDiscard = dialog && dialog->button(QMessageBox::Cancel);
            if (askedToDiscard) QTest::mouseClick(dialog->button(QMessageBox::Cancel), Qt::LeftButton);
            else if (dialog) dialog->reject();
        });
        QVERIFY(askedToDiscard);
        QVERIFY(window.isVisible());
        command(window, "quit", [&] {
            if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                QTest::mouseClick(dialog->button(QMessageBox::Discard), Qt::LeftButton);
        });
        QVERIFY(!window.isVisible());
        QCOMPARE(contents(path), original);
    }
    void writeAliasesAndSaveBeforeQuit() {
        const QString path = directory_.filePath("write-commands.pdf");
        QVERIFY(QFile::copy(pdf_, path));
        const auto original = contents(path);
        ReaderWindow window;
        activate(window);
        QVERIFY(window.openDocument(path));
        QVERIFY(QDir().mkdir(path + ".nightreader.json"));
        action(window, "Highlight")->trigger();
        drag(*window.findChild<DocumentView *>()->pageCanvas(0), {50, 70}, {200, 140});
        command(window, "wq");
        QVERIFY(window.isVisible());
        QVERIFY(window.findChild<QLineEdit *>("pageCommand")->isVisible());
        QVERIFY(window.statusBar()->currentMessage().contains("Could not save"));
        QVERIFY(QDir().rmdir(path + ".nightreader.json"));
        Document document;
        QString error;
        QVERIFY(document.open(path, error));
        AnnotationStore saved;
        for (const auto &alias : {"s", "save", "w", "write"}) {
            command(window, alias);
            QVERIFY(saved.load(path, document.fingerprint(), 3, error));
            QCOMPARE(saved.items.size(), 1);
            QCOMPARE(saved.items[0].kind, Annotation::Kind::Highlight);
            QVERIFY(window.isVisible());
            QVERIFY(!window.findChild<QLineEdit *>("pageCommand")->isVisible());
            QVERIFY(QFile::remove(path + ".nightreader.json"));
        }
        // Explicit save writes even when autosave has already cleared the dirty flag.
        command(window, "write");
        QVERIFY(QFile::exists(path + ".nightreader.json"));
        command(window, "w " + path);
        QVERIFY(window.findChild<QLineEdit *>("pageCommand")->isVisible());
        QCOMPARE(contents(path), original);
        command(window, "wq");
        QVERIFY(!window.isVisible());
        QVERIFY(saved.load(path, document.fingerprint(), 3, error));
        QCOMPARE(saved.items.size(), 1);
        QCOMPARE(contents(path), original);
        ReaderWindow empty;
        activate(empty);
        command(empty, "wq");
        QVERIFY(empty.isVisible());
        QVERIFY(empty.statusBar()->currentMessage().contains("Open a PDF"));
    }

};
QTEST_MAIN(ReaderTests)
#include "reader_tests.moc"
