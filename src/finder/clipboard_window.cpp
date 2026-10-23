#include "clipboard_window.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

ClipboardWindow::ClipboardWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setObjectName("clipboardWindow");
    setWindowTitle("Clipboard");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(560, 200);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_description = new QLabel(this);
    m_description->setObjectName("clipboardDescription");
    m_description->setContentsMargins(8, 3, 8, 3);
    m_description->setFrameStyle(QFrame::Box | QFrame::Plain);
    m_description->setAutoFillBackground(true);
    auto palette = m_description->palette();
    palette.setColor(QPalette::Window, QColor("#dddddd"));
    m_description->setPalette(palette);
    layout->addWidget(m_description);
    m_pages = new QStackedWidget(this);
    m_text = new QPlainTextEdit(m_pages);
    m_text->setObjectName("clipboardText");
    m_text->setReadOnly(true);
    m_text->document()->setDocumentMargin(14);
    m_pictureArea = new QScrollArea(m_pages);
    m_picture = new QLabel(m_pictureArea);
    m_picture->setObjectName("clipboardPicture");
    m_picture->setMargin(14);
    m_pictureArea->setWidget(m_picture);
    m_pictureArea->setBackgroundRole(QPalette::Base);
    m_pages->addWidget(m_text);
    m_pages->addWidget(m_pictureArea);
    layout->addWidget(m_pages, 1);
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardWindow::refresh);
    refresh();
}

ClipboardWindow *ClipboardWindow::show(QWidget *parent)
{
    static QPointer<ClipboardWindow> window;
    if (!window)
        window = new ClipboardWindow(parent);
    window->QWidget::show();
    window->raise();
    window->activateWindow();
    return window;
}

void ClipboardWindow::refresh()
{
    const auto *data = QGuiApplication::clipboard()->mimeData();
    QImage image;
    if (data && data->hasImage())
        image = qvariant_cast<QImage>(data->imageData());
    if (!image.isNull())
    {
        // A preview, not an editor: keep very large pictures to a sensible size.
        if (image.width() > 2048 || image.height() > 2048)
            image = image.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_kind = "picture";
        m_picture->setPixmap(QPixmap::fromImage(image));
        m_picture->adjustSize();
        m_pages->setCurrentWidget(m_pictureArea);
    }
    else if (data && data->hasText())
    {
        m_kind = "text";
        m_text->setPlainText(data->text().left(256 * 1024));
        m_pages->setCurrentWidget(m_text);
    }
    else
    {
        m_kind = "none";
        m_text->clear();
        m_pages->setCurrentWidget(m_text);
    }
    m_description->setText("Clipboard contents: " + m_kind);
}
