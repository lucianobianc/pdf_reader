#pragma once
#include "annotations.h"
#include "document.h"
#include "document_view.h"
#include "icons.h"
#include <QElapsedTimer>
#include <QMainWindow>

class QCloseEvent;
class QDockWidget;
class QLabel;
class QLineEdit;
class QScrollArea;
class QMenu;
class QComboBox;
class QTreeWidget;

class ReaderWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ReaderWindow(QWidget *parent = nullptr);
    bool openDocument(const QString &path);
    int currentPage() const { return page_; } // Zero-based internally; one-based in the UI.
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    void createActions();
    void createAnnotationTools();
    void showAnnotationTools(QPoint globalPosition);
    void applyTheme(int index);
    void updateToolIcons(QColor foreground);
    void openFilePicker();
    void showPage(int page, bool bottom = false);
    void scrollVertical(int amount);
    void setZoom(double zoom);
    void populateOutline();
    void updateStatus();
    void saveAnnotations();
    bool confirmUnsavedChanges();
    void undo();
    void addAnnotation(const Annotation &mark);
    void eraseAnnotation(int index);
    void executeCommand();
    void showChapterList();
    bool moveChapter(bool forward);
    void commandError(const QString &message);
    Document document_;
    AnnotationStore annotations_;
    QVector<Chapter> chapters_;
    DocumentView *view_;
    QScrollArea *scroll_;
    QDockWidget *outlineDock_;
    QTreeWidget *outlineTree_;
    QMenu *annotationMenu_;
    QComboBox *themeSelector_;
    QComboBox *strokeSelector_;
    QVector<QAction *> annotationActions_;
    QVector<QPair<QAction *, ReaderIcon>> iconActions_;
    QLabel *readingModeIcon_;
    QColor annotationColor_ = QColor("#e5b93f");
    int readingTheme_ = 0;
    QLineEdit *command_;
    QLabel *position_;
    QAction *navigateAction_;
    QAction *helpAction_;
    int page_ = 0;
    double zoom_ = 1.0;
    bool annotationsWritable_ = true;
    bool dirty_ = false;
    QElapsedTimer pendingG_;
    struct AnnotationEdit {
        Annotation mark;
        int index;
        bool added;
    };
    QVector<AnnotationEdit> history_;
};
