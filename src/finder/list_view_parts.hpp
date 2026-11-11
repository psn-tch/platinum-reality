#pragma once
// Finder's list view parts (column header, row painting, selected icon), shared with other
// Platinum list windows such as the App Store, so every list draws the same way.
#include <QApplication>
#include <QHeaderView>
#include <QEvent>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QStyleOptionHeader>
#include <QStyledItemDelegate>
#include <QTreeView>

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

// The selected (darkened) form of a list or icon-view icon.
QIcon selectedFolderIcon(const QIcon &icon, QSize size, qreal ratio);

class FolderColumnHeader final : public QHeaderView
{
  public:
    // nameInset: where the Name title starts, past the rows' disclosure triangle and icon.
    explicit FolderColumnHeader(QWidget *parent, int nameInset = 44)
        : QHeaderView(Qt::Horizontal,parent), m_nameInset(nameInset)
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
        const int inset=logical==0 ? m_nameInset : 6;
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
  private:
    int m_nameInset;
};
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
