#pragma once
#include <QHash>
#include <QObject>
#include <QProcess>
#include <functional>
#include <vector>

// The Appearance sound track (Mac OS 9 "Platinum Sounds"): menus, windows and WindowShade make
// their theme sounds. None are bundled. They are the user's own, imported by
// scripts/import-macos9-assets.py into <assets>/sounds/platinum with a manifest of theme-sound
// tags (mnuo, wopn, wcol, ...). The track choice persists in the profile; the alert volume
// applies. Without a player (tests, previews) nothing is heard, but played() still reports.
class InterfaceSounds final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
  public:
    InterfaceSounds(QString profile, QString assets, QString player, std::function<int()> volume,
                    QObject *parent = nullptr);
    ~InterfaceSounds() override;
    bool available() const { return !m_files.isEmpty(); }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void play(const QString &tag);

  signals:
    void changed();
    void played(const QString &tag, const QString &path, int volume);

  private:
    QString m_profile, m_folder, m_player;
    std::function<int()> m_volume;
    QHash<QString, QString> m_files;
    bool m_enabled = true;
    std::vector<QProcess *> m_playing;
};
