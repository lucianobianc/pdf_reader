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
#include <QMenu>
#include <QFrame>
#include <QGridLayout>
#include <QToolButton>
#include <QWidgetAction>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QDir>
#include <QTreeWidget>
#include <algorithm>
#include <functional>

ReaderWindow::ReaderWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Nightreader — PDF reader"));
    resize(1100, 800);
    view_ = new DocumentView(this);
    scroll_ = view_;
    setCentralWidget(scroll_);
    outlineDock_ = new QDockWidget(QStringLiteral("Document outline"), this);
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
    createActions();
    createAnnotationTools();
    command_ = new QLineEdit(this);
    command_->setObjectName("pageCommand");
    command_->setPlaceholderText(QStringLiteral(":page 123 · :chapter 3 · :c to list chapters · Esc to cancel"));
    command_->hide();
    statusBar()->addPermanentWidget(command_, 1);
    position_ = new QLabel(QStringLiteral("Open a PDF with :open or Ctrl+O · :help for help"), this);
    statusBar()->addPermanentWidget(position_);
    connect(command_, &QLineEdit::returnPressed, this, &ReaderWindow::executeCommand);
    connect(view_, &DocumentView::annotationCreated, this, &ReaderWindow::addAnnotation);
    connect(view_, &DocumentView::annotationEraseRequested, this, &ReaderWindow::eraseAnnotation);
    connect(view_, &DocumentView::textBoxRequested, this, [this](Annotation box) {
        if (!annotationsWritable_) return;
        bool accepted = false;
        const QString text = QInputDialog::getMultiLineText(this, QStringLiteral("Text box"),
            QStringLiteral("Text (up to 10,000 characters):"), {}, &accepted);
        if (!accepted || text.trimmed().isEmpty()) return;
        if (text.size() > 10000) {
            QMessageBox::warning(this, QStringLiteral("Text box"), QStringLiteral("The text exceeds the 10,000 character limit."));
            return;
        }
        box.text = text;
        addAnnotation(box);
    });
    connect(view_, &DocumentView::currentPageChanged, this, [this](int page) {
        page_ = page;
        updateStatus();
    });
    connect(view_, &DocumentView::renderFailed, this, [this] {
        statusBar()->showMessage(QStringLiteral("Could not render this page."), 5000);
    });
    connect(view_, &DocumentView::annotationToolsRequested, this, &ReaderWindow::showAnnotationTools);
    qApp->installEventFilter(this);
}

void ReaderWindow::createActions()
{
    auto *open = new QAction(QStringLiteral("Open PDF"), this);
    open->setShortcut(QKeySequence::Open);
    addAction(open);
    connect(open, &QAction::triggered, this, &ReaderWindow::openFilePicker);
    helpAction_ = new QAction(QStringLiteral("Help [?]"), this);
    addAction(helpAction_);
    connect(helpAction_, &QAction::triggered, this, [this] {
        annotationMenu_->hide();
        QMessageBox::information(this, QStringLiteral("Using Nightreader"),
            QStringLiteral("hjkl: scroll continuously · J/K: next/previous page\n"
               "H/L: previous/next chapter · d/u: half a screen · gg/G: first/last page\n"
               ":12, :#12, :p 12 or :page 12: go to page\n"
               ":c or :chapter: list chapters · :c 3: go to chapter\n"
               ":c next / :c prev: next/previous chapter\n"
               ":o or :open [path]: open PDF · :q or :quit: close the reader\n"
               ":s/:save or :w/:write: save annotations · :wq: save and quit\n"
               ":h or :help: help · :t or :theme [light|dark|sepia|burgundy]: reading mode\n"
               "t: cycle reading modes · Tab: toggle outline · +/−: zoom\n"
               "Right-click or a: annotation tools · Esc: navigate/cancel\n"
               "Ctrl+Z: undo the last annotation change · Ctrl+S: save annotations\n\n"
               "Highlight: drag a rectangle. Draw: drag a freehand stroke.\n"
               "Text: drag a box and enter text. Eraser: click or drag over annotations.\n"
               "Annotations are saved to <PDF>.nightreader.json. The original PDF is never modified."));
    });
}

void ReaderWindow::createAnnotationTools()
{
    annotationMenu_ = new QMenu(this);
    annotationMenu_->setObjectName("annotationMenu");
    auto *body = new QFrame(annotationMenu_);
    auto *layout = new QGridLayout(body);
    layout->setContentsMargins(8, 8, 8, 8);
    auto addButton = [this, body, layout](QAction *action, ReaderIcon icon, int row, int column) {
        iconActions_.append({action, icon});
        auto *button = new QToolButton(body);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(24, 24));
        button->setMinimumSize(38, 38);
        button->setAccessibleName(action->text());
        layout->addWidget(button, row, column);
    };
    auto *tools = new QActionGroup(this);
    auto addTool = [this, tools, addButton](const QString &label, PageCanvas::Tool tool, int column) {
        auto *action = new QAction(label, this);
        action->setCheckable(true);
        tools->addAction(action);
        addAction(action);
        const ReaderIcon icons[] = {ReaderIcon::Navigate, ReaderIcon::Highlight, ReaderIcon::Draw,
            ReaderIcon::Text, ReaderIcon::Eraser};
        addButton(action, icons[column], 0, column);
        if (tool != PageCanvas::Tool::Navigate) annotationActions_.append(action);
        connect(action, &QAction::triggered, this, [this, tool] {
            annotationMenu_->hide();
            view_->setTool(tool);
            scroll_->setFocus();
        });
        return action;
    };
    navigateAction_ = addTool(QStringLiteral("Navigate [Esc]"), PageCanvas::Tool::Navigate, 0);
    navigateAction_->setChecked(true);
    addTool(QStringLiteral("Highlight"), PageCanvas::Tool::Highlight, 1);
    addTool(QStringLiteral("Draw"), PageCanvas::Tool::Ink, 2);
    addTool(QStringLiteral("Text"), PageCanvas::Tool::Text, 3);
    addTool(QStringLiteral("Eraser"), PageCanvas::Tool::Eraser, 4);
    auto *color = new QAction(QStringLiteral("Color…"), this);
    addButton(color, ReaderIcon::Color, 1, 0);
    annotationActions_.append(color);
    connect(color, &QAction::triggered, this, [this] {
        annotationMenu_->hide();
        const auto selected = QColorDialog::getColor(annotationColor_, this, QStringLiteral("Annotation color"));
        if (selected.isValid()) { annotationColor_ = selected; view_->setColor(selected); }
    });
    strokeSelector_ = new QComboBox(body);
    strokeSelector_->setObjectName("strokeWidth");
    for (const auto &name : {QStringLiteral("Thin stroke"), QStringLiteral("Medium stroke"), QStringLiteral("Thick stroke")}) {
        strokeSelector_->addItem(QString{});
        const int index = strokeSelector_->count() - 1;
        strokeSelector_->setItemData(index, name, Qt::ToolTipRole);
        strokeSelector_->setItemData(index, name, Qt::AccessibleTextRole);
    }
    strokeSelector_->setAccessibleName(QStringLiteral("Stroke thickness"));
    strokeSelector_->setToolTip(QStringLiteral("Stroke thickness"));
    strokeSelector_->setIconSize(QSize(24, 24));
    strokeSelector_->setCurrentIndex(1);
    layout->addWidget(strokeSelector_, 1, 1, 1, 2);
    connect(strokeSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        view_->setPenWidth(index == 0 ? 0.002 : index == 1 ? 0.004 : 0.008);
        const auto name = strokeSelector_->itemData(index, Qt::AccessibleTextRole).toString();
        strokeSelector_->setToolTip(name);
        strokeSelector_->setAccessibleDescription(name);
    });
    auto *undoAction = new QAction(QStringLiteral("Undo"), this);
    undoAction->setShortcut(QKeySequence::Undo);
    addAction(undoAction);
    addButton(undoAction, ReaderIcon::Undo, 1, 3);
    annotationActions_.append(undoAction);
    connect(undoAction, &QAction::triggered, this, [this] { annotationMenu_->hide(); undo(); });
    auto *save = new QAction(QStringLiteral("Save"), this);
    save->setShortcut(QKeySequence::Save);
    addAction(save);
    addButton(save, ReaderIcon::Save, 1, 4);
    annotationActions_.append(save);
    connect(save, &QAction::triggered, this, [this] { annotationMenu_->hide(); saveAnnotations(true); });
    readingModeIcon_ = new QLabel(body);
    readingModeIcon_->setAlignment(Qt::AlignCenter);
    readingModeIcon_->setToolTip(QStringLiteral("Reading mode"));
    layout->addWidget(readingModeIcon_, 2, 0);
    themeSelector_ = new QComboBox(body);
    themeSelector_->setObjectName("readingTheme");
    for (const auto &name : {QStringLiteral("Light"), QStringLiteral("Dark"), QStringLiteral("Sepia"), QStringLiteral("Burgundy")}) {
        themeSelector_->addItem(QString{});
        const int index = themeSelector_->count() - 1;
        themeSelector_->setItemData(index, name, Qt::ToolTipRole);
        themeSelector_->setItemData(index, name, Qt::AccessibleTextRole);
    }
    themeSelector_->setAccessibleName(QStringLiteral("Reading mode"));
    themeSelector_->setToolTip(QStringLiteral("Reading mode"));
    themeSelector_->setIconSize(QSize(24, 24));
    layout->addWidget(themeSelector_, 2, 1, 1, 2);
    connect(themeSelector_, &QComboBox::currentIndexChanged, this, &ReaderWindow::applyTheme);
    auto *outline = outlineDock_->toggleViewAction();
    outline->setText(QStringLiteral("Outline [Tab]"));
    addButton(outline, ReaderIcon::Outline, 2, 3);
    addButton(helpAction_, ReaderIcon::Help, 2, 4);
    auto *contents = new QWidgetAction(annotationMenu_);
    contents->setDefaultWidget(body);
    annotationMenu_->addAction(contents);
    updateToolIcons(palette().color(QPalette::WindowText));
}

void ReaderWindow::showAnnotationTools(QPoint globalPosition)
{
    if (document_.pageCount() > 0) annotationMenu_->popup(globalPosition);
}

void ReaderWindow::applyTheme(int index)
{
    if (index < 0 || index > 3) return;
    readingTheme_ = index;
    view_->setTheme(static_cast<PageCanvas::Theme>(index));
    const QSignalBlocker blocker(themeSelector_);
    themeSelector_->setCurrentIndex(index);
    const auto modeName = themeSelector_->itemData(index, Qt::AccessibleTextRole).toString();
    themeSelector_->setToolTip(modeName);
    themeSelector_->setAccessibleDescription(modeName);
    readingModeIcon_->setToolTip(modeName);
    const QColor backgrounds[] = {QColor("#e6e8eb"), QColor("#17191e"), QColor("#d4c3a3"), QColor("#21141d")};
    const QColor foregrounds[] = {QColor("#25272b"), QColor("#dadde3"), QColor("#463627"), QColor("#e4bcc5")};
    QPalette colors = qApp->palette();
    for (auto role : {QPalette::Window, QPalette::Base, QPalette::Button})
        colors.setColor(role, backgrounds[index]);
    colors.setColor(QPalette::AlternateBase, backgrounds[index].lighter(110));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        colors.setColor(role, foregrounds[index]);
    setPalette(colors);
    annotationMenu_->setPalette(colors);
    scroll_->viewport()->setPalette(colors);
    updateToolIcons(foregrounds[index]);
}

void ReaderWindow::updateToolIcons(QColor foreground)
{
    for (const auto &entry : iconActions_) entry.first->setIcon(readerIcon(entry.second, foreground));
    const ReaderIcon modes[] = {ReaderIcon::Light, ReaderIcon::Dark, ReaderIcon::Sepia, ReaderIcon::Burgundy};
    for (int i = 0; i < 4; ++i) themeSelector_->setItemIcon(i, readerIcon(modes[i], foreground));
    const ReaderIcon strokes[] = {ReaderIcon::ThinStroke, ReaderIcon::MediumStroke, ReaderIcon::ThickStroke};
    for (int i = 0; i < 3; ++i) strokeSelector_->setItemIcon(i, readerIcon(strokes[i], foreground));
    readingModeIcon_->setPixmap(readerIcon(modes[readingTheme_], foreground).pixmap(24, 24));
}

void ReaderWindow::openFilePicker()
{
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Open PDF"), {}, QStringLiteral("PDF documents (*.pdf)"));
    if (!path.isEmpty()) openDocument(path);
}

bool ReaderWindow::openDocument(const QString &path)
{
    if (!confirmUnsavedChanges()) return false;
    QString error;
    if (!document_.open(path, error)) {
        QMessageBox::warning(this, QStringLiteral("Open PDF"), error);
        return false;
    }
    dirty_ = false;
    history_.clear();
    annotationsWritable_ = annotations_.load(document_.path(), document_.fingerprint(), document_.pageCount(), error);
    for (auto *action : annotationActions_) action->setEnabled(annotationsWritable_);
    strokeSelector_->setEnabled(annotationsWritable_);
    navigateAction_->trigger();
    if (!annotationsWritable_) QMessageBox::warning(this, QStringLiteral("Protected annotations"), error);
    chapters_ = document_.chapters();
    populateOutline();
    setWindowTitle(QFileInfo(path).fileName() + " — Nightreader");
    view_->setAnnotations(annotations_.items);
    view_->setDocument(&document_);
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
        empty->setText(0, QStringLiteral("This PDF has no outline."));
        empty->setData(0, Qt::UserRole, -1);
        empty->setFlags(Qt::NoItemFlags);
    }
    outlineTree_->expandToDepth(0);
}

void ReaderWindow::showPage(int page, bool bottom)
{
    view_->goToPage(page, bottom);
}

void ReaderWindow::scrollVertical(int amount)
{
    auto *bar = scroll_->verticalScrollBar();
    bar->setValue(bar->value() + amount);
}

void ReaderWindow::setZoom(double zoom)
{
    zoom_ = std::clamp(zoom, 0.5, 3.0);
    view_->setZoom(zoom_);
    updateStatus();
}

void ReaderWindow::updateStatus()
{
    if (document_.pageCount() == 0) {
        position_->setText(QStringLiteral("Open a PDF with :open or Ctrl+O · :help for help"));
        return;
    }
    position_->setText(QString(QStringLiteral("Page %1 / %2 · %3% · %4"))
        .arg(page_ + 1).arg(document_.pageCount()).arg(qRound(zoom_ * 100))
        .arg(dirty_ ? QStringLiteral("unsaved annotations") : QStringLiteral("annotations saved separately")));
}

bool ReaderWindow::saveAnnotations(bool force)
{
    if (!dirty_ && !force) return true;
    if (document_.pageCount() == 0) {
        statusBar()->showMessage(QStringLiteral("Open a PDF before saving annotations."));
        return false;
    }
    if (!annotationsWritable_) {
        statusBar()->showMessage(QStringLiteral("Annotations cannot be saved for this document."));
        return false;
    }
    QString error;
    const bool saved = annotations_.save(document_.path(), document_.fingerprint(), error);
    if (saved) {
        dirty_ = false;
        statusBar()->clearMessage();
    } else {
        statusBar()->showMessage(error + QStringLiteral(" — Press Ctrl+S or use :write to retry."));
    }
    updateStatus();
    return saved;
}

bool ReaderWindow::confirmUnsavedChanges()
{
    saveAnnotations();
    if (!dirty_) return true;
    return QMessageBox::warning(this, QStringLiteral("Unsaved annotations"),
        QStringLiteral("The annotation file could not be saved. Discard changes?"),
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
    view_->setAnnotations(annotations_.items);
    dirty_ = true;
    saveAnnotations();
}

void ReaderWindow::eraseAnnotation(int index)
{
    if (!annotationsWritable_ || index < 0 || index >= annotations_.items.size()) return;
    history_.append({annotations_.items[index], index, false});
    annotations_.items.removeAt(index);
    view_->setAnnotations(annotations_.items);
    dirty_ = true;
    saveAnnotations();
}

void ReaderWindow::undo()
{
    if (!annotationsWritable_ || history_.isEmpty()) return;
    const auto edit = history_.takeLast();
    if (edit.added) annotations_.items.removeAt(edit.index);
    else annotations_.items.insert(edit.index, edit.mark);
    view_->setAnnotations(annotations_.items);
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
        commandError(QStringLiteral("This PDF has no navigable chapters."));
        return;
    }
    command_->hide();
    QDialog dialog(this);
    dialog.setObjectName("chapterDialog");
    dialog.setWindowTitle(QStringLiteral("Chapters"));
    dialog.resize(520, 360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget(&dialog);
    list->setObjectName("chapterList");
    int current = 0;
    int currentStart = -1;
    for (int i = 0; i < chapters_.size(); ++i) {
        const auto &chapter = chapters_[i];
        list->addItem(QStringLiteral("%1. %2 — page %3").arg(i + 1).arg(chapter.title).arg(chapter.page + 1));
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
        position_->setText(QStringLiteral("This PDF has no navigable chapters."));
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
        position_->setText(forward ? QStringLiteral("No next chapter.") : QStringLiteral("No previous chapter."));
        return false;
    }
    showPage(destination);
    return true;
}

void ReaderWindow::executeCommand()
{
    QString value = command_->text().trimmed();
    if (value.startsWith(':')) value.remove(0, 1);
    value = value.trimmed();
    const qsizetype separator = value.indexOf(QRegularExpression("\\s"));
    const QString name = separator < 0 ? value : value.left(separator);
    const QString argument = separator < 0 ? QString{} : value.mid(separator).trimmed();
    if (name == "o" || name == "open") {
        command_->hide();
        if (argument.isEmpty()) openFilePicker();
        else {
            QString path = argument;
            if (path.size() >= 2 && ((path.startsWith('"') && path.endsWith('"')) ||
                    (path.startsWith('\'') && path.endsWith('\''))))
                path = path.mid(1, path.size() - 2);
            if (path.startsWith("~/")) path.replace(0, 1, QDir::homePath());
            openDocument(path);
        }
        return;
    }
    if (name == "s" || name == "save" || name == "w" || name == "write" || name == "wq") {
        if (!argument.isEmpty()) {
            commandError(QStringLiteral("Usage: :save, :write or :wq (no path)."));
            return;
        }
        if (!saveAnnotations(true)) {
            command_->selectAll();
            return;
        }
        command_->hide();
        if (name == "wq") close();
        else statusBar()->showMessage(QStringLiteral("Annotations saved."), 3000);
        return;
    }
    if (name == "q" || name == "quit") {
        if (!argument.isEmpty()) { commandError(QStringLiteral("Usage: :q or :quit")); return; }
        command_->hide();
        close();
        return;
    }
    if (name == "h" || name == "help") {
        command_->hide();
        helpAction_->trigger();
        return;
    }
    if (name == "t" || name == "theme") {
        const QStringList themes = {"light", "dark", "sepia", "burgundy"};
        const int theme = themes.indexOf(argument);
        if (!argument.isEmpty() && theme < 0) {
            commandError(QStringLiteral("Reading modes: light, dark, sepia, burgundy."));
            return;
        }
        command_->hide();
        applyTheme(argument.isEmpty() ? (readingTheme_ + 1) % 4 : theme);
        return;
    }
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
            commandError(chapters_.isEmpty() ? QStringLiteral("This PDF has no navigable chapters.") :
                QStringLiteral("Invalid chapter. Enter a number from 1 to %1.").arg(chapters_.size()));
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
        commandError(QStringLiteral("Invalid page. Enter a number from 1 to %1.").arg(document_.pageCount()));
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
        if (auto *popup = QApplication::activePopupWidget(); popup && popup != annotationMenu_) return false;
        annotationMenu_->hide();
        command_->hide();
        navigateAction_->trigger();
        pendingG_.invalidate();
        scroll_->setFocus();
        return true;
    }
    if (QApplication::activePopupWidget()) return false;
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
    else if (text == "a") showAnnotationTools(scroll_->viewport()->mapToGlobal(scroll_->viewport()->rect().center()));
    else if (text == "t") applyTheme((readingTheme_ + 1) % 4);
    else if (key->key() == Qt::Key_Tab) outlineDock_->setVisible(!outlineDock_->isVisible());
    else if (text == "?") {
        helpAction_->trigger();
    } else return false;
    return true;
}
