// xdg_positioner constraint adjustment (flip, slide, resize), as the xdg-shell protocol describes.
#include "desktop/popup_placement.hpp"
#include <QTest>

namespace
{
// A context menu as Qt, GTK and Firefox ask for one: anchored at the click point, growing down
// and to the right, and allowed to flip and slide on both axes.
PopupPositioner menuAt(QPoint click, QSize size, Qt::Orientations flip = Qt::Horizontal | Qt::Vertical,
                       Qt::Orientations slide = Qt::Horizontal | Qt::Vertical, Qt::Orientations resize = {})
{
    return {QRect(click, QSize(1, 1)), Qt::TopEdge | Qt::LeftEdge, Qt::BottomEdge | Qt::RightEdge, {}, size, flip,
            slide, resize};
}
} // namespace

class PopupPlacementTests final : public QObject
{
    Q_OBJECT
  private slots:
    void aMenuThatFitsIsNotMoved()
    {
        QCOMPARE(placePopup(menuAt({100, 100}, {140, 66}), {0, 0, 1120, 760}), QRect(100, 100, 140, 66));
    }
    void aMenuAtTheBottomRightCornerFlipsToStayOnScreen()
    {
        // The defect: at (1104, 753) a 141x66 menu was drawn almost entirely off a 1120x760 screen.
        QCOMPARE(placePopup(menuAt({1104, 753}, {141, 66}), {0, 0, 1120, 760}), QRect(964, 688, 141, 66));
    }
    void flippingThatStillDoesNotFitSlidesInstead()
    {
        // Near the top of a short screen a flip up would cross the top edge: slide instead.
        const auto placed = placePopup(menuAt({50, 40}, {100, 80}), {0, 0, 400, 100});
        QCOMPARE(placed, QRect(50, 20, 100, 80));
    }
    void onlyAllowedAdjustmentsAreMade()
    {
        // No adjustments allowed (as xwayland-satellite asks): the request is kept as it is.
        QCOMPARE(placePopup(menuAt({1104, 753}, {141, 66}, {}, {}), {0, 0, 1120, 760}), QRect(1104, 753, 141, 66));
        // Slide only: pushed back inside without flipping.
        QCOMPARE(placePopup(menuAt({1104, 753}, {141, 66}, {}), {0, 0, 1120, 760}), QRect(979, 694, 141, 66));
        // Resize only: cut to what is visible.
        QCOMPARE(placePopup(menuAt({1104, 700}, {141, 66}, {}, {}, Qt::Horizontal | Qt::Vertical), {0, 0, 1120, 760}),
                 QRect(1104, 700, 16, 60));
    }
    void aMenuTallerThanTheScreenKeepsItsTopVisible()
    {
        QCOMPARE(placePopup(menuAt({10, 500}, {100, 900}), {0, 0, 1120, 760}).top(), 0);
    }
    void flippingPreservesTheOriginalOffset()
    {
        // A submenu: anchored to the right edge of its item with a 4-pixel overlap offset.
        PopupPositioner submenu{QRect(1000, 300, 150, 20), Qt::TopEdge | Qt::RightEdge, Qt::BottomEdge | Qt::RightEdge,
                                QPoint(-4, 0), QSize(160, 100), Qt::Horizontal | Qt::Vertical, Qt::Horizontal | Qt::Vertical, {}};
        // xdg_positioner keeps the original offset; only anchor/gravity flip.
        QCOMPARE(placePopup(submenu, {0, 0, 1120, 760}), QRect(1000 - 160 - 4, 300, 160, 100));
        submenu.anchorRect.moveTop(730);
        submenu.anchorEdges = Qt::BottomEdge | Qt::RightEdge;
        submenu.offset.setY(7);
        QCOMPARE(placePopup(submenu, {0, 0, 1120, 760}), QRect(836, 637, 160, 100));
    }
    void originalOffsetDeterminesWhetherAFlipFits()
    {
        auto popup = menuAt({110, 20}, {50, 30});
        popup.offset = {-30, 0};
        // Flipping the 1-pixel anchor to its right edge gives x=111. Keeping
        // -30 makes the 50-pixel popup fit at x=31. Inverting it would reject
        // the valid flip and slide to x=50 instead.
        QCOMPARE(placePopup(popup, {0, 0, 100, 100}), QRect(31, 20, 50, 30));
    }
    void boundsAreAnyMonitorNotTheOrigin()
    {
        // The second monitor (1120, 0, 960x600): a menu at its bottom right stays on it.
        QCOMPARE(placePopup(menuAt({2065, 593}, {142, 66}), {1120, 0, 960, 600}), QRect(1924, 528, 142, 66));
    }
};

QTEST_GUILESS_MAIN(PopupPlacementTests)
#include "popup_placement_tests.moc"
