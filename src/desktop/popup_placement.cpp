#include "popup_placement.hpp"

namespace
{
// One axis of xdg_positioner placement: the anchor point on the anchor rectangle, then the
// popup on the gravity side of it. `low`/`high` are the edges for this axis (Left/Right or
// Top/Bottom).
int place(int anchorStart, int anchorLength, Qt::Edges anchor, Qt::Edges gravity, int length,
          Qt::Edge low, Qt::Edge high)
{
    const int point = anchor & low    ? anchorStart
                      : anchor & high ? anchorStart + anchorLength
                                      : anchorStart + anchorLength / 2;
    return gravity & low ? point - length : gravity & high ? point : point - length / 2;
}

Qt::Edges flipped(Qt::Edges edges, Qt::Edge low, Qt::Edge high)
{
    const bool hasLow = edges & low, hasHigh = edges & high;
    edges &= ~Qt::Edges(low | high);
    if (hasLow)
        edges |= high;
    if (hasHigh)
        edges |= low;
    return edges;
}

struct Span
{
    int start, length;
    int end() const { return start + length; }
    bool inside(int min, int max) const { return start >= min && end() <= max; }
};

Span constrain(Span span, int anchorStart, int anchorLength, Qt::Edges anchor, Qt::Edges gravity,
               int offset, bool flip, bool slide, bool resize, int min, int max, Qt::Edge low,
               Qt::Edge high)
{
    if (span.inside(min, max))
        return span;
    if (flip)
    {
        const Span other{place(anchorStart, anchorLength, flipped(anchor, low, high),
                               flipped(gravity, low, high), span.length, low, high) +
                             offset,
                         span.length};
        // xdg_positioner keeps the original anchor rectangle and offset; only
        // the anchor and gravity edges change during a flip.
        if (other.inside(min, max))
            return other;
    }
    if (slide)
    {
        if (span.end() > max)
            span.start = max - span.length;
        if (span.start < min)
            span.start = min; // Wider than the bounds: keep its start on screen.
        if (span.inside(min, max))
            return span;
    }
    if (resize)
    {
        const int start = qMax(span.start, min), end = qMin(span.end(), max);
        if (end > start)
            span = {start, end - start};
    }
    return span;
}
} // namespace

QRect placePopup(const PopupPositioner &p, const QRect &bounds)
{
    const Span x{place(p.anchorRect.x(), p.anchorRect.width(), p.anchorEdges, p.gravityEdges, p.size.width(),
                       Qt::LeftEdge, Qt::RightEdge) +
                     p.offset.x(),
                 p.size.width()};
    const Span y{place(p.anchorRect.y(), p.anchorRect.height(), p.anchorEdges, p.gravityEdges, p.size.height(),
                       Qt::TopEdge, Qt::BottomEdge) +
                     p.offset.y(),
                 p.size.height()};
    if (bounds.isEmpty())
        return {x.start, y.start, x.length, y.length};
    const auto cx = constrain(x, p.anchorRect.x(), p.anchorRect.width(), p.anchorEdges, p.gravityEdges, p.offset.x(),
                              p.flip & Qt::Horizontal, p.slide & Qt::Horizontal, p.resize & Qt::Horizontal,
                              bounds.left(), bounds.left() + bounds.width(), Qt::LeftEdge, Qt::RightEdge);
    const auto cy = constrain(y, p.anchorRect.y(), p.anchorRect.height(), p.anchorEdges, p.gravityEdges, p.offset.y(),
                              p.flip & Qt::Vertical, p.slide & Qt::Vertical, p.resize & Qt::Vertical,
                              bounds.top(), bounds.top() + bounds.height(), Qt::TopEdge, Qt::BottomEdge);
    return {cx.start, cy.start, cx.length, cy.length};
}
