#pragma once
#include <QObject>
#include <QString>
#include <QTimer>

// The laptop's own battery for the Control Strip's Battery module (as a PowerBook's Battery
// Monitor), read from sysfs power_supply: no daemon, no bus, read-only. Peripheral batteries
// (scope "Device", such as a Bluetooth mouse) are not the system battery.
class BatteryStatus final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool present READ present NOTIFY changed)
    Q_PROPERTY(int percent READ percent NOTIFY changed)
    Q_PROPERTY(bool charging READ charging NOTIFY changed)
    Q_PROPERTY(bool pluggedIn READ pluggedIn NOTIFY changed)
    // "Charging", "Discharging", "Full" or "Not charging".
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // Estimated minutes until empty (discharging) or full (charging); -1 when unknown.
    Q_PROPERTY(int minutesRemaining READ minutesRemaining NOTIFY changed)
  public:
    explicit BatteryStatus(QString root = "/sys/class/power_supply", QObject *parent = nullptr);
    bool present() const { return m_present; }
    int percent() const { return m_percent; }
    bool charging() const { return m_state == "Charging"; }
    bool pluggedIn() const { return m_pluggedIn; }
    QString state() const { return m_state; }
    int minutesRemaining() const { return m_minutes; }
    Q_INVOKABLE void refresh();

  signals:
    void changed();

  private:
    QString m_root, m_state;
    bool m_present = false, m_pluggedIn = false;
    int m_percent = -1, m_minutes = -1;
    QTimer m_poll;
};
