#include "system_profile.hpp"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QStorageInfo>

SystemProfile::SystemProfile(QString root, QObject *parent) : QObject(parent), m_root(std::move(root)) {}

QString SystemProfile::read(const QString &path) const
{
    QFile file(m_root + path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.read(64 * 1024)).trimmed() : QString();
}

namespace
{
// A name from an hwdata list (pci.ids, usb.ids): vendor lines unindented, device lines one tab in.
QString hardwareName(const QString &list, const QString &vendor, const QString &device)
{
    QFile file(list);
    if (vendor.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {};
    QString vendorName;
    bool inVendor = false;
    while (!file.atEnd())
    {
        const auto line = QString::fromUtf8(file.readLine()).chopped(1);
        if (line.startsWith('#') || line.isEmpty())
            continue;
        if (!line.startsWith('\t'))
        {
            if (inVendor)
                break;
            inVendor = line.startsWith(vendor + "  ");
            if (inVendor)
                vendorName = line.mid(vendor.size() + 2);
        }
        else if (inVendor && !line.startsWith("\t\t") && line.mid(1).startsWith(device + "  "))
            return vendorName + " " + line.mid(device.size() + 3);
    }
    return vendorName;
}
QString sizeText(qint64 bytes)
{
    if (bytes <= 0)
        return {};
    const double gb = double(bytes) / 1e9;
    return gb >= 1000 ? QString::number(gb / 1000, 'f', 1) + " TB" : gb >= 10 ? QString::number(qRound(gb)) + " GB"
                                                                              : QString::number(gb, 'f', 1) + " GB";
}
} // namespace

QVariantMap SystemProfile::profile() const
{
    QVariantMap result;
    // Software.
    QString system;
    for (const auto &line : read("/etc/os-release").split('\n'))
        if (line.startsWith("PRETTY_NAME="))
            system = line.mid(12).remove('"');
    QString platinum;
    for (const auto &entry : QDir(m_root + "/var/lib/pacman/local").entryList({"platinum-reality-[0-9]*"}, QDir::Dirs))
        platinum = entry.mid(QString("platinum-reality-").size());
    result.insert("software", QVariantMap{{"system", system}, {"kernel", read("/proc/sys/kernel/osrelease")},
                                          {"platinum", platinum.isEmpty() ? QString("this development build") : platinum},
                                          {"toolkit", QString("Qt ") + qVersion()}});
    // Hardware.
    QString processor;
    int cores = 0;
    for (const auto &line : read("/proc/cpuinfo").split('\n'))
    {
        if (line.startsWith("processor"))
            ++cores;
        else if (processor.isEmpty() && line.startsWith("model name"))
            processor = line.section(':', 1).trimmed();
    }
    const auto khz = read("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq").toLongLong();
    result.insert("hardware", QVariantMap{{"model", read("/sys/class/dmi/id/product_name")},
                                          {"vendor", read("/sys/class/dmi/id/sys_vendor")},
                                          {"processor", processor}, {"cores", cores},
                                          {"speed", khz > 0 ? QString::number(khz / 1000) + " MHz" : QString()}});
    // Network: each interface that is up, but not the loopback.
    QVariantList network;
    for (const auto &interface : QNetworkInterface::allInterfaces())
    {
        if (interface.flags().testFlag(QNetworkInterface::IsLoopBack) || !interface.flags().testFlag(QNetworkInterface::IsUp))
            continue;
        QStringList addresses;
        for (const auto &entry : interface.addressEntries())
            if (!entry.ip().isLinkLocal())
                addresses << entry.ip().toString();
        const bool wireless = QFileInfo::exists("/sys/class/net/" + interface.name() + "/wireless");
        network.append(QVariantMap{{"name", interface.name()}, {"kind", wireless ? "Wi-Fi" : interface.type() == QNetworkInterface::Ethernet ? "Ethernet" : "Network"},
                                   {"addresses", addresses}});
    }
    result.insert("network", network);
    // USB devices, by bus, without the buses' own root hubs.
    QVariantList usb;
    const auto usbIds = m_root + "/usr/share/hwdata/usb.ids";
    for (const auto &entry : QDir(m_root + "/sys/bus/usb/devices").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        const auto base = "/sys/bus/usb/devices/" + entry.fileName();
        if (entry.fileName().startsWith("usb") || entry.fileName().contains(':') || read(base + "/idVendor").isEmpty())
            continue;
        auto name = (read(base + "/manufacturer") + " " + read(base + "/product")).trimmed();
        if (name.isEmpty())
            name = hardwareName(usbIds, read(base + "/idVendor"), read(base + "/idProduct"));
        usb.append(QVariantMap{{"bus", read(base + "/busnum").toInt()}, {"name", name.isEmpty() ? QString("USB device") : name}});
    }
    result.insert("usb", usb);
    // Disks and their mounted volumes (each volume once, though btrfs mounts it several times).
    QVariantList storage;
    const auto mounted = QStorageInfo::mountedVolumes();
    for (const auto &entry : QDir(m_root + "/sys/block").entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name))
    {
        const auto name = entry.fileName();
        if (name.startsWith("loop") || name.startsWith("zram") || name.startsWith("ram") || name.startsWith("dm-"))
            continue;
        const auto base = "/sys/block/" + name;
        const auto size = read(base + "/size").toLongLong() * 512;
        if (size <= 0)
            continue;
        QVariantList volumes;
        QStringList seen;
        for (const auto &volume : mounted)
        {
            const auto device = QString::fromUtf8(volume.device());
            if (!device.startsWith("/dev/" + name) || seen.contains(device))
                continue;
            seen << device;
            const auto label = volume.name().isEmpty() ? (volume.rootPath() == "/" ? QString("System") : QDir(volume.rootPath()).dirName())
                                                       : volume.name();
            volumes.append(QVariantMap{{"name", label},
                                       {"mountPoint", volume.rootPath()}, {"fileSystem", QString::fromUtf8(volume.fileSystemType())},
                                       {"size", sizeText(volume.bytesTotal())}});
        }
        const auto device = QFileInfo(m_root + base + "/device").canonicalFilePath();
        storage.append(QVariantMap{{"name", name}, {"model", read(base + "/device/model")}, {"size", sizeText(size)},
                                   {"removable", read(base + "/removable") == "1"}, {"usb", device.contains("/usb")},
                                   {"volumes", volumes}});
    }
    result.insert("storage", storage);
    // Display cards and the connectors with a display.
    QVariantList video;
    const auto pciIds = m_root + "/usr/share/hwdata/pci.ids";
    for (const auto &entry : QDir(m_root + "/sys/class/drm").entryList({"card*"}, QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name))
    {
        if (entry.contains('-'))
            continue;
        const auto base = "/sys/class/drm/" + entry + "/device";
        auto card = hardwareName(pciIds, read(base + "/vendor").mid(2), read(base + "/device").mid(2));
        QStringList connectors;
        for (const auto &connector : QDir(m_root + "/sys/class/drm").entryList({entry + "-*"}, QDir::AllEntries, QDir::Name))
            if (read("/sys/class/drm/" + connector + "/status") == "connected")
                connectors << connector.mid(entry.size() + 1);
        video.append(QVariantMap{{"card", card.isEmpty() ? QString("Display card") : card}, {"connectors", connectors}});
    }
    result.insert("video", video);
    return result;
}
