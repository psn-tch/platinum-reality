#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

// Mac OS 9 alert sounds (Finishing School Phase 11, docs/alert-sounds.md). The public build ships
// one original sound, "Simple Beep", synthesized into the profile; personal sounds are WAV files
// the user places in <assets>/sounds and are never bundled. The choice and alert volume persist
// in the profile. Without a player (tests, previews) or at volume 0, an alert flashes the menu
// bar instead, as Mac OS 9 did.
class AlertSounds final : public QObject
{
    Q_OBJECT
    // [{name, path, personal}], "Simple Beep" first, then personal sounds by name.
    Q_PROPERTY(QVariantList sounds READ sounds NOTIFY changed)
    Q_PROPERTY(QString selected READ selected NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(QString assetFolder READ assetFolder CONSTANT)
  public:
    // player is the playback program (pw-play); empty means silent (menu-bar flash only).
    AlertSounds(QString profile, QString assets, QString player, QObject *parent = nullptr);
    ~AlertSounds() override;
    QVariantList sounds() const { return m_sounds; }
    QString selected() const { return m_selected; }
    int volume() const { return m_volume; }
    QString assetFolder() const { return m_assets + "/sounds"; }
    Q_INVOKABLE void refresh();
    // Choose and play the new alert sound, as the Sound control panel does.
    Q_INVOKABLE void choose(const QString &name);
    Q_INVOKABLE void setVolume(int value);
    // The system alert.
    Q_INVOKABLE void play();

  signals:
    void changed();
    // A silent alert: the menu bar flashes.
    void flash();
    void played(const QString &path, int volume);

  private:
    void save() const;
    QString pathOf(const QString &name) const;
    QString m_profile, m_assets, m_player, m_selected = "Simple Beep";
    int m_volume = 60;
    QVariantList m_sounds;
    QProcess m_process;
    QTimer m_limit;
};

// The startup sound (docs/alert-sounds.md): the user's own <assets>/startup-chime.*, played by
// the session supervisor once per boot when the Log In appears. Never bundled. Whether it plays and
// how loud persist in <profile>/startup-sound.json, which the supervisor reads at the next boot.
class StartupSound final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(QString assetFolder READ assetFolder CONSTANT)
  public:
    // player is the playback program (pw-play); empty means silent (tests, previews).
    StartupSound(QString profile, QString assets, QString player, QObject *parent = nullptr);
    ~StartupSound() override;
    bool available() const { return !m_path.isEmpty(); }
    QString fileName() const;
    bool enabled() const { return m_enabled; }
    int volume() const { return m_volume; }
    QString assetFolder() const { return m_assets; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setEnabled(bool value);
    Q_INVOKABLE void setVolume(int value);
    // Plays the sound at the chosen volume, as a preview.
    Q_INVOKABLE void play();

  signals:
    void changed();
    void played(const QString &path, int volume);

  private:
    void save() const;
    QString m_profile, m_assets, m_player, m_path;
    bool m_enabled = true;
    // Half volume, as before this setting existed: full volume startled Lily Hewitt (2026-10-23).
    int m_volume = 50;
    QProcess m_process;
    QTimer m_limit;
};
