#include "finder_window.hpp"
#include <QStandardPaths>
#include <QLocale>
#include <QTimer>
#include <QPushButton>
#include <QTreeWidget>
#include <QThread>
#include <QDirIterator>
#include "clipboard_window.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include "file_details.hpp"
#include "file_drag.hpp"
#include "platinum_style.hpp"
#include "window_surface_id.hpp"
#include <QActionGroup>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDrag>
#include <QPointingDevice>
#include <QMouseEvent>
#include <QLineEdit>
#include <QSplitter>
#include <QListWidget>
#include <QToolButton>
#include <QProcess>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QStyleOptionHeader>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include "file_labels.hpp"
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QPixmapCache>
#include <QProgressDialog>
#include <QScrollBar>
#include <QShortcut>
#include <QSet>
#include <QStorageInfo>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>

namespace
{
QStyledItemDelegate *makeFolderIconDelegate(QListView *view);
}

FolderIcons::FolderIcons(QWidget *parent) : QListView(parent)
{
    m_spring.opened = [this](const QString &folder) { emit springOpen(folder); };
    setViewMode(QListView::IconMode);
    setIconSize({32, 32});
    setItemDelegate(makeFolderIconDelegate(this));
    setGridSize({96, 78});
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Free);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionRectVisible(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setSpacing(8);
}
namespace
{
// The 9.2.2 item/space band is 21px including its bottom rule. Its inactive
// field/text flatten to #eee/#777 instead of retaining a sunken Qt panel.
class FolderHeader final : public QLabel
{
  public:
    explicit FolderHeader(QWidget *owner) : QLabel(owner)
    {
        setObjectName("folderItemCount");
        setAlignment(Qt::AlignCenter);
        setFixedHeight(21);
        owner->installEventFilter(this);
    }
  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ActivationChange) update();
        return QLabel::eventFilter(watched, event);
    }
    void paintEvent(QPaintEvent *) override
    {
        const bool active = isActiveWindow();
        QPainter painter(this);
        painter.fillRect(rect(), QColor(active ? "#dddddd" : "#eeeeee"));
        if (active) {
            painter.fillRect(0, 0, width(), 1, Qt::white);
            painter.fillRect(0, height()-2, width(), 1, QColor("#aaaaaa"));
        }
        painter.fillRect(0, height()-1, width(), 1, QColor(active ? "#000000" : "#555555"));
        painter.setPen(QColor(active ? "#000000" : "#777777"));
        painter.drawText(rect().adjusted(4, 0, -4, -1), alignment(),
                         fontMetrics().elidedText(text(), Qt::ElideRight, qMax(0, width()-8)));
    }
};
class FolderColumnHeader final : public QHeaderView
{
  public:
    explicit FolderColumnHeader(QWidget *parent) : QHeaderView(Qt::Horizontal,parent)
    {
        setSectionsClickable(true);setSectionsMovable(true);setStretchLastSection(true);
    }
  protected:
    void paintSection(QPainter *painter,const QRect &rect,int logical) const override
    {
        if (!rect.isValid()) return;
        QStyleOptionHeader option;initStyleOptionForIndex(&option,logical);
        const bool active=window()->isActiveWindow();
        const bool down=logical==sortIndicatorSection() || option.state.testFlag(QStyle::State_Sunken);
        painter->save();painter->setClipRect(rect);
        painter->fillRect(rect,QColor(active ? (down ? "#888" : "#ccc") : (down ? "#bbb" : "#ddd")));
        const auto topLeft=QColor(active ? (down ? "#111" : "#666") : (down ? "#777" : "#aaa"));
        const auto bottomRight=QColor(active ? (down ? "#444" : "#333") : "#999");
        painter->fillRect(rect.x(),rect.y(),rect.width()-1,1,topLeft);
        painter->fillRect(rect.x(),rect.y(),1,rect.height()-1,topLeft);
        painter->fillRect(rect.right(),rect.y()+1,1,rect.height()-1,bottomRight);
        painter->fillRect(rect.x()+1,rect.bottom(),rect.width()-1,1,bottomRight);
        const auto corner=QColor(active ? (down ? "#222" : "#555") : (down ? "#888" : "#aaa"));
        painter->fillRect(rect.right(),rect.top(),1,1,corner);
        painter->fillRect(rect.left(),rect.bottom(),1,1,corner);
        if(active){
            const auto light=QColor(down ? "#555" : "#fff");
            const auto shade=QColor(down ? "#aaa" : "#888");
            painter->fillRect(rect.x()+1,rect.y()+1,rect.width()-3,1,light);
            painter->fillRect(rect.x()+1,rect.y()+1,1,rect.height()-3,light);
            painter->fillRect(rect.right()-1,rect.y()+2,1,rect.height()-3,shade);
            painter->fillRect(rect.x()+2,rect.bottom()-1,rect.width()-3,1,shade);
            const auto innerCorner=QColor(down ? "#777" : "#ccc");
            painter->fillRect(rect.right()-1,rect.top()+1,1,1,innerCorner);
            painter->fillRect(rect.left()+1,rect.bottom()-1,1,1,innerCorner);
        }
        // Name follows the disclosure/icon gutter; other metadata starts at6px.
        const int inset=logical==0 ? 44 : 6;
        const bool arrow=isSortIndicatorShown() && logical==sortIndicatorSection();
        const auto textRect=rect.adjusted(inset,2,arrow ? -20 : -6,-2);
        painter->setFont(font());
        painter->setPen(QColor(active ? "#000" : "#777"));
        painter->drawText(textRect,Qt::AlignLeft|Qt::AlignVCenter|Qt::TextSingleLine,
            fontMetrics().elidedText(option.text,Qt::ElideRight,qMax(0,textRect.width())));
        // Preserve the existing sort-direction affordance until the reference's
        // separate reverse-order control is implemented and measured interactively.
        if(arrow){
            option.rect=QRect(rect.right()-13,rect.center().y()-4,9,9);
            style()->drawPrimitive(QStyle::PE_IndicatorHeaderArrow,&option,painter,this);
        }
        painter->restore();
    }
};
QIcon selectedFolderIcon(const QIcon &icon, QSize size, qreal ratio)
{
    // Raster icons can carry explicit state artwork. Named theme engines may
    // report sizes for generated states too; retain our default for those.
    // Require this logical size, so a 32px state cannot leak into the 16px view.
    if (icon.name().isEmpty() && icon.availableSizes(QIcon::Selected).contains(size))
        // Isolate the logical slot before Qt scales for the target display.
        // Requesting at ratio>1 here could choose the other slot's artwork.
        return QIcon(icon.pixmap(size,1.0,QIcon::Selected));
    // Measured list icons, Sherlock and the ordinary folder halve RGB. Some
    // reference artwork uses a different mapping; do not apply it universally
    // without source-family provenance (see docs/fidelity/desktop-icons.md).
    const auto key = QString("platinum-finder-selected-rgb-half-%1-%2x%3-%4")
        .arg(icon.cacheKey()).arg(size.width()).arg(size.height()).arg(ratio);
    QPixmap pixmap;
    if (!QPixmapCache::find(key,&pixmap))
    {
        auto image = icon.pixmap(size,ratio,QIcon::Normal).toImage().convertToFormat(QImage::Format_ARGB32);
        if (image.isNull()) return icon;
        for (int y=0;y<image.height();++y)
        {
            auto *pixels=reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x=0;x<image.width();++x)
            {
                const auto pixel=pixels[x];
                pixels[x]=qRgba(qRed(pixel)/2,qGreen(pixel)/2,qBlue(pixel)/2,qAlpha(pixel));
            }
        }
        pixmap=QPixmap::fromImage(image);
        QPixmapCache::insert(key,pixmap);
    }
    return QIcon(pixmap);
}

class FolderListDelegate final : public QStyledItemDelegate
{
  public:
    explicit FolderListDelegate(QTreeView *view) : QStyledItemDelegate(view), m_view(view) {}
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(19, option.fontMetrics.height()+2));
        return size;
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto styled = option;
        initStyleOption(&styled, index);
        const QColor field(index.column()==m_view->header()->sortIndicatorSection() ? "#dddddd" : "#eeeeee");
        const bool highlight = styled.state.testFlag(QStyle::State_Selected) &&
                               m_view->window()->isActiveWindow() && index.column()==0;
        // Selection remains in the model. The reference paints only the active Name
        // label, leaving metadata and the inactive row in their ordinary fields.
        styled.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);
        styled.backgroundBrush = field;
        if (highlight && !styled.icon.isNull())
            styled.icon = selectedFolderIcon(styled.icon,styled.decorationSize,m_view->devicePixelRatioF());
        auto *style = styled.widget ? styled.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &styled, painter, styled.widget);
        painter->save();
        if (highlight && !styled.text.isEmpty())
        {
            const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, &styled, styled.widget)+1;
            const auto area = style->subElementRect(QStyle::SE_ItemViewItemText, &styled, styled.widget)
                                  .adjusted(margin,0,-margin,0);
            const auto text = styled.fontMetrics.elidedText(styled.text,styled.textElideMode,qMax(0,area.width()));
            const auto label = style->itemTextRect(styled.fontMetrics,area,
                styled.displayAlignment | Qt::TextSingleLine,true,text).adjusted(-1,0,1,0)
                .intersected(option.rect.adjusted(0,0,0,-1));
            // Difference against the measured field turns #ddd into black and black
            // glyphs into #ddd without shifting or redrawing their baseline.
            painter->setCompositionMode(QPainter::CompositionMode_Difference);
            painter->fillRect(label,field);
            painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        painter->fillRect(option.rect.left(), option.rect.bottom(), option.rect.width(), 1, Qt::white);
        painter->restore();
    }
  private:
    QTreeView *m_view;
};
class FolderIconDelegate final : public QStyledItemDelegate
{
  public:
    explicit FolderIconDelegate(QListView *view) : QStyledItemDelegate(view),m_view(view) {}
    void paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index) const override
    {
        auto styled=option;initStyleOption(&styled,index);
        const bool highlight=styled.state.testFlag(QStyle::State_Selected) && m_view->window()->isActiveWindow();
        styled.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);
        styled.backgroundBrush=Qt::white;
        if(highlight && !styled.icon.isNull())
            styled.icon=selectedFolderIcon(styled.icon,styled.decorationSize,m_view->devicePixelRatioF());
        auto *style=styled.widget ? styled.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem,&styled,painter,styled.widget);
        if(highlight && !styled.text.isEmpty()){
            const int margin=style->pixelMetric(QStyle::PM_FocusFrameHMargin,&styled,styled.widget)+1;
            const auto area=style->subElementRect(QStyle::SE_ItemViewItemText,&styled,styled.widget)
                .adjusted(margin,0,-margin,0);
            const auto text=styled.fontMetrics.elidedText(styled.text,styled.textElideMode,qMax(0,area.width()));
            const auto label=style->itemTextRect(styled.fontMetrics,area,
                styled.displayAlignment | Qt::TextSingleLine,true,text).adjusted(-1,0,1,0).intersected(option.rect);
            painter->save();painter->setCompositionMode(QPainter::CompositionMode_Difference);
            painter->fillRect(label,Qt::white);painter->restore();
        }
    }
  private:
    QListView *m_view;
};
QStyledItemDelegate *makeFolderIconDelegate(QListView *view)
{
    return new FolderIconDelegate(view);
}
// A finger held still for 0.6 s on a folder view opens its menu, as a right click does: the
// handheld's touchscreen has no right button (Lily Hewitt, 2026-10-29). Touch arrives here as Qt's
// synthesized mouse events from a touchscreen device; moving more than a few pixels cancels.
class LongPressMenu final : public QObject
{
  public:
    explicit LongPressMenu(QAbstractItemView *view) : QObject(view), m_view(view)
    {
        m_timer.setSingleShot(true);
        m_timer.setInterval(600);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            m_fired = true;
            emit m_view->customContextMenuRequested(m_at);
        });
        view->viewport()->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseMove &&
            event->type() != QEvent::MouseButtonRelease)
            return false;
        auto *mouse = static_cast<QMouseEvent *>(event);
        const bool finger = mouse->pointingDevice() &&
                            mouse->pointingDevice()->type() == QInputDevice::DeviceType::TouchScreen;
        if (event->type() == QEvent::MouseButtonPress)
        {
            m_fired = false;
            if (finger && mouse->button() == Qt::LeftButton)
            {
                m_at = mouse->position().toPoint();
                m_timer.start();
            }
            return false;
        }
        if (event->type() == QEvent::MouseMove)
        {
            if (m_timer.isActive() && (mouse->position().toPoint() - m_at).manhattanLength() > 12)
                m_timer.stop();
            return m_fired; // The menu is open: no drag or rubber band from the same finger.
        }
        m_timer.stop();
        const bool fired = m_fired;
        m_fired = false;
        return fired; // The finger that opened the menu does not also click.
    }

  private:
    QAbstractItemView *m_view;
    QTimer m_timer;
    QPoint m_at;
    bool m_fired = false;
};

// Labelled items (Label menu, FileLabels): the icon tinted with the label's colour, as Mac OS 9
// draws it, and in the list the name in that colour. Labels are read when first shown.
class LabelledFileModel final : public QFileSystemModel
{
  public:
    LabelledFileModel(QString profile, QObject *parent) : QFileSystemModel(parent), m_profile(std::move(profile))
    {
        connect(this, &QFileSystemModel::directoryLoaded, this, [this] { m_labels.clear(); m_icons.clear(); });
    }
    int labelOf(const QString &path) const
    {
        if (const auto known = m_labels.constFind(path); known != m_labels.cend())
            return *known;
        const auto label = FileLabels::label(path, m_profile);
        m_labels.insert(path, label);
        return label;
    }
    void forget(const QString &path)
    {
        m_labels.remove(path);
        m_icons.remove(path);
        const auto at = index(path);
        if (at.isValid())
            emit dataChanged(at, at.siblingAtColumn(columnCount(at.parent()) - 1));
    }
    QVariant data(const QModelIndex &at, int role) const override
    {
        if (at.column() == 0 && (role == Qt::DecorationRole || role == Qt::ForegroundRole))
            if (const auto label = labelOf(filePath(at)); label)
            {
                if (role == Qt::ForegroundRole)
                    return QColor(FileLabels::color(label));
                const auto path = filePath(at);
                if (const auto cached = m_icons.constFind(path); cached != m_icons.cend())
                    return *cached;
                const auto icon = tinted(QFileSystemModel::data(at, role).value<QIcon>(), QColor(FileLabels::color(label)));
                m_icons.insert(path, icon);
                return icon;
            }
        return QFileSystemModel::data(at, role);
    }
    static QIcon tinted(const QIcon &icon, const QColor &color)
    {
        QIcon result;
        for (const int size : {16, 32, 64, 128})
        {
            auto image = icon.pixmap(size, size).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (image.isNull())
                continue;
            const auto original = image;
            QPainter painter(&image);
            // Darken the drawing by the colour, then keep only the drawing's own shape.
            painter.setCompositionMode(QPainter::CompositionMode_Multiply);
            painter.fillRect(image.rect(), color);
            painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            painter.drawImage(0, 0, original);
            painter.end();
            result.addPixmap(QPixmap::fromImage(image));
        }
        return result.isNull() ? icon : result;
    }

  private:
    QString m_profile;
    mutable QHash<QString, int> m_labels;
    mutable QHash<QString, QIcon> m_icons;
};

QPointer<QDrag> activeFileDrag;
void startFileDrag(QAbstractItemView *view, const QModelIndexList &indexes)
{
    auto *model = qobject_cast<QFileSystemModel *>(view->model());
    if (!model)
        return;
    QList<QUrl> urls;
    QSet<QString> seen;
    for (const auto &index : indexes)
    {
        const auto path = model->filePath(index.siblingAtColumn(0));
        if (!seen.contains(path))
        {
            seen.insert(path);
            urls.push_back(QUrl::fromLocalFile(path));
        }
    }
    if (urls.isEmpty() || urls.size() > 128)
        return;
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mime->setData(finderDragMime, token.toUtf8());
    mime->setData(finderFileUrlsMime, mime->data("text/uri-list"));
    QPointer<QDrag> drag = new QDrag(view);
    drag->setProperty("frankensteinFileDragToken", token);
    activeFileDrag = drag;
    drag->setMimeData(mime);
    QPixmap preview(160, 40);
    preview.fill(Qt::transparent);
    QPainter painter(&preview);
    painter.setPen(Qt::black);
    painter.setBrush(QColor(238, 238, 238, 230));
    painter.drawRect(0, 0, 159, 39);
    painter.drawPixmap(4, 4, model->fileIcon(indexes.first()).pixmap(32, 32));
    const auto label = urls.size() == 1 ? QFileInfo(urls.first().toLocalFile()).fileName()
                                        : QString("%1 items").arg(urls.size());
    painter.drawText(QRect(42, 4, 113, 32), Qt::AlignVCenter,
                     painter.fontMetrics().elidedText(label, Qt::ElideRight, 113));
    painter.end();
    drag->setPixmap(preview);
    drag->setHotSpot({12, 12});
    // The destination performs the move. Never delete originals based on the
    // protocol result: the asynchronous file job can still fail or collide.
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
    if (activeFileDrag == drag)
        activeFileDrag.clear();
    if (drag)
        drag->deleteLater();
}
Qt::DropAction fileDropAction(const QDropEvent *event)
{
    const bool copy = event->modifiers().testFlag(Qt::AltModifier) ||
                      event->modifiers().testFlag(Qt::ControlModifier) ||
                      !event->mimeData()->hasFormat(finderDragMime);
    if (copy && event->possibleActions().testFlag(Qt::CopyAction))
        return Qt::CopyAction;
    if (!copy && event->possibleActions().testFlag(Qt::MoveAction))
        return Qt::MoveAction;
    return event->possibleActions().testFlag(Qt::CopyAction) ? Qt::CopyAction : Qt::IgnoreAction;
}
void acceptFileDrag(QDropEvent *event)
{
    const auto action = fileDropAction(event);
    if (action != Qt::IgnoreAction && event->mimeData()->hasUrls())
    {
        event->setDropAction(action);
        event->accept();
    }
    else
        event->ignore();
}
QString dropDirectory(QAbstractItemView *view, QPoint point)
{
    auto *model = qobject_cast<QFileSystemModel *>(view->model());
    const auto index = view->indexAt(point).siblingAtColumn(0);
    return index.isValid() && model->isDir(index) ? model->filePath(index)
                                                  : model->filePath(view->rootIndex());
}
} // namespace
bool FinderWindow::finishFileDrag(const QString &token)
{
    if (token.isEmpty() || token.size() > 64 || !activeFileDrag ||
        activeFileDrag->property("frankensteinFileDragToken").toString() != token)
        return false;
    // The desktop has already decoded the URLs. Older data-device protocols
    // may not notify the source that another process finished its drop. Cancel
    // only this matching transport loop; the destination owns the file job.
    QDrag::cancel();
    return true;
}
void FolderIcons::startDrag(Qt::DropActions)
{
    startFileDrag(this, selectedIndexes());
}
void FolderIcons::dragEnterEvent(QDragEnterEvent *e)
{
    acceptFileDrag(e);
}
SpringLoading::SpringLoading(QAbstractItemView *view) : m_view(view)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(delay);
    QObject::connect(&m_timer, &QTimer::timeout, view, [this]
    {
        auto *model = qobject_cast<QFileSystemModel *>(m_view->model());
        if (!m_target.isValid() || !model || !model->isDir(m_target))
            return;
        const auto path = model->filePath(m_target);
        m_target = QPersistentModelIndex();
        if (opened)
            opened(path);
    });
}
void SpringLoading::hover(QPoint at)
{
    auto *model = qobject_cast<QFileSystemModel *>(m_view->model());
    const auto index = m_view->indexAt(at).siblingAtColumn(0);
    // A folder being dragged does not open under itself.
    const bool dragged = m_view->selectionModel() && m_view->selectionModel()->isSelected(index) && activeFileDrag;
    if (!index.isValid() || !model || !model->isDir(index) || dragged)
    {
        cancel();
        return;
    }
    if (QPersistentModelIndex(index) == m_target && m_timer.isActive())
        return;
    m_target = index;
    m_timer.start();
}
void SpringLoading::cancel()
{
    m_timer.stop();
    m_target = QPersistentModelIndex();
}
void FolderIcons::dragMoveEvent(QDragMoveEvent *e)
{
    acceptFileDrag(e);
    if (e->isAccepted())
        m_spring.hover(e->position().toPoint());
    else
        m_spring.cancel();
}
void FolderIcons::dragLeaveEvent(QDragLeaveEvent *e)
{
    m_spring.cancel();
    QListView::dragLeaveEvent(e);
}
void FolderIcons::dropEvent(QDropEvent *e)
{
    m_spring.cancel();
    auto *model = qobject_cast<QFileSystemModel *>(this->model());
    const auto target = indexAt(e->position().toPoint());
    const bool folder = target.isValid() && model->isDir(target);
    if (e->source() == this && !folder &&
        !(e->modifiers() & (Qt::AltModifier | Qt::ControlModifier)))
    {
        QPoint position = e->position().toPoint() +
                          QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
        for (const auto &index : selectedIndexes())
        {
            if (index.column() != 0)
                continue;
            setPositionForIndex(position, index);
            position += QPoint(15, 15);
        }
        emit placementChanged();
        e->setDropAction(Qt::MoveAction);
        e->accept();
        return;
    }
    const auto paths = fileDragPaths(e->mimeData());
    if (paths.isEmpty())
    {
        e->ignore();
        return;
    }
    acceptFileDrag(e);
    if (e->isAccepted())
        emit filesDropped(paths, dropDirectory(this, e->position().toPoint()),
                          e->dropAction() == Qt::CopyAction);
}
FolderList::FolderList(QWidget *parent) : QTreeView(parent)
{
    setHeader(new FolderColumnHeader(this));
    m_spring.opened = [this](const QString &folder) { emit springOpen(folder); };
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::MoveAction);
    setIconSize({16,16});
    setUniformRowHeights(true);
    setItemDelegate(new FolderListDelegate(this));
    header()->setFixedHeight(qMax(21, fontMetrics().height()+4));
    connect(header(), &QHeaderView::sortIndicatorChanged, viewport(), qOverload<>(&QWidget::update));
}
void FolderList::drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const
{
    const QColor field(header()->sortIndicatorSection()==0 ? "#dddddd" : "#eeeeee");
    painter->fillRect(rect, field);
    QTreeView::drawBranches(painter, rect, index);
    painter->save();
    if (model() && (model()->hasChildren(index) || model()->canFetchMore(index)) &&
        (rootIsDecorated() || index.parent()!=rootIndex()) && rect.width()>=indentation())
    {
        // Measured blue triangles, including their one-pixel gray shadows.
        // Keep Qt's branch hit testing, indentation and expansion logic.
        painter->fillRect(rect.right()-indentation()+1,rect.top(),indentation(),rect.height()-1,field);
        const bool expanded=isExpanded(index);
        const QPoint origin(rect.right()-indentation()/2-(expanded ? 5 : 1),
                            rect.center().y()-(expanded ? 2 : 6));
        static constexpr const char *closedRows[]={"K......","KK.....","KLK....","KLMK...",
            "KLMMK..","KLMMDK.","KLMDKSs","KLDKSs.","KMKSs..","KKSs...","KSs....",".s....."};
        static constexpr const char *expandedRows[]={"KKKKKKKKKKK.",".KLLLLLLMKSs",
            "..KMMMMDKSs.","...KMMDKSs..","....KDKSs...",".....KSs....","......s....."};
        const auto rows=expanded ? expandedRows : closedRows;
        for (int y=0;y<(expanded ? 7 : 12);++y)for (int x=0;x<(expanded ? 12 : 7);++x)
        {
            QColor color;
            switch(rows[y][x]){
            case 'K':color=Qt::black;break;
            case 'L':color=QColor("#ccccff");break;
            case 'M':color=QColor("#9999ff");break;
            case 'D':color=QColor("#6666cc");break;
            case 'S':color=QColor("#888888");break;
            case 's':color=QColor("#bbbbbb");break;
            default:continue;
            }
            painter->fillRect(origin.x()+x,origin.y()+y,1,1,color);
        }
    }
    painter->fillRect(rect.left(), rect.bottom(), rect.width(), 1, Qt::white);
    painter->restore();
}
void FolderList::startDrag(Qt::DropActions)
{
    startFileDrag(this, selectedIndexes());
}
void FolderList::dragEnterEvent(QDragEnterEvent *e)
{
    acceptFileDrag(e);
}
void FolderList::dragMoveEvent(QDragMoveEvent *e)
{
    acceptFileDrag(e);
    if (e->isAccepted())
        m_spring.hover(e->position().toPoint());
    else
        m_spring.cancel();
}
void FolderList::dragLeaveEvent(QDragLeaveEvent *e)
{
    m_spring.cancel();
    QTreeView::dragLeaveEvent(e);
}
void FolderList::dropEvent(QDropEvent *e)
{
    m_spring.cancel();
    const auto paths = fileDragPaths(e->mimeData());
    if (paths.isEmpty())
    {
        e->ignore();
        return;
    }
    acceptFileDrag(e);
    if (e->isAccepted())
        emit filesDropped(paths, dropDirectory(this, e->position().toPoint()),
                          e->dropAction() == Qt::CopyAction);
}
FinderWindow::FinderWindow(QString root, QString profile, QString directory, QWidget *parent)
    : QMainWindow(parent), m_root(std::move(root)), m_profile(std::move(profile)),
      m_directory(QDir::cleanPath(std::move(directory))),
      m_operations(m_root, FinderPlaces::trashFor(m_profile), FinderPlaces::volumes), m_model(new LabelledFileModel(m_profile, this)),
      m_icons(new FolderIcons), m_list(new FolderList), m_views(new QStackedWidget)
{
    setAttribute(Qt::WA_DeleteOnClose);
    updateTitle();
    setObjectName("finderWindow");
    QDir().mkpath(m_profile);
    m_settings = std::make_unique<QSettings>(m_profile + "/finder.ini", QSettings::IniFormat);
    m_stateKey = QString::fromLatin1(
        QCryptographicHash::hash(m_directory.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_settings->beginGroup(m_stateKey);
    resize(640, 430);
    restoreGeometry(m_settings->value("geometry").toByteArray());
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    // Navigation, as in Dolphin: Back, Forward, Enclosing Folder and the folder's path (type a path
    // and press Return to go there). Then Mac OS 9's folder window header, "19 items, 47.32 GB
    // available", centred (docs/historical-fidelity-review.md F05).
    auto *navigation = new QHBoxLayout;
    navigation->setContentsMargins(4, 3, 4, 3);
    navigation->setSpacing(3);
    const auto arrowButton = [&](Qt::ArrowType arrow, const char *name, const QString &tip)
    {
        auto *button = new QToolButton;
        button->setArrowType(arrow);
        button->setObjectName(name);
        button->setToolTip(tip);
        button->setAutoRaise(false);
        navigation->addWidget(button);
        return button;
    };
    m_backButton = arrowButton(Qt::LeftArrow, "backButton", "Back");
    m_forwardButton = arrowButton(Qt::RightArrow, "forwardButton", "Forward");
    m_upButton = arrowButton(Qt::UpArrow, "upButton", "Enclosing Folder");
    m_path = new QLineEdit;
    m_path->setObjectName("pathField");
    navigation->addWidget(m_path, 1);
    layout->addLayout(navigation);
    m_header = new FolderHeader(this);
    layout->addWidget(m_header);
    updateHeader();
    installPlatinumIcons(m_model);
    m_model->setReadOnly(true);
    m_model->setFilter(QSettings(m_profile + "/finder.ini", QSettings::IniFormat).value("showHiddenFiles", false).toBool()
                           ? QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                           : QDir::AllEntries | QDir::NoDotAndDotDot);
    m_model->setRootPath(m_directory);
    m_icons->setObjectName("iconView");
    m_list->setObjectName("listView");
    // A 9.2.2 Finder window always shows both scroll bars, idle when there is nothing to scroll.
    for (QAbstractScrollArea *view : {static_cast<QAbstractScrollArea *>(m_icons), static_cast<QAbstractScrollArea *>(m_list)})
    {
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }
    m_icons->setModel(m_model);
    m_list->setModel(m_model);
    const auto rootIndex = m_model->index(m_directory);
    m_icons->setRootIndex(rootIndex);
    m_list->setRootIndex(rootIndex);
    const auto updateCount = [this] { updateHeader(); };
    connect(m_model, &QFileSystemModel::rowsInserted, this, updateCount);
    connect(m_model, &QFileSystemModel::rowsRemoved, this, updateCount);
    connect(m_model, &QFileSystemModel::modelReset, this, updateCount);
    connect(m_model, &QFileSystemModel::directoryLoaded, this,
            [this, updateCount](const QString &path)
            {
                if (path == m_directory)
                    updateCount();
            });
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSortingEnabled(true);
    m_list->sortByColumn(0, Qt::AscendingOrder);
    m_list->setColumnWidth(0, 214);
    // Name, Date Modified, then the remaining modern filesystem metadata.
    m_list->header()->moveSection(m_list->header()->visualIndex(3), 1);
    m_views->addWidget(m_icons);
    m_views->addWidget(m_list);
    // Places, as in Dolphin's sidebar: the home disk and its standard folders, drives, Trash.
    m_places = new QListWidget;
    m_places->setObjectName("placesList");
    m_places->setIconSize(QSize(16, 16));
    m_places->setMinimumWidth(110);
    m_places->setFrameShape(QFrame::NoFrame);
    connect(m_places, &QListWidget::itemClicked, this, [this](QListWidgetItem *item)
            { navigate(item->data(Qt::UserRole).toString()); });
    auto *split = new QSplitter;
    split->setObjectName("placesSplitter");
    split->addWidget(m_places);
    split->addWidget(m_views);
    split->setStretchFactor(1, 1);
    split->setSizes({140, 500});
    split->setChildrenCollapsible(false);
    layout->addWidget(split, 1); // The folder takes the height, not the header.
    updatePlaces();
    connect(m_backButton, &QToolButton::clicked, this, [this] {
        if (!m_back.isEmpty()) { m_forward.prepend(m_directory); navigate(m_back.takeLast(), false); }
    });
    connect(m_forwardButton, &QToolButton::clicked, this, [this] {
        if (!m_forward.isEmpty()) { m_back.append(m_directory); navigate(m_forward.takeFirst(), false); }
    });
    connect(m_upButton, &QToolButton::clicked, this, [this] { navigate(QFileInfo(m_directory).absolutePath()); });
    connect(m_path, &QLineEdit::returnPressed, this, [this] {
        auto path = m_path->text().trimmed();
        if (path == "~" || path.startsWith("~/"))
            path = QDir::homePath() + path.mid(1);
        if (!navigate(QDir(m_directory).absoluteFilePath(path)))
            m_path->setText(QDir::toNativeSeparators(m_directory));
    });
    setCentralWidget(content);
    setViewMode(m_settings->value("list", false).toBool());
    m_path->setText(QDir::toNativeSeparators(m_directory));
    m_backButton->setEnabled(false);
    m_forwardButton->setEnabled(false);
    m_upButton->setEnabled(m_directory != "/" && m_operations.readableDirectory(QFileInfo(m_directory).absolutePath()));
    new LongPressMenu(m_icons);
    new LongPressMenu(m_list);
    connect(m_icons, &QListView::doubleClicked, this, &FinderWindow::openIndex);
    connect(m_list, &QTreeView::doubleClicked, this, &FinderWindow::openIndex);
    connect(m_model, &QFileSystemModel::directoryLoaded, this,
            [this](const QString &path)
            {
                if (path == m_directory)
                {
                    m_directoryReady = true;
                    restorePositions();
                }
            });
    connect(m_icons, &FolderIcons::placementChanged, this, &FinderWindow::saveState);
    const auto receiveFiles = [this](QStringList paths, QString destination, bool copy)
    {
        if (paths.isEmpty())
            return;
        // To another disk a plain drag copies (the flash drive keeps its files), as in Mac OS.
        copy = copy || !FileOperations::sameVolume(paths, destination);
        const auto ops = m_operations;
        auto bytes = copy ? std::make_shared<CopyProgress>() : std::shared_ptr<CopyProgress>();
        auto cancel = std::make_shared<std::atomic_bool>(false);
        perform(copy ? "Copying…" : "Moving…",
                [ops, paths, destination, copy, bytes, cancel]
                {
                    if (bytes)
                        bytes->total.store(FileOperations::measure(paths));
                    FileResult result{true, {}, {}};
                    int completed = 0;
                    for (const auto &path : paths)
                    {
                        result = copy ? ops.copy(path, destination, cancel, bytes) : ops.move(path, destination);
                        if (!result.ok)
                        {
                            result.error = QString("%1 of %2 items completed.\n\n")
                                               .arg(completed)
                                               .arg(paths.size()) +
                                           result.error;
                            break;
                        }
                        ++completed;
                    }
                    return result;
                }, bytes, cancel);
    };
    m_receiveFiles = receiveFiles;
    connect(m_icons, &FolderIcons::filesDropped, this, receiveFiles);
    connect(m_list, &FolderList::filesDropped, this, receiveFiles);
    // A spring-loaded folder opens in this window, as a click on it does.
    const auto spring = [this](const QString &folder)
    {
        if (m_operations.readableDirectory(folder))
            navigate(folder);
    };
    connect(m_icons, &FolderIcons::springOpen, this, spring);
    connect(m_list, &FolderList::springOpen, this, spring);
    // Paste follows the clipboard (files copied here, in another Finder or another file manager).
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &FinderWindow::updateActions);
    auto action = [this](QMenu *menu, QString text, QString name, QKeySequence shortcut, auto fn)
    {
        auto *a = menu->addAction(text);
        a->setObjectName(name);
        setClassicShortcuts(a, shortcut);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    auto *file = menuBar()->addMenu("File");
    action(file, "New Folder", "newFolder", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
           [this]
           {
               bool ok = false;
               auto name = QInputDialog::getText(this, "New Folder", "Name:", QLineEdit::Normal,
                                                 "untitled folder", &ok);
               if (ok)
               {
                   auto ops = m_operations;
                   auto dir = m_directory;
                   perform("Creating folder…",
                           [ops, dir, name] { return ops.createFolder(dir, name); });
               }
           });
    auto *openAction = action(file, "Open", "open", QKeySequence(Qt::CTRL | Qt::Key_O),
           [this]
           {
               const auto paths = selectedPaths();
               for (const auto &p : paths)
                   if (paths.size() == 1 || !QFileInfo(p).isDir())
                       openIndex(m_model->index(p));
           });
    openAction->setShortcuts(openAction->shortcuts() << QKeySequence(Qt::Key_Return) << QKeySequence(Qt::Key_Enter)
                                         << QKeySequence(Qt::CTRL | Qt::Key_Down));
    action(file, "Close Window", "closeWindow", QKeySequence(Qt::CTRL | Qt::Key_W),
           [this] { close(); });
    action(file, "Open With…", "openWith", {},
           [this]
           {
               const auto paths = selectedPaths();
               if (paths.size() == 1 && m_operations.readableFile(paths.first()))
                   emit openDocument(paths.first(), true);
           });
    file->addSeparator();
    action(
        file, "Get Info…", "getInfo", QKeySequence(Qt::CTRL | Qt::Key_I),
        [this]
        {
            const auto paths = selectedPaths();
            if (paths.size() == 1)
                showInfo(paths.first());
        });
    // Label ▸ None and the seven colours, each with its swatch (Mac OS 9's File ▸ Label).
    auto *labels = file->addMenu("Label");
    labels->setObjectName("labelMenu");
    labels->menuAction()->setObjectName("label");
    const auto addLabel = [this, labels](int number, const QString &name)
    {
        auto *a = labels->addAction(name);
        a->setObjectName("label" + QString::number(number));
        a->setCheckable(true);
        if (number)
        {
            QPixmap swatch(12, 12);
            swatch.fill(QColor(FileLabels::color(number)));
            QPainter frame(&swatch);
            frame.setPen(Qt::black);
            frame.drawRect(0, 0, 11, 11);
            frame.end();
            a->setIcon(QIcon(swatch));
        }
        connect(a, &QAction::triggered, this, [this, number] { setLabel(number); });
    };
    addLabel(0, "None");
    labels->addSeparator();
    for (const auto &label : FileLabels::labels())
        addLabel(label.index, label.name);
    // The current label is ticked when every selected item has it.
    connect(labels, &QMenu::aboutToShow, this, [this, labels]
    {
        const auto paths = selectedPaths();
        int shared = -1;
        for (const auto &path : paths)
        {
            const auto label = static_cast<LabelledFileModel *>(m_model)->labelOf(path);
            shared = shared == -1 || shared == label ? label : -2;
        }
        for (int n = 0; n <= 7; ++n)
            if (auto *a = labels->findChild<QAction *>("label" + QString::number(n)))
                a->setChecked(n == shared);
    });
    action(file, "Duplicate", "duplicate", QKeySequence(Qt::CTRL | Qt::Key_D),
           [this]
           {
               auto p = selectedPaths();
               if (p.size() == 1)
               {
                   auto ops = m_operations;
                   perform("Duplicating…", [ops, path = p.first()] { return ops.duplicate(path); });
               }
           });
    action(file, "Make Alias", "makeAlias", QKeySequence(Qt::CTRL | Qt::Key_M),
           [this]
           {
               const auto p = selectedPaths();
               if (p.isEmpty())
                   return;
               auto ops = m_operations;
               perform("Making alias…", [ops, p] {
                   FileResult r{true, {}, {}};
                   for (const auto &path : p)
                       if (!(r = ops.makeAlias(path)).ok)
                           break;
                   return r;
               });
           });
    action(file, "New Text File", "newFile", {},
           [this]
           {
               auto ops = m_operations;
               perform("Creating file…", [ops, dir = m_directory] { return ops.createFile(dir, "untitled.txt"); });
           });
    action(file, "Open in Terminal", "openTerminal", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T),
           [this]
           {
               // The selected folder, else this window's folder.
               const auto p = selectedPaths();
               const auto folder = p.size() == 1 && QFileInfo(p.first()).isDir() ? p.first() : m_directory;
               QProcess::startDetached("konsole", {"--separate", "--workdir", folder});
           });
    action(file, "Find…", "find", QKeySequence(Qt::CTRL | Qt::Key_F),
           [this]
           {
               bool ok = false;
               const auto text = QInputDialog::getText(this, "Find", "Find items whose name contains:",
                                                       QLineEdit::Normal, {}, &ok).trimmed();
               if (ok && !text.isEmpty())
                   findItems(text);
           });
    action(file, "Rename…", "rename", QKeySequence(Qt::Key_F2),
           [this]
           {
               auto p = selectedPaths();
               if (p.size() != 1)
                   return;
               bool ok = false;
               auto name = QInputDialog::getText(this, "Rename", "Name:", QLineEdit::Normal,
                                                 QFileInfo(p.first()).fileName(), &ok);
               if (ok)
               {
                   auto ops = m_operations;
                   perform("Renaming…",
                           [ops, path = p.first(), name] { return ops.rename(path, name); });
               }
           });
    file->addSeparator();
    auto *trashAction = action(file, "Move to Trash", "trash", QKeySequence(Qt::CTRL | Qt::Key_Backspace),
           [this]
           {
               auto p = selectedPaths();
               if (p.isEmpty())
                   return;
               auto ops = m_operations;
               perform("Moving to Trash…",
                       [ops, p]
                       {
                           FileResult r{true, {}, {}};
                           for (auto path : p)
                           {
                               r = ops.trash(path);
                               if (!r.ok)
                                   break;
                           }
                           return r;
                       });
           });
    // Delete, as in Dolphin and other Linux file managers, besides Command-Delete.
    trashAction->setShortcuts(trashAction->shortcuts() << QKeySequence(Qt::Key_Delete));
    action(file, "Put Away", "restore", QKeySequence(Qt::CTRL | Qt::Key_Y),
           [this]
           {
               auto p = selectedPaths();
               if (p.size() != 1)
                   return;
               auto ops = m_operations;
               perform("Putting away…", [ops, path = p.first()] { return ops.restore(path); });
           });
    // Mac OS 9's Finder has no Quit command (docs/historical-fidelity-review.md F07).
    // Edit as in the 9.2.2 Finder (checked in Lily Hewitt's 9.2.2 VM): with items selected, Copy copies
    // their names as text, one per line in display order. Native adaptation: the items themselves
    // travel too (text/uri-list, and the GNOME and KDE copy/cut markers), so Cut, Copy and Paste
    // work with other file managers; Paste copies, or after Cut moves, into this folder.
    auto *edit = menuBar()->addMenu("Edit");
    const auto unavailable = [&](QString text, QString name, QKeySequence shortcut)
    {
        auto *a = action(edit, text, name, shortcut, [] {});
        a->setEnabled(false);
    };
    unavailable("Undo", "undo", QKeySequence::Undo);
    edit->addSeparator();
    const auto putOnClipboard = [this](bool cut)
    {
        const auto paths = selectedPaths();
        if (paths.isEmpty())
            return;
        auto *mime = new QMimeData;
        QList<QUrl> urls;
        QByteArray gnome(cut ? "cut" : "copy");
        for (const auto &path : paths)
        {
            urls << QUrl::fromLocalFile(path);
            gnome += "\n" + QUrl::fromLocalFile(path).toEncoded();
        }
        mime->setUrls(urls);
        mime->setText(selectedNames().join('\n'));
        mime->setData("x-special/gnome-copied-files", gnome);
        mime->setData("application/x-kde-cutselection", cut ? "1" : "0");
        QGuiApplication::clipboard()->setMimeData(mime);
    };
    action(edit, "Cut", "cut", QKeySequence::Cut, [putOnClipboard] { putOnClipboard(true); });
    action(edit, "Copy", "copy", QKeySequence::Copy, [putOnClipboard] { putOnClipboard(false); });
    action(edit, "Paste", "paste", QKeySequence::Paste, [this] { pasteInto(m_directory); });
    action(edit, "Copy Path", "copyPath", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C),
           [this]
           {
               const auto paths = selectedPaths();
               QGuiApplication::clipboard()->setText(paths.isEmpty() ? m_directory : paths.join('\n'));
           });
    unavailable("Clear", "clear", {});
    action(edit, "Select All", "selectAll", QKeySequence::SelectAll,
           [this]
           {
               if (m_views->currentIndex() == 0)
                   m_icons->selectAll();
               else
                   m_list->selectAll();
           });
    action(edit, "Show Clipboard", "showClipboard", {}, [this] { ClipboardWindow::show(this); });
    auto *view = menuBar()->addMenu("View");
    action(view, "as Icons", "icons", {}, [this] { setViewMode(false); });
    action(view, "as List", "list", {}, [this] { setViewMode(true); });
    // as Window / as Pop-up Window (9.2.2): the desktop draws the window, so it makes the change.
    // Only inside Platinum Reality's own session, where that desktop is there to ask.
    if (qEnvironmentVariable("FRANKENSTEIN_ISOLATED") == "1")
    {
        view->addSeparator();
        auto *kinds = new QActionGroup(view);
        for (const bool popUp : {false, true})
        {
            auto *kind = action(view, popUp ? "as Pop-up Window" : "as Window",
                                popUp ? "asPopUpWindow" : "asWindow", {},
                                [this, popUp] { askDesktopPopUp("SetPopUpWindow", popUp); });
            kind->setCheckable(true);
            kind->setChecked(!popUp);
            kinds->addAction(kind);
        }
        connect(view, &QMenu::aboutToShow, this, [this] { askDesktopPopUp("IsPopUpWindow", false); });
    }
    view->addSeparator();
    auto *hidden = action(view, "Show Hidden Files", "showHidden", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Period),
                          [this] { setShowHidden(!(m_model->filter() & QDir::Hidden)); });
    hidden->setCheckable(true);
    hidden->setChecked(m_model->filter() & QDir::Hidden);
    view->addSeparator();
    action(view, "Clean Up", "cleanUp", {},
           [this]
           {
               m_icons->setMovement(QListView::Static);
               m_icons->doItemsLayout();
               m_icons->setMovement(QListView::Free);
               saveState();
           });
    // Go: a native adaptation (Mac OS 9 reached places through the desktop, the Apple menu and
    // Favorites). The user's standard folders and mounted drives, and a typed path, each opened
    // in its own window only when it is readable inside the places this Finder may work in.
    auto *go = menuBar()->addMenu("Go");
    const auto place = [this](const QString &path)
    {
        if (!navigate(path))
            QMessageBox::information(this, "Go", "“" + QDir::toNativeSeparators(path) +
                                                     "” is not available.");
    };
    action(go, FinderPlaces::diskName(m_profile), "goHome", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H),
           [this, place] { place(m_root); });
    action(go, "Desktop", "goDesktop", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D),
           [this, place] { place(m_root + "/Desktop"); });
    const QList<std::pair<QString, QStandardPaths::StandardLocation>> standard{
        {"Documents", QStandardPaths::DocumentsLocation}, {"Downloads", QStandardPaths::DownloadLocation},
        {"Pictures", QStandardPaths::PicturesLocation}, {"Music", QStandardPaths::MusicLocation},
        {"Videos", QStandardPaths::MoviesLocation}};
    QList<QAction *> folders;
    for (const auto &[label, location] : standard)
        folders << action(go, label, "go" + label, {}, [place, location]
                          { place(QStandardPaths::writableLocation(location)); });
    auto *drivesStart = go->addSeparator();
    auto *drivesEnd = go->addSeparator();
    action(go, "Go to Folder…", "goToFolder", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G),
           [this, place]
           {
               bool ok = false;
               auto path = QInputDialog::getText(this, "Go to Folder", "Folder:", QLineEdit::Normal,
                                                 QDir::toNativeSeparators(m_directory), &ok).trimmed();
               if (!ok || path.isEmpty())
                   return;
               if (path == "~" || path.startsWith("~/"))
                   path = QDir::homePath() + path.mid(1);
               place(QDir(m_directory).absoluteFilePath(path));
           });
    // Folders that are not there are hidden; drives are listed as they are mounted now.
    connect(go, &QMenu::aboutToShow, this,
            [this, go, folders, standard, drivesStart, drivesEnd, place]
            {
                for (qsizetype i = 0; i < folders.size(); ++i)
                    folders[i]->setVisible(
                        m_operations.readableDirectory(QStandardPaths::writableLocation(standard[i].second)));
                const auto actions = go->actions();
                for (auto i = actions.indexOf(drivesStart) + 1; i < actions.indexOf(drivesEnd); ++i)
                {
                    go->removeAction(actions[i]);
                    actions[i]->deleteLater();
                }
                QList<QAction *> drives;
                for (const auto &volumes : FinderPlaces::volumes)
                    for (const auto &drive : QDir(volumes).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
                        if (drives.size() < 32 && m_operations.readableDirectory(drive.absoluteFilePath()))
                        {
                            auto *entry = new QAction(drive.fileName(), go);
                            entry->setObjectName("goDrive");
                            connect(entry, &QAction::triggered, this, [place, path = drive.absoluteFilePath()] { place(path); });
                            drives << entry;
                        }
                go->insertActions(drivesEnd, drives);
                drivesEnd->setVisible(!drives.isEmpty());
            });
    auto *window = menuBar()->addMenu("Window");
    action(window, "Open Parent", "parent", QKeySequence(Qt::CTRL | Qt::Key_Up),
           [this] { navigate(QFileInfo(m_directory).absolutePath()); });
    auto *back = action(go, "Back", "back", QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), [this] { m_backButton->click(); });
    back->setShortcuts(back->shortcuts() << QKeySequence(Qt::ALT | Qt::Key_Left) << QKeySequence(Qt::Key_Backspace));
    auto *forward = action(go, "Forward", "forward", QKeySequence(Qt::CTRL | Qt::Key_BracketRight), [this] { m_forwardButton->click(); });
    forward->setShortcuts(forward->shortcuts() << QKeySequence(Qt::ALT | Qt::Key_Right));
    findChild<QAction *>("parent")->setShortcuts(findChild<QAction *>("parent")->shortcuts() << QKeySequence(Qt::ALT | Qt::Key_Up));
    action(window, "Open in New Window", "openNewWindow", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_O),
           [this]
           {
               const auto p = selectedPaths();
               emit openFolder(p.size() == 1 && QFileInfo(p.first()).isDir() ? p.first() : m_directory);
           });
    // The open Finder windows, rebuilt each time the menu opens (Mac OS 9.1 Window menu).
    auto *windowList = window->addSeparator();
    connect(window, &QMenu::aboutToShow, this,
            [this, window, windowList]
            {
                const auto actions = window->actions();
                for (auto i = actions.indexOf(windowList) + 1; i < actions.size(); ++i)
                {
                    window->removeAction(actions[i]);
                    actions[i]->deleteLater();
                }
                emit windowMenuRequested(window);
            });
    auto *special = menuBar()->addMenu("Special");
    action(special, "Empty Trash…", "emptyTrash", {},
           [this]
           {
               const auto items = m_operations.trashedItems();
               if (items.isEmpty())
               {
                   QMessageBox::information(this, "Empty Trash", "The Trash is empty.");
                   return;
               }
               QMessageBox ask(QMessageBox::Warning, "Empty Trash",
                               QString("The Trash contains %1 item%2. Are you sure you want to "
                                       "permanently remove %3?")
                                   .arg(items.size())
                                   .arg(items.size() == 1 ? "" : "s")
                                   .arg(items.size() == 1 ? "this item" : "these items"),
                               QMessageBox::Ok | QMessageBox::Cancel, this);
               ask.setDefaultButton(QMessageBox::Cancel); // Destructive: Cancel is the default.
               keepReturnOnTheDefault(ask);
               if (ask.exec() != QMessageBox::Ok)
                   return;
               auto ops = m_operations;
               perform("Emptying Trash…",
                       [ops, items]
                       {
                           for (const auto &item : items)
                               if (auto result = ops.removeFromTrash(item); !result.ok)
                                   return result;
                           return FileResult{true, ops.trashBase(), {}};
                       });
           });
    // As in the 9.2.2 Finder (Eject and Erase Disk aside). Only inside the desktop session, which
    // performs them: Log Out closes applications first; Sleep, Restart and Shut Down where offered.
    if (qEnvironmentVariable("FRANKENSTEIN_ISOLATED") == "1")
    {
        special->addSeparator();
        m_powerActions << action(special, "Sleep", "sleep", {}, [this] { emit sessionRequested("sleep"); })
                       << action(special, "Restart", "restart", {}, [this] { emit sessionRequested("restart"); })
                       << action(special, "Shut Down", "shutDown", {}, [this] { emit sessionRequested("shut-down"); })
                       << special->addSeparator();
        action(special, "Log Out", "logOut", {}, [this] { emit sessionRequested("log-out"); });
        setPowerAvailable(false);
    }
    auto *help = menuBar()->addMenu("Help");
    action(help, "About Finder…", "about", {},
           [this]
           {
               QMessageBox::about(
                   this, "Finder",
                   "Platinum Reality\nA native Linux spatial file manager.\n\nHistorical "
                   "behavior is being qualified against Mac OS 9.2.2.");
           });
    for (auto *view :
         {static_cast<QAbstractItemView *>(m_icons), static_cast<QAbstractItemView *>(m_list)})
    {
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested, this,
                [this, view](QPoint point) { showContextMenu(view, point); });
        connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                &FinderWindow::updateActions);
    }
    connect(file, &QMenu::aboutToShow, this, &FinderWindow::updateActions);
    updateActions();
    QTimer::singleShot(100, this, &FinderWindow::restorePositions);
}
QStringList FinderWindow::selectedNames() const
{
    // In the order the window shows them: icons by position, list rows top to bottom.
    const bool icons = m_views->currentIndex() == 0;
    auto *view = icons ? static_cast<QAbstractItemView *>(m_icons) : static_cast<QAbstractItemView *>(m_list);
    QModelIndexList selected;
    QSet<QString> seen;
    for (const auto &index : view->selectionModel()->selectedIndexes())
    {
        const auto item = index.siblingAtColumn(0);
        if (!seen.contains(m_model->filePath(item)))
        {
            seen.insert(m_model->filePath(item));
            selected.push_back(item);
        }
    }
    std::stable_sort(selected.begin(), selected.end(), [view](const QModelIndex &a, const QModelIndex &b) {
        const auto first = view->visualRect(a), second = view->visualRect(b);
        return first.y() != second.y() ? first.y() < second.y() : first.x() < second.x();
    });
    QStringList names;
    for (const auto &index : selected)
        names.push_back(m_model->fileName(index));
    return names;
}
QStringList FinderWindow::selectedPaths() const
{
    QStringList paths;
    auto *selection =
        m_views->currentIndex() == 0 ? m_icons->selectionModel() : m_list->selectionModel();
    QSet<QString> seen;
    // An icon view exposes column 0 of a four-column QFileSystemModel. A
    // selected icon need not constitute a fully selected model row.
    for (const auto &i : selection->selectedIndexes())
    {
        const auto path = m_model->filePath(i.siblingAtColumn(0));
        if (!seen.contains(path))
        {
            paths.push_back(path);
            seen.insert(path);
        }
    }
    return paths;
}
void FinderWindow::updateActions()
{
    const auto paths = selectedPaths();
    const auto canonical = QFileInfo(m_directory).canonicalFilePath();
    const bool writable =
        !canonical.isEmpty() && (canonical == m_root || canonical.startsWith(m_root + '/'));
    const bool trash = m_directory == m_operations.trashBase() + "/files";
    const auto enable = [this](const char *name, bool enabled)
    {
        if (auto *action = findChild<QAction *>(name))
            action->setEnabled(enabled && !m_busy);
    };
    enable("newFolder", writable);
    enable("open", !paths.isEmpty());
    enable("openWith", paths.size() == 1 && m_operations.readableFile(paths.first()));
    enable("getInfo", paths.size() == 1);
    enable("copy", !paths.isEmpty());
    enable("cut", !paths.isEmpty() && m_directory != m_operations.trashBase() + "/files");
    const auto *clipboard = QGuiApplication::clipboard()->mimeData();
    enable("paste", clipboard && clipboard->hasUrls() && m_directory != m_operations.trashBase() + "/files");
    enable("rename", writable && paths.size() == 1);
    enable("duplicate", writable && paths.size() == 1);
    enable("makeAlias", writable && !paths.isEmpty());
    enable("newFile", writable);
    enable("openTerminal", !trash);
    enable("trash", writable && !paths.isEmpty());
    enable("restore", trash && paths.size() == 1);
    enable("cleanUp", m_views->currentIndex() == 0);
}
void FinderWindow::showInfo(const QString &path)
{
    // Mac OS 9's Info window (Finishing School FS-G01): modeless, one per item, bound to that
    // item and read-only. It belongs to the Finder, not this folder window, so it stays open
    // when the folder closes.
    if (const auto open = m_infoWindows.value(path))
    {
        open->raise();
        open->activateWindow();
        return;
    }
    const QFileInfo item(path);
    const auto details = fileDetails(path);
    auto *window = new QWidget(nullptr, Qt::Window);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setObjectName("infoWindow");
    window->setWindowTitle(item.fileName() + " Info");
    auto *layout = new QVBoxLayout(window);
    auto *heading = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(QFileIconProvider().icon(item).pixmap(32, 32));
    auto *name = new QLabel(item.fileName());
    name->setObjectName("infoName");
    name->setTextFormat(Qt::PlainText);
    auto bold = name->font();
    bold.setBold(true);
    name->setFont(bold);
    heading->addWidget(icon);
    heading->addWidget(name, 1);
    layout->addLayout(heading);
    auto *rule = new QFrame;
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    layout->addWidget(rule);
    auto *facts = new QFormLayout;
    facts->setLabelAlignment(Qt::AlignRight);
    const bool alias = details.contains("original");
    const QList<std::pair<QString, QString>> rows{{"Kind", "kindLabel"},
                                                  {alias ? "Original" : "Size", alias ? "original" : "size"},
                                                  {"Where", "where"},
                                                  {"Created", "created"},
                                                  {"Modified", "modified"}};
    for (const auto &[label, key] : rows)
    {
        auto *value = new QLabel(details.value(key).toString());
        value->setObjectName("info" + label);
        value->setTextFormat(Qt::PlainText);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        facts->addRow(label + ":", value);
    }
    const auto label = static_cast<LabelledFileModel *>(m_model)->labelOf(path);
    auto *labelValue = new QLabel(label ? FileLabels::name(label) : QString("None"));
    labelValue->setObjectName("infoLabel");
    labelValue->setTextFormat(Qt::PlainText);
    if (label)
    {
        QPixmap swatch(12, 12);
        swatch.fill(QColor(FileLabels::color(label)));
        auto *row = new QHBoxLayout;
        auto *chip = new QLabel;
        chip->setPixmap(swatch);
        row->addWidget(chip);
        row->addWidget(labelValue, 1);
        facts->addRow("Label:", row);
    }
    else
        facts->addRow("Label:", labelValue);
    layout->addLayout(facts);
    // Escape or Close Window (Ctrl+W, the Finder's Command key) closes it; nothing is written.
    for (const auto &sequence : {QKeySequence(Qt::Key_Escape), QKeySequence(Qt::CTRL | Qt::Key_W)})
        connect(new QShortcut(sequence, window), &QShortcut::activated, window, &QWidget::close);
    window->setMinimumWidth(320);
    m_infoWindows.insert(path, window);
    window->show();
}
void FinderWindow::setLabel(int label)
{
    auto *model = static_cast<LabelledFileModel *>(m_model);
    QStringList failed;
    for (const auto &path : selectedPaths())
    {
        if (!FileLabels::setLabel(path, label, m_profile))
            failed << QFileInfo(path).fileName();
        model->forget(path);
    }
    if (!failed.isEmpty())
        QMessageBox::warning(this, "Label", "The label could not be changed for: " + failed.join(", ") + ".");
}
void FinderWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Places beside a small, Mac-sized window would leave too little room for the folder.
    if (m_places)
        m_places->setVisible(width() >= 480);
}
void FinderWindow::updateTitle()
{
    setWindowTitle(m_directory == m_operations.trashBase() + "/files" ? "Trash"
                   : m_directory == m_root                            ? FinderPlaces::diskName(m_profile)
                   : QFileInfo(m_directory).fileName().isEmpty()
                       ? (m_directory == "/" ? QString("Computer") : FinderPlaces::diskName(m_profile))
                       : QFileInfo(m_directory).fileName());
}
void FinderWindow::updateHeader()
{
    const int count = m_model->rowCount(m_icons->rootIndex());
    auto text = QString("%1 item%2").arg(count).arg(count == 1 ? "" : "s");
    const QStorageInfo volume(m_directory);
    if (volume.isValid() && volume.isReady())
        text += ", " + QLocale::system().formattedDataSize(volume.bytesAvailable(), 2,
                                                            QLocale::DataSizeTraditionalFormat) +
                " available";
    m_header->setText(text);
}
void FinderWindow::updatePlaces()
{
    if (!m_places)
        return;
    m_places->clear();
    const auto add = [this](const QString &label, const QString &path, QStyle::StandardPixmap icon)
    {
        if (path.isEmpty() || !m_operations.readableDirectory(path))
            return;
        auto *item = new QListWidgetItem(style()->standardIcon(icon), label, m_places);
        item->setData(Qt::UserRole, QDir::cleanPath(path));
        item->setToolTip(QDir::toNativeSeparators(path));
        if (QFileInfo(path).canonicalFilePath() == QFileInfo(m_directory).canonicalFilePath())
            m_places->setCurrentItem(item);
    };
    add(FinderPlaces::diskName(m_profile), m_root, QStyle::SP_DriveHDIcon);
    for (const auto &[label, location] : QList<std::pair<QString, QStandardPaths::StandardLocation>>{
             {"Desktop", QStandardPaths::DesktopLocation}, {"Documents", QStandardPaths::DocumentsLocation},
             {"Downloads", QStandardPaths::DownloadLocation}, {"Pictures", QStandardPaths::PicturesLocation},
             {"Music", QStandardPaths::MusicLocation}, {"Videos", QStandardPaths::MoviesLocation}})
    {
        const auto path = location == QStandardPaths::DesktopLocation ? m_root + "/Desktop"
                                                                      : QStandardPaths::writableLocation(location);
        if (QFileInfo(path).canonicalFilePath() != QFileInfo(m_root).canonicalFilePath())
            add(label, path, QStyle::SP_DirIcon);
    }
    for (const auto &volumes : FinderPlaces::volumes)
        for (const auto &drive : QDir(volumes).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            add(drive.fileName(), drive.absoluteFilePath(), QStyle::SP_DriveFDIcon);
    if (FinderPlaces::wholeComputer)
        add("Computer", "/", QStyle::SP_ComputerIcon);
    add("Trash", m_operations.trashBase() + "/files", QStyle::SP_TrashIcon);
}
bool FinderWindow::navigate(const QString &requested, bool record)
{
    const auto path = QFileInfo(requested).canonicalFilePath();
    if (path.isEmpty() || !m_operations.readableDirectory(path) || m_busy)
        return false;
    if (path == m_directory)
        return true;
    saveState();
    m_settings->endGroup();
    if (record)
    {
        m_back.append(m_directory);
        m_forward.clear();
    }
    const auto from = m_directory;
    m_directory = path;
    m_stateKey = QString::fromLatin1(QCryptographicHash::hash(m_directory.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_settings->beginGroup(m_stateKey);
    m_directoryReady = false;
    m_icons->clearSelection();
    m_list->clearSelection();
    m_model->setRootPath(m_directory);
    const auto rootIndex = m_model->index(m_directory);
    m_icons->setRootIndex(rootIndex);
    m_list->setRootIndex(rootIndex);
    if (m_settings->contains("list"))
        setViewMode(m_settings->value("list").toBool());
    updateTitle();
    updateHeader();
    updatePlaces();
    m_path->setText(QDir::toNativeSeparators(m_directory));
    m_backButton->setEnabled(!m_back.isEmpty());
    m_forwardButton->setEnabled(!m_forward.isEmpty());
    m_upButton->setEnabled(m_directory != "/" && m_operations.readableDirectory(QFileInfo(m_directory).absolutePath()));
    // An already loaded folder sends no directoryLoaded again.
    QTimer::singleShot(0, this, [this, path] {
        if (m_directory == path && !m_model->canFetchMore(m_model->index(path)))
        {
            m_directoryReady = true;
            restorePositions();
            updateHeader();
        }
    });
    updateActions();
    emit navigated(from, m_directory);
    return true;
}
void FinderWindow::pasteInto(const QString &destination)
{
    const auto *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls() || !m_receiveFiles)
        return;
    QStringList paths;
    for (const auto &url : mime->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    const bool cut = mime->data("application/x-kde-cutselection") == "1" ||
                     mime->data("x-special/gnome-copied-files").startsWith("cut\n");
    m_receiveFiles(paths, destination, !cut);
    if (cut)
        QGuiApplication::clipboard()->clear(); // A moved item cannot be pasted again.
}
void FinderWindow::showContextMenu(QAbstractItemView *view, QPoint point)
{
    if (m_busy)
        return;
    const auto index = view->indexAt(point).siblingAtColumn(0);
    if (!index.isValid())
        view->clearSelection();
    else if (!view->selectionModel()->isSelected(index))
        view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect |
                                                  QItemSelectionModel::Rows);
    if (index.isValid())
        view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    updateActions();
    auto *menu = new QMenu(this);
    menu->setObjectName("finderContextMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto add = [this, menu](const char *name)
    {
        if (auto *action = findChild<QAction *>(name))
            menu->addAction(action);
    };
    // Like Dolphin's: the file commands where people look for them, in Mac OS 9's order.
    if (index.isValid())
    {
        const auto clicked = m_model->filePath(index);
        const bool folder = m_model->isDir(index);
        add("open");
        add("openWith");
        menu->addSeparator();
        add("cut");
        add("copy");
        if (folder)
        {
            auto *into = menu->addAction("Paste Into Folder");
            into->setObjectName("pasteInto");
            into->setEnabled(findChild<QAction *>("paste")->isEnabled() && selectedPaths().size() == 1);
            connect(into, &QAction::triggered, this, [this, clicked] { pasteInto(clicked); });
        }
        add("copyPath");
        menu->addSeparator();
        add("getInfo");
        if (auto *labels = findChild<QMenu *>("labelMenu"))
        {
            // Share real actions and checked-state updates, while keeping popup
            // presentation independent of the File menu's persistent submenu.
            auto *contextLabels=menu->addMenu(labels->title());
            contextLabels->setObjectName("finderContextLabels");
            contextLabels->addActions(labels->actions());
            connect(contextLabels,&QMenu::aboutToShow,labels,&QMenu::aboutToShow);
        }
        add("rename");
        add("duplicate");
        add("makeAlias");
        if (folder)
            add("openTerminal");
        menu->addSeparator();
        add(m_directory == m_operations.trashBase() + "/files" ? "restore" : "trash");
    }
    else
    {
        add("newFolder");
        add("newFile");
        menu->addSeparator();
        add("paste");
        menu->addSeparator();
        add("openTerminal");
        add("copyPath");
        menu->addSeparator();
        add("selectAll");
        add("cleanUp");
        auto *views = menu->addMenu("View");
        views->addAction(findChild<QAction *>("icons"));
        views->addAction(findChild<QAction *>("list"));
        add("showHidden");
    }
    const auto prepareMenu=[](QMenu *target) {
        target->setProperty("platinumFinderContextMenu",true);
        auto font=target->font();font.setBold(true);target->setFont(font);
    };
    prepareMenu(menu);
    for(auto *action:menu->actions())if(action->menu())prepareMenu(action->menu());
    menu->popup(view->viewport()->mapToGlobal(point));
}
QString FinderWindow::dropDirectoryAt(QPoint windowPosition) const
{
    if (!isVisible() || m_busy || !m_directoryReady || QApplication::activeModalWidget())
        return {};
    auto *view = m_views->currentIndex() == 0 ? static_cast<QAbstractItemView *>(m_icons)
                                              : static_cast<QAbstractItemView *>(m_list);
    const auto local = view->viewport()->mapFrom(this, windowPosition);
    if (!view->viewport()->rect().contains(local))
        return {};
    const auto directory = dropDirectory(view, local);
    return m_operations.readableDirectory(directory) ? directory : QString();
}
QVariantMap FinderWindow::directoryState() const
{
    // Read-only geometry for live qualification. It reports actual viewport
    // rectangles; it does not expose an input-injection or file-command API.
    auto *view = m_views->currentIndex() == 0 ? static_cast<QAbstractItemView *>(m_icons)
                                              : static_cast<QAbstractItemView *>(m_list);
    QVariantList entries;
    for (int row = 0; row < m_model->rowCount(view->rootIndex()) && row < 4096; ++row)
    {
        const auto index = m_model->index(row, 0, view->rootIndex());
        const auto rect = view->visualRect(index);
        const auto point = view->viewport()->mapTo(this, rect.topLeft());
        if (!rect.isValid())
            continue;
        entries.push_back(QVariantMap{{"name", m_model->fileName(index)},
                                      {"x", point.x()},
                                      {"y", point.y()},
                                      {"width", rect.width()},
                                      {"height", rect.height()}});
    }
    const auto contextMenus = findChildren<QMenu *>("finderContextMenu");
    return {{"surfaceId", finderSurfaceId(windowHandle())},
            {"loaded", m_directoryReady},
            {"busy", m_busy},
            {"windowActive", QApplication::activeWindow() == this},
            {"selectedItems", selectedPaths().size()},
            {"listView", m_views->currentIndex() == 1},
            {"showHidden", bool(m_model->filter() & QDir::Hidden)},
            {"entries", entries},
            {"contextMenuVisible", std::any_of(contextMenus.cbegin(), contextMenus.cend(),
                                               [](auto *menu) { return menu->isVisible(); })},
            {"items", m_model->rowCount(m_icons->rootIndex())}};
}
void FinderWindow::setShowHidden(bool show)
{
    QSettings settings(m_profile + "/finder.ini", QSettings::IniFormat);
    settings.setValue("showHiddenFiles", show);
    settings.sync();
    for (auto *widget : QApplication::topLevelWidgets())
        if (auto *window = qobject_cast<FinderWindow *>(widget))
        {
            const QDir::Filters base = QDir::AllEntries | QDir::NoDotAndDotDot;
            window->m_model->setFilter(show ? base | QDir::Hidden : base);
            if (auto *item = window->findChild<QAction *>("showHidden"))
                item->setChecked(show);
        }
}
void FinderWindow::openIndex(const QModelIndex &index)
{
    const QString path = m_model->filePath(index);
    QFileInfo info(path);
    if (info.isDir())
    {
        // In this window, as Dolphin does; with Option held, in its own window, as on a Mac.
        if (!m_operations.readableDirectory(path))
            QMessageBox::warning(this, "Open Folder", "This folder is unavailable.");
        else if (QApplication::keyboardModifiers() & Qt::AltModifier)
            emit openFolder(path);
        else
            navigate(path);
        return;
    }
    if (!m_operations.readableFile(path))
    {
        QMessageBox::warning(this, "Open Document",
                             "This document is unavailable or outside the selected workspace.");
        return;
    }
    emit openDocument(path, false);
}
void FinderWindow::perform(QString label, std::function<FileResult()> job, std::shared_ptr<CopyProgress> bytes,
                           std::shared_ptr<std::atomic_bool> cancel)
{
    if (m_busy)
    {
        QApplication::beep();
        return;
    }
    m_busy = true;
    updateActions();
    auto *progress = new QProgressDialog(label, bytes ? "Stop" : QString(), 0, bytes ? 1000 : 0, this);
    progress->setObjectName("operationProgress");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(150);
    progress->setAutoReset(false);
    progress->setAutoClose(false);
    if (bytes)
    {
        // Mac OS 9's copy dialog: what remains, and Stop.
        auto *timer = new QTimer(progress);
        connect(timer, &QTimer::timeout, progress, [progress, bytes, label]
        {
            const auto total = std::max<qint64>(1, bytes->total.load());
            const auto done = std::min(bytes->done.load(), total);
            progress->setValue(int(done * 1000 / total));
            progress->setLabelText(label + "\n" + QLocale().formattedDataSize(done) + " of " +
                                   QLocale().formattedDataSize(bytes->total.load()));
        });
        timer->start(150);
        connect(progress, &QProgressDialog::canceled, progress, [progress, cancel]
        {
            if (cancel)
                cancel->store(true);
            progress->setLabelText("Stopping…");
        });
    }
    auto *watcher = new QFutureWatcher<FileResult>(this);
    connect(
        watcher, &QFutureWatcher<FileResult>::finished, this,
        [this, progress, watcher]
        {
            auto result = watcher->result();
            m_busy = false;
            updateActions();
            progress->close();
            progress->deleteLater();
            watcher->deleteLater();
            if (!result.ok)
                QMessageBox::warning(this, "The operation could not be completed", result.error);
        });
    watcher->setFuture(QtConcurrent::run(std::move(job)));
}
void FinderWindow::setViewMode(bool list)
{
    m_views->setCurrentIndex(list ? 1 : 0);
    updateActions();
}
void FinderWindow::saveState()
{
    m_settings->setValue("geometry", saveGeometry());
    m_settings->setValue("list", m_views->currentIndex() == 1);
    if (m_views->currentIndex() == 0)
    {
        QVariantMap positions;
        for (int row = 0; row < m_model->rowCount(m_icons->rootIndex()); ++row)
        {
            auto index = m_model->index(row, 0, m_icons->rootIndex());
            positions.insert(m_model->fileName(index),
                             m_icons->visualRect(index).topLeft() +
                                 QPoint(m_icons->horizontalScrollBar()->value(),
                                        m_icons->verticalScrollBar()->value()));
        }
        m_settings->setValue("positions", positions);
    }
    m_settings->sync();
}
void FinderWindow::restorePositions()
{
    const auto positions = m_settings->value("positions").toMap();
    for (int row = 0; row < m_model->rowCount(m_icons->rootIndex()); ++row)
    {
        auto index = m_model->index(row, 0, m_icons->rootIndex());
        auto key = m_model->fileName(index);
        if (positions.contains(key))
            m_icons->restorePosition(index, positions.value(key).toPoint());
    }
}
void FinderWindow::closeEvent(QCloseEvent *e)
{
    if (m_busy)
    {
        QMessageBox::information(
            this, "File Operation",
            "Wait for the file operation to finish before closing this window.");
        e->ignore();
        return;
    }
    saveState();
    QMainWindow::closeEvent(e);
}

// Asks the desktop to make this window a pop-up (or not), or whether it is one; either reply
// sets the View menu's check. The desktop changes only this Finder's own windows.
void FinderWindow::askDesktopPopUp(const QString &method, bool popUp)
{
    const auto surface = finderSurfaceId(windowHandle());
    if (!surface)
        return;
    auto message = QDBusMessage::createMethodCall("org.frankenstein.Desktop", "/Desktop",
                                                  "org.frankenstein.Desktop", method);
    message << surface;
    if (method == "SetPopUpWindow")
        message << popUp;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, method, popUp]
    {
        const QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError())
            return;
        const bool isPopUp = method == "SetPopUpWindow" ? (reply.value() ? popUp : !popUp) : reply.value();
        if (auto *kind = findChild<QAction *>(isPopUp ? "asPopUpWindow" : "asWindow"))
            kind->setChecked(true);
    });
}
void FinderWindow::setPowerAvailable(bool available)
{
    for (auto *item : m_powerActions)
        item->setVisible(available);
}
void FinderWindow::findItems(const QString &text)
{
    auto *window = new QWidget(nullptr, Qt::Window);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setObjectName("findWindow");
    window->setWindowTitle("Find: " + text);
    window->resize(560, 360);
    auto *layout = new QVBoxLayout(window);
    auto *status = new QLabel("Searching “" + QFileInfo(m_directory).fileName() + "”…");
    status->setObjectName("findStatus");
    auto *results = new QTreeWidget;
    results->setObjectName("findResults");
    results->setHeaderLabels({"Name", "Where"});
    results->setRootIsDecorated(false);
    results->setColumnWidth(0, 220);
    auto *enclosing = new QPushButton("Open Enclosing Folder");
    enclosing->setEnabled(false);
    layout->addWidget(status);
    layout->addWidget(results, 1);
    layout->addWidget(enclosing, 0, Qt::AlignRight);
    const auto ops = m_operations;
    const QPointer<FinderWindow> self(this);
    const auto open = [self, ops](const QString &path)
    {
        if (!self)
            return;
        if (QFileInfo(path).isDir() && ops.readableDirectory(path))
            emit self->openFolder(path);
        else if (ops.readableFile(path))
            emit self->openDocument(path, false);
    };
    connect(results, &QTreeWidget::itemDoubleClicked, window,
            [open](QTreeWidgetItem *item) { open(item->data(0, Qt::UserRole).toString()); });
    connect(results, &QTreeWidget::itemSelectionChanged, window,
            [results, enclosing] { enclosing->setEnabled(results->currentItem()); });
    connect(enclosing, &QPushButton::clicked, window,
            [self, results, ops]
            {
                if (self && results->currentItem())
                    if (const auto folder = QFileInfo(results->currentItem()->data(0, Qt::UserRole).toString()).absolutePath();
                        ops.readableDirectory(folder))
                        emit self->openFolder(folder);
            });
    window->show();
    // Names only, never contents; hidden folders and links are not entered; bounded work.
    const QPointer<QWidget> target(window);
    auto *worker = QThread::create(
        [target, status, results, text, root = m_directory, ops]
        {
            QStringList found;
            QDirIterator walk(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDirIterator::Subdirectories);
            qsizetype visited = 0;
            bool complete = true;
            while (walk.hasNext())
            {
                if (++visited > 200000 || found.size() >= 1000 || !target)
                {
                    complete = false;
                    break;
                }
                const auto path = walk.next();
                if (walk.fileName().contains(text, Qt::CaseInsensitive) && ops.inside(QFileInfo(path).canonicalFilePath()))
                    found << path;
            }
            QMetaObject::invokeMethod(qApp, [target, status, results, found, complete, root]
            {
                if (!target)
                    return;
                const QFileIconProvider icons;
                for (const auto &path : found)
                {
                    auto *item = new QTreeWidgetItem(results, {QFileInfo(path).fileName(),
                                                               QDir(root).relativeFilePath(QFileInfo(path).absolutePath())});
                    item->setData(0, Qt::UserRole, path);
                    item->setIcon(0, icons.icon(QFileInfo(path)));
                }
                status->setText(QString("%1 item%2 found%3.").arg(found.size()).arg(found.size() == 1 ? "" : "s")
                                    .arg(complete ? "" : " (the search stopped early; look in a smaller folder)"));
            });
        });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
