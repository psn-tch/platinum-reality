#pragma once
#include <QObject>
#include <QVariantMap>

// The Keyboard and Mouse & Trackpad control panels' choices, saved in the profile's input.json.
// The session supervisor reads the same file and applies it to the private Sway (keyboards,
// mice, touchpads); the manager also gives the keyboard repeat to the desktop's own clients.
// Values outside their range are refused. A missing value leaves the system's default.
class InputSettings final : public QObject
{
    Q_OBJECT
    // {repeatDelay (ms, 150-1500), repeatRate (per second, 1-60), layouts (the checked Keyboard
    // Layouts), layout (the one in use)}
    Q_PROPERTY(QVariantMap keyboard READ keyboard NOTIFY changed)
    // {speed (-1 to 1), natural, leftHanded}
    Q_PROPERTY(QVariantMap pointer READ pointer NOTIFY changed)
    // {speed (-1 to 1), natural, tap, disableWhileTyping}
    Q_PROPERTY(QVariantMap touchpad READ touchpad NOTIFY changed)
    // Every layout the Keyboard panel offers: [{id ("de", "ch(fr)"), name ("German")}], by name.
    Q_PROPERTY(QVariantList layoutChoices READ layoutChoices CONSTANT)
  public:
    explicit InputSettings(QString profile, QObject *parent = nullptr);
    QVariantMap keyboard() const { return section("keyboard"); }
    QVariantMap pointer() const { return section("pointer"); }
    QVariantMap touchpad() const { return section("touchpad"); }
    Q_INVOKABLE bool set(const QString &section, const QString &key, const QVariant &value);
    // Back to the defaults: the file is removed, as if nothing had been chosen.
    Q_INVOKABLE bool reset(const QString &section);
    int repeatDelay() const { return m_values.value("keyboard/repeatDelay").toInt(); }
    int repeatRate() const { return m_values.value("keyboard/repeatRate").toInt(); }
    QStringList layouts() const { return m_values.value("keyboard/layouts").toStringList(); }
    QString layout() const { return m_values.value("keyboard/layout").toString(); }
    QVariantList layoutChoices() const;
    Q_INVOKABLE QString layoutName(const QString &id) const;

  signals:
    void changed();
    // After the file is written: the supervisor should apply it.
    void saved();
    void errorOccurred(QString message);

  private:
    QVariantMap section(const QString &name) const;
    bool save(const QVariantMap &values, const QStringList &chosen);
    QString m_profile;
    QVariantMap m_values;
    // The keys the user has chosen; only these are written.
    QStringList m_chosen;
};
