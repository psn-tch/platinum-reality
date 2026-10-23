#include "drop_receiver_window.hpp"
#include "finder/file_drag.hpp"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QPainter>

bool DesktopDropReceiver::event(QEvent *event)
{
    if (event->type() == QEvent::Expose && isExposed())
    {
        m_store.resize(size());
        m_store.beginPaint(QRect(QPoint(), size()));
        QPainter painter(m_store.paintDevice());
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(QRect(QPoint(), size()), Qt::transparent);
        painter.end();
        m_store.endPaint();
        m_store.flush(QRect(QPoint(), size()));
        return true;
    }
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
        event->type() == QEvent::Drop)
    {
        auto *drop = static_cast<QDropEvent *>(event);
        const bool ownedHint = drop->mimeData()->hasFormat(finderDragMime);
        const bool copy =
            !ownedHint || bool(drop->modifiers() & (Qt::AltModifier | Qt::ControlModifier));
        const auto action = copy ? Qt::CopyAction : Qt::MoveAction;
        if (event->type() == QEvent::Drop)
        {
            const bool trace = qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") == "1";
            if (trace)
                qInfo("Drop received: owned=%d actions=%d action=%d urls=%d", ownedHint,
                      int(drop->possibleActions()), int(action), drop->mimeData()->hasUrls());
            const auto bytes = drop->mimeData()->data(finderDragMime);
            const auto token = bytes.size() <= 64 ? QString::fromUtf8(bytes) : QString();
            const auto paths = drop->possibleActions().testFlag(action)
                                   ? fileDragPaths(drop->mimeData())
                                   : QStringList{};
            if (trace)
                qInfo("Drop payload decoded: token bytes=%lld files=%lld", qint64(bytes.size()),
                      qint64(paths.size()));
            // Even a rejected decoded payload ends the pending transport. It
            // never creates a file job; the source still needs its release.
            emit filesDropped(paths, token);
            if (paths.isEmpty())
            {
                drop->ignore();
                return true;
            }
            drop->setDropAction(action);
            drop->accept();
            return true;
        }
        if (!drop->mimeData()->hasUrls() || !drop->possibleActions().testFlag(action))
        {
            drop->ignore();
            return true;
        }
        drop->setDropAction(action);
        drop->accept();
        if (event->type() == QEvent::DragEnter &&
            qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") == "1")
            qInfo("Drop entered: owned=%d actions=%d action=%d", ownedHint,
                  int(drop->possibleActions()), int(action));
        return true;
    }
    if (event->type() == QEvent::DragLeave)
    {
        event->accept();
        return true;
    }
    return QWindow::event(event);
}
DesktopDropReceiver::DesktopDropReceiver()
{
    setSurfaceType(QSurface::RasterSurface);
    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    setTitle("Frankenstein desktop drop endpoint");
    resize(2, 2);
}
