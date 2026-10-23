#include "editor_window.hpp"
#include "finder/platinum_style.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTextEdit>

EditorWindow::EditorWindow(QString file, QWidget *parent)
    : QMainWindow(parent), m_path(std::move(file)), m_text(new QTextEdit)
{
    setObjectName("editorWindow");
    resize(620, 440);
    m_text->setObjectName("documentText");
    m_text->setAcceptRichText(false);
    setCentralWidget(m_text);
    m_text->setFont(QFont("DejaVu Sans Mono", 10));
    if (!m_path.isEmpty())
    {
        QFile input(m_path);
        QString error;
        if (!input.open(QIODevice::ReadOnly)) error = input.errorString();
        else if (input.size() > 16 * 1024 * 1024) error = "This editor supports text files up to 16 MiB.";
        else
        {
            const auto bytes = input.read(16 * 1024 * 1024 + 1);
            QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
            const QString text = decoder.decode(bytes);
            if (bytes.size() > 16 * 1024 * 1024) error = "This editor supports text files up to 16 MiB.";
            else if (input.error() != QFileDevice::NoError) error = input.errorString();
            else if (decoder.hasError() || bytes.contains('\0'))
                error = "This document is not supported UTF-8 text. Open it with an appropriate application.";
            else m_text->setPlainText(text);
        }
        if (!error.isEmpty())
        {
            QMessageBox::warning(this, "Open Document", error);
            m_path.clear();
        }
    }
    auto add = [this](QMenu *menu, QString label, QString name, QKeySequence key, auto fn)
    {
        auto *a = menu->addAction(label);
        a->setObjectName(name);
        setClassicShortcuts(a, key);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    auto *menu = menuBar()->addMenu("File");
    add(menu, "New", "new", QKeySequence::New,
        []
        {
            auto *window = new EditorWindow;
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->show();
        });
    add(menu, "Open…", "open", QKeySequence::Open,
        [this]
        {
            auto path =
                QFileDialog::getOpenFileName(this, "Open", QFileInfo(m_path).absolutePath(), {}, nullptr, QFileDialog::DontUseNativeDialog);
            if (!path.isEmpty())
            {
                auto *w = new EditorWindow(path);
                w->setAttribute(Qt::WA_DeleteOnClose);
                w->show();
            }
        });
    add(menu, "Save", "save", QKeySequence::Save, [this] { save(); });
    add(menu, "Save As…", "saveAs", QKeySequence::SaveAs,
        [this]
        {
            auto path = QFileDialog::getSaveFileName(this, "Save As", m_path, {}, nullptr, QFileDialog::DontUseNativeDialog);
            if (!path.isEmpty())
            {
                auto old = m_path;
                m_path = path;
                if (!save())
                    m_path = old;
                updateTitle();
            }
        });
    menu->addSeparator();
    add(menu, "Close", "close", QKeySequence::Close, [this] { close(); });
    add(menu, "Quit Editor", "quit", QKeySequence::Quit, [] { QApplication::closeAllWindows(); });
    auto *edit = menuBar()->addMenu("Edit");
    add(edit, "Undo", "undo", QKeySequence::Undo, [this] { m_text->undo(); });
    add(edit, "Redo", "redo", QKeySequence::Redo, [this] { m_text->redo(); });
    edit->addSeparator();
    add(edit, "Cut", "cut", QKeySequence::Cut, [this] { m_text->cut(); });
    add(edit, "Copy", "copy", QKeySequence::Copy, [this] { m_text->copy(); });
    add(edit, "Paste", "paste", QKeySequence::Paste, [this] { m_text->paste(); });
    add(edit, "Select All", "selectAll", QKeySequence::SelectAll, [this] { m_text->selectAll(); });
    connect(m_text->document(), &QTextDocument::modificationChanged, this,
            [this] { updateTitle(); });
    updateTitle();
}
bool EditorWindow::save()
{
    if (m_path.isEmpty())
    {
        auto path = QFileDialog::getSaveFileName(this, "Save Document", {}, {}, nullptr, QFileDialog::DontUseNativeDialog);
        if (path.isEmpty())
            return false;
        m_path = path;
    }
    QSaveFile file(m_path);
    const auto bytes = m_text->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        QMessageBox::warning(this, "Save Document", file.errorString());
        return false;
    }
    m_text->document()->setModified(false);
    updateTitle();
    return true;
}
void EditorWindow::updateTitle()
{
    setWindowTitle((m_path.isEmpty() ? "untitled" : QFileInfo(m_path).fileName()) +
                   (m_text->document()->isModified() ? " •" : ""));
}
void EditorWindow::closeEvent(QCloseEvent *event)
{
    if (m_text->document()->isModified())
    {
        auto choice = QMessageBox::question(
            this, "Save Changes?", "Save changes before closing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel || (choice == QMessageBox::Save && !save()))
        {
            event->ignore();
            return;
        }
    }
    event->accept();
}
