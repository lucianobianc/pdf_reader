#include "reader_window.h"
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <algorithm>
#include <functional>

ReaderWindow::ReaderWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Vireo — PDF reader"));
    resize(1100, 800);
    canvas_ = new PageCanvas;
    scroll_ = new QScrollArea(this);
    scroll_->setObjectName("pageScroll");
    scroll_->setAlignment(Qt::AlignCenter);
    scroll_->setWidget(canvas_);
    setCentralWidget(scroll_);
    outlineDock_ = new QDockWidget(tr("Document outline"), this);
    outlineDock_->setObjectName("outlineDock");
    outlineTree_ = new QTreeWidget(outlineDock_);
    outlineTree_->setHeaderHidden(true);
    outlineTree_->setMinimumWidth(220);
    outlineDock_->setWidget(outlineTree_);
    addDockWidget(Qt::LeftDockWidgetArea, outlineDock_);
    connect(outlineTree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const int destination = item->data(0, Qt::UserRole).toInt();
        if (destination >= 0) showPage(destination);
    });
    connect(outlineTree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        const int destination = item->data(0, Qt::UserRole).toInt();
        if (destination >= 0) showPage(destination);
    });
    createToolbar();
    command_ = new QLineEdit(this);
    command_->setObjectName("pageCommand");
    command_->setPlaceholderText(tr(":page 123 · :chapter 3 · :c to list chapters · Esc to cancel"));
    command_->hide();
    statusBar()->addPermanentWidget(command_, 1);
    position_ = new QLabel(tr("Open a PDF to start reading"), this);
    statusBar()->addPermanentWidget(position_);
    connect(command_, &QLineEdit::returnPressed, this, &ReaderWindow::executeCommand);
    connect(canvas_, &PageCanvas::annotationCreated, this, &ReaderWindow::addAnnotation);
    connect(canvas_, &PageCanvas::annotationEraseRequested, this, &ReaderWindow::eraseAnnotation);
    connect(canvas_, &PageCanvas::textBoxRequested, this, [this](Annotation box) {
        if (!annotationsWritable_) return;
        bool accepted = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Text box"),
            tr("Text (up to 10,000 characters):"), {}, &accepted);
        if (!accepted || text.trimmed().isEmpty()) return;
        if (text.size() > 10000) {
            QMessageBox::warning(this, tr("Text box"), tr("The text exceeds the 10,000 character limit."));
            return;
        }
        box.text = text;
        addAnnotation(box);
    });
    qApp->installEventFilter(this);
}

void ReaderWindow::createToolbar()
{
    auto *toolbar = addToolBar(tr("Reading"));
    toolbar->setMovable(false);
    auto *open = toolbar->addAction(tr("Open PDF"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open PDF"), {}, tr("PDF documents (*.pdf)"));
        if (!path.isEmpty()) openDocument(path);
    });
    auto *outline = outlineDock_->toggleViewAction();
    outline->setText(tr("Outline [Tab]"));
    toolbar->addAction(outline);
    toolbar->addSeparator();
    connect(toolbar->addAction("−"), &QAction::triggered, this, [this] { setZoom(zoom_ - 0.25); });
    connect(toolbar->addAction("+"), &QAction::triggered, this, [this] { setZoom(zoom_ + 0.25); });
    toolbar->addWidget(new QLabel(tr("  Reading: ")));
    auto *themes = new QComboBox(toolbar);
    themes->setObjectName("readingTheme");
    themes->addItems({tr("Light"), tr("Dark"), tr("Sepia"), tr("Burgundy")});
    toolbar->addWidget(themes);
    connect(themes, &QComboBox::currentIndexChanged, this, [this](int index) {
        canvas_->setTheme(static_cast<PageCanvas::Theme>(index));
        const QColor backgrounds[] = {QColor("#e6e8eb"), QColor("#17191e"), QColor("#d4c3a3"), QColor("#21141d")};
        const QColor foregrounds[] = {QColor("#25272b"), QColor("#dadde3"), QColor("#463627"), QColor("#e4bcc5")};
        QPalette colors = qApp->palette();
        colors.setColor(QPalette::Window, backgrounds[index]);
        colors.setColor(QPalette::Base, backgrounds[index]);
        colors.setColor(QPalette::AlternateBase, backgrounds[index].lighter(110));
        colors.setColor(QPalette::Button, backgrounds[index]);
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            colors.setColor(role, foregrounds[index]);
        setPalette(colors);
        scroll_->viewport()->setPalette(colors);
    });
    helpAction_ = toolbar->addAction(tr("Help [?]"));
    connect(helpAction_, &QAction::triggered, this, [this] {
        QMessageBox::information(this, tr("Using Vireo"),
            tr("hjkl: scroll (j/k turn pages at the edge)\n"
               "J/K: next/previous page · H/L: previous/next chapter\n"
               "d/u: half a screen · gg/G: first/last page\n"
               ":123, :#123, :p 123 or :page 123 + Enter: go to page\n"
               ":c or :chapter: list chapters · :chapter 3: go to chapter\n"
               ":c next / :c prev: next/previous chapter\n"
               "Tab: toggle outline · a: annotation toolbar\n"
               "+/−: zoom · Esc: navigate/cancel · Ctrl+Z: undo the last annotation change\n"
               "\n"
               "Highlight: drag a rectangle. Draw: drag a freehand stroke.\n"
               "Text: drag a box and enter text. Eraser: click or drag over annotations.\n"
               "Annotations are saved to <PDF>.vireo.json. The original PDF is never modified.\n"
               "Reading filters also transform images in the PDF."));
    });
    addToolBarBreak();
    annotationToolbar_ = addToolBar(tr("Annotations"));
    annotationToolbar_->setObjectName("annotationToolbar");
    annotationToolbar_->setMovable(false);
    auto *tools = new QActionGroup(this);
    auto addTool = [this, tools](const QString &label, PageCanvas::Tool tool) {
        auto *action = annotationToolbar_->addAction(label);
        action->setCheckable(true);
        tools->addAction(action);
        connect(action, &QAction::triggered, this, [this, tool] { canvas_->setTool(tool); });
        return action;
    };
    navigateAction_ = addTool(tr("Navigate [Esc]"), PageCanvas::Tool::Navigate);
    navigateAction_->setChecked(true);
    addTool(tr("Highlight"), PageCanvas::Tool::Highlight);
    addTool(tr("Draw"), PageCanvas::Tool::Ink);
    addTool(tr("Text"), PageCanvas::Tool::Text);
    addTool(tr("Eraser"), PageCanvas::Tool::Eraser);
    connect(annotationToolbar_->addAction(tr("Color…")), &QAction::triggered, this, [this] {
        const auto color = QColorDialog::getColor(QColor("#e5b93f"), this, tr("Annotation color"));
        if (color.isValid()) canvas_->setColor(color);
    });
    auto *width = new QComboBox(annotationToolbar_);
    width->addItems({tr("Thin stroke"), tr("Medium stroke"), tr("Thick stroke")});
    width->setCurrentIndex(1);
    annotationToolbar_->addWidget(width);
    connect(width, &QComboBox::currentIndexChanged, this, [this](int index) {
        canvas_->setPenWidth(index == 0 ? 0.002 : index == 1 ? 0.004 : 0.008);
    });
    auto *undoAction = annotationToolbar_->addAction(tr("Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, &ReaderWindow::undo);
    auto *save = annotationToolbar_->addAction(tr("Save"));
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, &ReaderWindow::saveAnnotations);
    auto *toggleAnnotations = annotationToolbar_->toggleViewAction();
    toggleAnnotations->setText(tr("Annotations [a]"));
    toolbar->addAction(toggleAnnotations);
    connect(annotationToolbar_, &QToolBar::visibilityChanged, this, [this](bool visible) {
        if (!visible) navigateAction_->trigger();
    });
}

bool ReaderWindow::openDocument(const QString &path)
{
    if (!confirmUnsavedChanges()) return false;
    QString error;
    if (!document_.open(path, error)) {
        QMessageBox::warning(this, tr("Open PDF"), error);
        return false;
    }
    dirty_ = false;
    history_.clear();
    annotationsWritable_ = annotations_.load(document_.path(), document_.fingerprint(), document_.pageCount(), error);
    annotationToolbar_->setEnabled(annotationsWritable_);
    navigateAction_->trigger();
    if (!annotationsWritable_) QMessageBox::warning(this, tr("Protected annotations"), error);
    chapters_ = document_.chapters();
    populateOutline();
    setWindowTitle(QFileInfo(path).fileName() + " — Vireo");
    canvas_->setAnnotations(annotations_.items);
    showPage(0);
    return true;
}

void ReaderWindow::populateOutline()
{
    outlineTree_->clear();
    std::function<void(const QVector<Poppler::OutlineItem> &, QTreeWidgetItem *)> append;
    append = [&](const auto &items, QTreeWidgetItem *parent) {
        for (const auto &entry : items) {
            auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(outlineTree_);
            item->setText(0, entry.name());
            const auto destination = entry.destination();
            item->setData(0, Qt::UserRole, destination ? destination->pageNumber() - 1 : -1);
            if (entry.hasChildren()) append(entry.children(), item);
        }
    };
    append(document_.outline(), nullptr);
    if (outlineTree_->topLevelItemCount() == 0) {
        auto *empty = new QTreeWidgetItem(outlineTree_);
        empty->setText(0, tr("This PDF has no outline."));
        empty->setData(0, Qt::UserRole, -1);
        empty->setFlags(Qt::NoItemFlags);
    }
    outlineTree_->expandToDepth(0);
}

void ReaderWindow::showPage(int page, bool bottom)
{
    if (page < 0 || page >= document_.pageCount()) return;
    const QImage image = document_.render(page, zoom_);
    if (image.isNull()) {
        statusBar()->showMessage(tr("Could not render this page."), 5000);
        return;
    }
    page_ = page;
    canvas_->setPage(image, page_);
    scroll_->verticalScrollBar()->setValue(bottom ? scroll_->verticalScrollBar()->maximum() : 0);
    // Layout may update the scrollbar range after the canvas changes size.
    QTimer::singleShot(0, this, [this, bottom] {
        scroll_->verticalScrollBar()->setValue(bottom ? scroll_->verticalScrollBar()->maximum() : 0);
    });
    scroll_->setFocus();
    updateStatus();
}

void ReaderWindow::scrollVertical(int amount)
{
    auto *bar = scroll_->verticalScrollBar();
    if (amount > 0 && bar->value() == bar->maximum() && page_ + 1 < document_.pageCount()) showPage(page_ + 1);
    else if (amount < 0 && bar->value() == 0 && page_ > 0) showPage(page_ - 1, true);
    else bar->setValue(bar->value() + amount);
}

void ReaderWindow::setZoom(double zoom)
{
    zoom_ = std::clamp(zoom, 0.5, 3.0);
    showPage(page_);
}

void ReaderWindow::updateStatus()
{
    position_->setText(QString(tr("Page %1 / %2 · %3% · %4"))
        .arg(page_ + 1).arg(document_.pageCount()).arg(qRound(zoom_ * 100))
        .arg(dirty_ ? tr("unsaved annotations") : tr("annotations saved separately")));
}

void ReaderWindow::saveAnnotations()
{
    if (!dirty_ || !annotationsWritable_ || document_.pageCount() == 0) return;
    QString error;
    if (annotations_.save(document_.path(), document_.fingerprint(), error)) dirty_ = false;
    else statusBar()->showMessage(error + tr(" — Press Ctrl+S to retry."));
    updateStatus();
}

bool ReaderWindow::confirmUnsavedChanges()
{
    saveAnnotations();
    if (!dirty_) return true;
    return QMessageBox::warning(this, tr("Unsaved annotations"),
        tr("The annotation file could not be saved. Discard changes?"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}

void ReaderWindow::closeEvent(QCloseEvent *event)
{
    if (confirmUnsavedChanges()) event->accept();
    else event->ignore();
}

void ReaderWindow::addAnnotation(const Annotation &mark)
{
    if (!annotationsWritable_) return;
    history_.append({mark, int(annotations_.items.size()), true});
    annotations_.items.append(mark);
    canvas_->setAnnotations(annotations_.items);
    dirty_ = true;
    saveAnnotations();
}

void ReaderWindow::eraseAnnotation(int index)
{
    if (!annotationsWritable_ || index < 0 || index >= annotations_.items.size()) return;
    history_.append({annotations_.items[index], index, false});
    annotations_.items.removeAt(index);
    canvas_->setAnnotations(annotations_.items);
    dirty_ = true;
    saveAnnotations();
}

void ReaderWindow::undo()
{
    if (!annotationsWritable_ || history_.isEmpty()) return;
    const auto edit = history_.takeLast();
    if (edit.added) annotations_.items.removeAt(edit.index);
    else annotations_.items.insert(edit.index, edit.mark);
    canvas_->setAnnotations(annotations_.items);
    dirty_ = true;
    saveAnnotations();
    if (edit.mark.page != page_) showPage(edit.mark.page);
}

void ReaderWindow::commandError(const QString &message)
{
    position_->setText(message);
    command_->selectAll();
}

void ReaderWindow::showChapterList()
{
    if (chapters_.isEmpty()) {
        commandError(tr("This PDF has no navigable chapters."));
        return;
    }
    command_->hide();
    QDialog dialog(this);
    dialog.setObjectName("chapterDialog");
    dialog.setWindowTitle(tr("Chapters"));
    dialog.resize(520, 360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget(&dialog);
    list->setObjectName("chapterList");
    int current = 0;
    int currentStart = -1;
    for (int i = 0; i < chapters_.size(); ++i) {
        const auto &chapter = chapters_[i];
        list->addItem(tr("%1. %2 — page %3").arg(i + 1).arg(chapter.title).arg(chapter.page + 1));
        if (chapter.page <= page_ && chapter.page > currentStart) {
            current = i;
            currentStart = chapter.page;
        }
    }
    list->setCurrentRow(current);
    layout->addWidget(list);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    list->setFocus();
    if (dialog.exec() == QDialog::Accepted && list->currentRow() >= 0)
        showPage(chapters_[list->currentRow()].page);
    else scroll_->setFocus();
}

bool ReaderWindow::moveChapter(bool forward)
{
    if (chapters_.isEmpty()) {
        position_->setText(tr("This PDF has no navigable chapters."));
        return false;
    }
    int destination = -1;
    for (const auto &chapter : chapters_) {
        // Find the closest chapter start in the requested direction. Duplicate
        // or out-of-order outline destinations must not cause navigation loops.
        if (forward && chapter.page > page_ && (destination < 0 || chapter.page < destination))
            destination = chapter.page;
        if (!forward && chapter.page < page_ && chapter.page > destination)
            destination = chapter.page;
    }
    if (destination < 0) {
        position_->setText(forward ? tr("No next chapter.") : tr("No previous chapter."));
        return false;
    }
    showPage(destination);
    return true;
}

void ReaderWindow::executeCommand()
{
    QString value = command_->text().trimmed();
    if (value.startsWith(':')) value.remove(0, 1);
    const QStringList words = value.simplified().split(' ', Qt::SkipEmptyParts);
    const QString name = words.value(0);
    const QString argument = words.mid(1).join(' ');
    if (name == "c" || name == "chapter") {
        if (argument.isEmpty()) {
            showChapterList();
            return;
        }
        if (argument == "next" || argument == "prev") {
            if (moveChapter(argument == "next")) command_->hide();
            else command_->selectAll();
            return;
        }
        bool valid;
        const int chapter = argument.toInt(&valid);
        if (!valid || chapter < 1 || chapter > chapters_.size()) {
            commandError(chapters_.isEmpty() ? tr("This PDF has no navigable chapters.") :
                tr("Invalid chapter. Enter a number from 1 to %1.").arg(chapters_.size()));
            return;
        }
        command_->hide();
        showPage(chapters_[chapter - 1].page);
        return;
    }
    if (name == "p" || name == "page") value = argument;
    if (value.startsWith('#')) value.remove(0, 1);
    bool valid;
    const int page = value.toInt(&valid);
    if (!valid || page < 1 || page > document_.pageCount()) {
        commandError(tr("Invalid page. Enter a number from 1 to %1.").arg(document_.pageCount()));
        return;
    }
    command_->hide();
    showPage(page - 1);
}

bool ReaderWindow::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() != QEvent::KeyPress || !isActiveWindow() || QApplication::activeModalWidget())
        return QMainWindow::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape) {
        command_->hide();
        navigateAction_->trigger();
        pendingG_.invalidate();
        scroll_->setFocus();
        return true;
    }
    if (qobject_cast<QLineEdit *>(QApplication::focusWidget()) ||
        key->modifiers().testFlag(Qt::ControlModifier) || key->modifiers().testFlag(Qt::AltModifier) ||
        key->modifiers().testFlag(Qt::MetaModifier)) return false;
    const QString text = key->text();
    // Check shifted keys before lowercase commands. Some platforms deliver
    // lowercase event text with Shift, so also inspect the physical key code.
    const auto uppercase = [key, &text](int code, const QString &letter) {
        return text == letter || (key->key() == code && key->modifiers().testFlag(Qt::ShiftModifier));
    };
    if (uppercase(Qt::Key_J, "J") || uppercase(Qt::Key_K, "K")) {
        pendingG_.invalidate();
        showPage(page_ + (uppercase(Qt::Key_J, "J") ? 1 : -1));
        return true;
    }
    if (uppercase(Qt::Key_H, "H") || uppercase(Qt::Key_L, "L")) {
        pendingG_.invalidate();
        moveChapter(uppercase(Qt::Key_L, "L"));
        return true;
    }
    if (uppercase(Qt::Key_G, "G")) {
        pendingG_.invalidate();
        showPage(document_.pageCount() - 1, true);
        return true;
    }
    if (text == "g") {
        if (pendingG_.isValid() && pendingG_.elapsed() < 800) { showPage(0); pendingG_.invalidate(); }
        else pendingG_.start();
        return true;
    }
    pendingG_.invalidate();
    if (text == "j") scrollVertical(48);
    else if (text == "k") scrollVertical(-48);
    else if (text == "d") scrollVertical(scroll_->viewport()->height() / 2);
    else if (text == "u") scrollVertical(-scroll_->viewport()->height() / 2);
    else if (text == "h") scroll_->horizontalScrollBar()->setValue(scroll_->horizontalScrollBar()->value() - 48);
    else if (text == "l") scroll_->horizontalScrollBar()->setValue(scroll_->horizontalScrollBar()->value() + 48);
    else if (text == ":") { command_->setText(":"); command_->show(); command_->setFocus(); }
    else if (text == "+" || text == "=") setZoom(zoom_ + 0.25);
    else if (text == "-") setZoom(zoom_ - 0.25);
    else if (text == "a") annotationToolbar_->setVisible(!annotationToolbar_->isVisible());
    else if (key->key() == Qt::Key_Tab) outlineDock_->setVisible(!outlineDock_->isVisible());
    else if (text == "?") {
        helpAction_->trigger();
    } else return false;
    return true;
}
