#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QVariantList>
#include <functional>

// Extensions Manager (Lily Hewitt, 2026-11-14): the programs that start when you log in, after
// 9.2.2's Extensions Manager (captured 2026-11-12). The items are the freedesktop autostart
// entries (the user's ~/.config/autostart over the system's /etc/xdg/autostart, by file name)
// that apply to this session; each is On or Off in the selected set. The sets are this session's
// own (profile/extensions.json), so KDE keeps its own startup programs untouched. Two sets are
// locked, as 9.2.2's "Mac OS 9.2.2 All" and "Base": changing one makes the change in My Settings.
// In the first session the items already there begin Off; later ones begin On.
class StartupItems final : public QObject
{
    Q_OBJECT
    // {id, name, comment, file, command, program, user, size, package, version, on}, user items first.
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(QStringList sets READ sets NOTIFY changed)
    Q_PROPERTY(QString selectedSet READ selectedSet NOTIFY changed)
    Q_PROPERTY(bool selectedLocked READ selectedLocked NOTIFY changed)
    // What starts at the next login differs from what started at this one (Revert puts it back).
    Q_PROPERTY(bool changedSinceLogin READ changedSinceLogin NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    static constexpr auto mySettings = "My Settings";
    static constexpr auto allSet = "Platinum Reality All";
    static constexpr auto baseSet = "Platinum Reality Base";
    // Starts one item's desktop file; false when it could not.
    using Launcher = std::function<bool(const QString &file, const QString &id)>;
    // directories: most important first (the user's own autostart folder, then the system's).
    StartupItems(QString profile, QStringList directories, QStringList desktopNames,
                 QProcessEnvironment environment, QObject *parent = nullptr);
    ~StartupItems() override;
    // $XDG_CONFIG_HOME/autostart, then each of $XDG_CONFIG_DIRS (default /etc/xdg) /autostart.
    static QStringList standardDirectories(const QProcessEnvironment &environment);
    QVariantList items() const;
    QStringList sets() const;
    QString selectedSet() const { return m_selected; }
    bool selectedLocked() const { return locked(m_selected); }
    bool changedSinceLogin() const;
    QString error() const { return m_error; }
    bool isOn(const QString &id) const;
    // Reads the folders again, and asks pacman which package owns each program (in the background).
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool setOn(const QString &id, bool on);
    // Every item of a group (user or system) On or Off at once.
    Q_INVOKABLE bool setGroupOn(bool user, bool on);
    Q_INVOKABLE bool selectSet(const QString &name);
    // A copy of the selected set under a new name, which becomes the selected set.
    Q_INVOKABLE bool duplicateSet(const QString &name);
    Q_INVOKABLE bool deleteSet(const QString &name);
    // Back to the sets as they were when this session started.
    Q_INVOKABLE void revert();
    // Starts each item that is On, once; returns the ids started.
    QStringList startAll(const Launcher &launcher);

  signals:
    void changed();

  private:
    struct Item
    {
        QString id, name, comment, file, command, program, package, version;
        bool user = false;
        qint64 size = 0;
    };
    static bool locked(const QString &name) { return name == allSet || name == baseSet; }
    QStringList offIn(const QString &set) const;
    QStringList startingIds() const;
    // Makes the selected set editable: a locked one is copied into My Settings first.
    void unlockSelected();
    bool save();
    void load();
    void readPackages();
    QString m_profile;
    QStringList m_directories, m_desktopNames;
    QProcessEnvironment m_environment;
    QList<Item> m_items;
    // Set name -> ids that are Off in it (new items are On, as a newly installed extension is).
    QHash<QString, QStringList> m_sets;
    QString m_selected = mySettings;
    QJsonObject m_atLogin;
    QStringList m_startedAtLogin;
    QString m_error;
    QProcess m_pacman;
};
