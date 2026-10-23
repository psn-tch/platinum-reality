#pragma once
#include <QSize>
#include <algorithm>

struct WindowPresentation
{
    bool resize;
    bool zoom;
    bool closeBox;
};

// Native presentation policy, not a claim about every classic dialog class.
// A modal picker may remain resizable; a fixed-size alert must not grow.
inline WindowPresentation windowPresentation(bool modal, QSize minimum, QSize maximum)
{
    const auto flexible = [](int low, int high)
    { return high <= 0 || high > std::max(0, low); };
    const bool resize = flexible(minimum.width(), maximum.width()) ||
                        flexible(minimum.height(), maximum.height());
    return {resize, !modal && resize, !modal};
}

inline QSize boundedContentSize(QSize requested, QSize minimum, QSize maximum)
{
    const auto extent = [](int desired, int low, int high, int fallback)
    {
        if (low <= 0)
            low = high > 0 ? std::min(fallback, high) : fallback;
        return std::clamp(desired, low, std::max(low, high > 0 ? high : 16384));
    };
    return {extent(requested.width(), minimum.width(), maximum.width(), 160),
            extent(requested.height(), minimum.height(), maximum.height(), 90)};
}
