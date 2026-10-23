#include "display_layout.hpp"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

std::optional<DisplayLayout> DisplayLayout::create(QVector<DisplayOutput> outputs,
                                                 QString primary, Mode mode, QString *error)
{
    if(error) error->clear();
    const auto reject=[&](const QString &message)->std::optional<DisplayLayout>{
        if(error) *error=message;
        return std::nullopt;
    };
    if(outputs.isEmpty() || outputs.size()>16) return reject("Expected 1 to 16 displays.");
    if(mode!=Mode::Extend && mode!=Mode::Mirror) return reject("Unknown display mode.");
    static const QRegularExpression connector("^[A-Za-z0-9_.-]{1,64}$");
    static const QSet<QString> transforms{"normal","90","180","270","flipped","flipped-90","flipped-180","flipped-270"};
    QSet<QString> names;
    QRect bounds;
    for(const auto &output:outputs) {
        const auto r=output.logical;
        if(!connector.match(output.name).hasMatch() || names.contains(output.name))
            return reject("Invalid or duplicate display connector.");
        names.insert(output.name);
        // Bound coordinates before QRect arithmetic; also bound the final canvas.
        if(r.width()<80 || r.height()<80 || r.width()>16384 || r.height()>16384 ||
           r.x() < -16384 || r.x()>16384 || r.y() < -16384 || r.y()>16384 ||
           !std::isfinite(output.scale) || output.scale<0.5 || output.scale>4 ||
           !transforms.contains(output.transform)) return reject("Invalid display geometry, scale or rotation.");
        for(const auto &other:outputs) {
            if(&output==&other) break;
            // Physical screens remain a non-overlapping host layout even in
            // Mirror: fitting is presentation, not overlapping output crops.
            if(r.intersects(other.logical)) return reject("Display rectangles overlap.");
        }
        bounds=bounds.united(r);
    }
    if(bounds.width()>16384 || bounds.height()>16384 || qint64(bounds.width())*bounds.height()>64*1024*1024)
        return reject("Display canvas is too large.");
    // Stable connector order and external-first fallback after docking changes.
    std::sort(outputs.begin(),outputs.end(),[](const auto &a,const auto &b){return a.name<b.name;});
    if(!names.contains(primary)) {
        const auto external=[](const QString &name){
            return !name.startsWith("eDP-",Qt::CaseInsensitive) && !name.startsWith("LVDS-",Qt::CaseInsensitive)
                   && !name.startsWith("DSI-",Qt::CaseInsensitive);
        };
        const auto best=std::max_element(outputs.begin(),outputs.end(),[&](const auto &a,const auto &b){
            if(external(a.name)!=external(b.name)) return !external(a.name);
            return qint64(a.logical.width())*a.logical.height()<qint64(b.logical.width())*b.logical.height();
        });
        primary=best->name;
    }
    DisplayLayout layout;
    layout.m_primary=primary;
    layout.m_mode=mode;
    layout.m_hostBounds=QRect(QPoint(),bounds.size());
    for(auto &output:outputs) output.logical.translate(-bounds.topLeft());
    layout.m_outputs=std::move(outputs);
    return layout;
}

DisplayLayout DisplayLayout::single(QSize size)
{
    // Only used for the existing unconstrained nested host window.
    DisplayLayout layout;
    size=size.expandedTo(QSize(80,80));
    layout.m_outputs={{"PLATINUM-1",QRect(QPoint(),size),1,"normal"}};
    layout.m_primary="PLATINUM-1";
    layout.m_hostBounds=QRect(QPoint(),size);
    return layout;
}

QRect DisplayLayout::primaryHostRect() const
{
    for(const auto &output:m_outputs) if(output.name==m_primary) return output.logical;
    return {};
}
QRect DisplayLayout::primaryRect() const
{
    const auto primary=primaryHostRect();
    return m_mode==Mode::Mirror ? QRect(QPoint(),primary.size()) : primary;
}
QRect DisplayLayout::desktopBounds() const
{
    return m_mode==Mode::Mirror ? primaryRect() : m_hostBounds;
}
QVector<QRect> DisplayLayout::desktopScreens() const
{
    if(m_mode==Mode::Mirror) return {primaryRect()};
    QVector<QRect> screens;
    for(const auto &output:m_outputs) screens.append(output.logical);
    return screens;
}
QRect DisplayLayout::screenForWindow(QRect window) const
{
    auto best=primaryRect();
    qint64 bestArea=-1, bestDistance=std::numeric_limits<qint64>::max();
    const auto candidates=desktopScreens();
    for(const auto &screen:candidates) {
        const auto overlap=window.intersected(screen);
        const qint64 area=qint64(overlap.width())*overlap.height();
        const auto delta=window.center()-screen.center();
        const qint64 distance=qint64(delta.x())*delta.x()+qint64(delta.y())*delta.y();
        if(area>bestArea || (area==bestArea && (distance<bestDistance ||
                    (distance==bestDistance && screen==primaryRect())))) {
            best=screen; bestArea=area; bestDistance=distance;
        }
    }
    return best;
}
QPoint DisplayLayout::reachablePosition(QPoint requested, QSize contentSize, bool fitWhole) const
{
    // Keep a title-bar grip on a real display, including layouts with gaps.
    const auto screens=desktopScreens();
    const QRect grip(requested,QSize(80,25));
    if(!fitWhole) {
        int covered=0;
        for(const auto &candidate:screens) {
            const auto work=candidate.adjusted(0,candidate==primaryRect()?26:0,0,0);
            const auto overlap=work.intersected(grip);
            covered+=overlap.width()*overlap.height();
        }
        // Adjacent screens form one continuous draggable area. Do not snap a
        // title bar merely because its grip crosses a monitor/scale boundary.
        if(covered==grip.width()*grip.height()) return requested;
    }
    auto screen=screenForWindow(grip);
    for(const auto &candidate:screens)
        if(candidate.contains(grip)) { screen=candidate; break; }
    const int top=screen.y()+(screen==primaryRect()?26:0);
    const int right=screen.x()+screen.width()-(fitWhole?contentSize.width()+6:80);
    const int bottom=screen.y()+screen.height()-(fitWhole?contentSize.height()+27:25);
    return {std::clamp(requested.x(),screen.x(),std::max(screen.x(),right)),
            std::clamp(requested.y(),top,std::max(top,bottom))};
}
QRectF DisplayLayout::mirrorPicture(const QString &name) const
{
    for(const auto &output:m_outputs) if(output.name==name) {
        const auto target=QRectF(output.logical);
        const auto source=primaryHostRect().size();
        const double ratio=std::min(target.width()/source.width(),target.height()/source.height());
        const QSizeF size(source.width()*ratio,source.height()*ratio);
        return {target.center()-QPointF(size.width()/2,size.height()/2),size};
    }
    return {};
}
std::optional<QPointF> DisplayLayout::hostToDesktop(QPointF point) const
{
    for(const auto &output:m_outputs) {
        const QRectF rect(output.logical);
        // Explicit half-open bounds: adjacent displays don't own the same edge.
        if(point.x()<rect.x() || point.x()>=rect.x()+rect.width() ||
           point.y()<rect.y() || point.y()>=rect.y()+rect.height()) continue;
        if(m_mode==Mode::Extend) return point;
        const auto picture=mirrorPicture(output.name);
        if(point.x()<picture.x() || point.x()>=picture.x()+picture.width() ||
           point.y()<picture.y() || point.y()>=picture.y()+picture.height()) return std::nullopt;
        const auto at=point-picture.topLeft();
        return QPointF(at.x()*primaryRect().width()/picture.width(),at.y()*primaryRect().height()/picture.height());
    }
    return std::nullopt;
}
