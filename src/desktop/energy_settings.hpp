#pragma once
#include <QObject>

// The Energy Saver panel's choices, in the profile's energy.json: minutes without use before the
// displays turn off and before the computer sleeps; 0 is never (the default). The session
// supervisor gives them to swayidle (scripts/standalone-session.py, saved_energy_settings).
class EnergySettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int displayOff READ displayOff NOTIFY changed)
    Q_PROPERTY(int sleep READ sleep NOTIFY changed)
  public:
    explicit EnergySettings(QString profile, QObject *parent = nullptr);
    int displayOff() const { return m_displayOff; }
    int sleep() const { return m_sleep; }
    // "displayOff" (0, or 1-240 minutes) or "sleep" (0, or 5-480 minutes).
    Q_INVOKABLE bool set(const QString &key, int minutes);

  signals:
    void changed();
    // After the file is written: the supervisor should apply it.
    void saved();
    void errorOccurred(QString message);

  private:
    QString m_profile;
    int m_displayOff = 0, m_sleep = 0;
};
