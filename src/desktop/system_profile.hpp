#pragma once
#include <QObject>
#include <QVariantMap>

// Apple System Profiler's facts (9.2.2 captured 2026-11-15): the software, hardware and network
// overviews, and the devices and volumes on each bus. All read-only, from /etc/os-release, /proc,
// /sys, the hardware ID lists (hwdata) and the mounted volumes; nothing is changed or run.
class SystemProfile final : public QObject
{
    Q_OBJECT
  public:
    // root: prefix for etc/, proc/, sys/, usr/share/hwdata and var/lib/pacman ("" on the computer;
    // tests name a folder). Mounted volumes and network interfaces are always the computer's.
    explicit SystemProfile(QString root, QObject *parent = nullptr);
    // {software: {system, kernel, platinum, toolkit}, hardware: {model, vendor, processor, cores,
    //  speed}, network: [{name, kind, addresses}], usb: [{bus, name}], storage: [{name, model, size,
    //  removable, usb, volumes: [{name, mountPoint, fileSystem, size}]}], video: [{card, connectors}]}
    Q_INVOKABLE QVariantMap profile() const;
    // The device files whose volumes are on a disk ("sda"): it, its partitions and the
    // device-mapper volumes made from them (/dev/dm-0 and /dev/mapper/cryptroot).
    QStringList devices(const QString &disk) const;

  private:
    QString read(const QString &path) const;
    QString m_root;
};
