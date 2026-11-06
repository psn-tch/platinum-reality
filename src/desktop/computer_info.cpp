#include "computer_info.hpp"
#include <QFile>
#include <QLocale>
#include <QSet>
#include <QSysInfo>
#include <algorithm>
#include <limits>
#include <optional>

namespace
{
QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    // Proc files report size 0: read a bounded buffer, not QFile::atEnd().
    auto data = file.read(65537);
    return data.size() <= 65536 ? data : QByteArray{};
}
std::optional<qint64> bytes(const QByteArray &data, const QByteArray &name)
{
    std::optional<qint64> result;
    bool found = false;
    for (const auto &raw : data.split('\n')) {
        const auto fields = raw.simplified().split(' ');
        if (fields.value(0) != name + ':') continue;
        if (found || fields.size() != 3 || fields[2] != "kB") return {};
        found = true;
        bool ok = false;
        const auto value = fields[1].toULongLong(&ok);
        if (!ok || fields[1].startsWith('-') || value > quint64(std::numeric_limits<qint64>::max()/1024)) return {};
        result = qint64(value)*1024;
    }
    return result;
}
QString label(std::optional<qint64> value)
{
    return value ? QLocale::system().formattedDataSize(*value,1,QLocale::DataSizeTraditionalFormat)
                 : QString("Unavailable");
}
QVariant number(std::optional<qint64> value)
{
    return value ? QVariant::fromValue(*value) : QVariant{};
}
}

QVariantMap computerInformation(const QList<ComputerApplication> &applications, const QString &procRoot)
{
    const auto memory = read(procRoot+"/meminfo");
    const auto ram = bytes(memory,"MemTotal");
    auto available = bytes(memory,"MemAvailable");
    if (available && ram && *available > *ram) available.reset();
    const auto swap = bytes(memory,"SwapTotal"), freeSwap = bytes(memory,"SwapFree");
    const auto usedSwap = swap && freeSwap && *freeSwap <= *swap ? std::optional<qint64>(*swap-*freeSwap)
                                                                 : std::nullopt;
    QVariantList rows;
    qint64 largest = 0;
    for (const auto &app : applications.mid(0,128)) {
        QSet<qint64> seen;
        qint64 resident = 0;
        bool known = !app.processes.isEmpty() && app.processes.size() <= 64;
        for (auto pid : app.processes.mid(0,64)) {
            if (seen.contains(pid)) continue;
            seen.insert(pid);
            if (pid <= 0) { known = false; continue; }
            auto rss = bytes(read(procRoot+'/'+QString::number(pid)+"/status"),"VmRSS");
            if (!rss || *rss > std::numeric_limits<qint64>::max()-resident) { known = false; continue; }
            resident += *rss;
        }
        const std::optional<qint64> measured = known ? std::optional<qint64>(resident) : std::nullopt;
        if (known) largest = std::max(largest,resident);
        rows.append(QVariantMap{{"id",app.id},{"name",app.name.left(256)},{"system",app.system},
                                {"rssBytes",number(measured)},{"rssText",label(measured)},
                                {"known",known}});
    }
    for (auto &row : rows) {
        auto item = row.toMap();
        item.insert("fraction",item.value("known").toBool() && largest > 0
            ? double(item.value("rssBytes").toLongLong())/double(largest) : 0.0);
        row = item;
    }
    return {{"system",QSysInfo::prettyProductName()},{"version",QSysInfo::kernelVersion()},
            {"ramBytes",number(ram)},{"ram",label(ram)},
            {"swapBytes",number(swap)},{"swap",swap && *swap == 0 ? QString("Off") : label(swap)},
            {"swapUsed",label(usedSwap)},{"available",label(available)},
            // Linux MemAvailable is an estimate, not a largest contiguous heap.
            {"largestUnusedBlock",QString("Not reported")},{"applications",rows}};
}
