#pragma once
#include <QPoint>
#include <QRect>
#include <QSize>
#include <Qt>

// An xdg_positioner as the client sent it: where a popup (a menu, a tooltip) wants to be,
// relative to its parent's window geometry, and how it may be moved to stay on screen.
struct PopupPositioner
{
    QRect anchorRect;
    Qt::Edges anchorEdges;
    Qt::Edges gravityEdges;
    QPoint offset;
    QSize size;
    Qt::Orientations flip, slide, resize;
};

// The popup's geometry, kept inside bounds (the monitor, in the same coordinates) as the
// xdg-shell constraint adjustment rules describe: per axis, flip if that fits, else slide, else
// resize, and each only if the client allowed it. Unconstrained popups are returned unchanged.
QRect placePopup(const PopupPositioner &positioner, const QRect &bounds);
