#pragma once
#include "appearance/interface_font.hpp"
#include <QColor>
#include <QMap>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

class AppearanceSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int shadeDuration READ shadeDuration NOTIFY changed)
    Q_PROPERTY(bool controlStripVisible READ controlStripVisible NOTIFY changed)
    Q_PROPERTY(bool controlStripCollapsed READ controlStripCollapsed NOTIFY changed)
    Q_PROPERTY(int controlStripX READ controlStripX NOTIFY changed)
    Q_PROPERTY(int controlStripY READ controlStripY NOTIFY changed)
    // How many modules the strip shows, set by dragging its end tab; 0 shows every one that fits.
    Q_PROPERTY(int controlStripModules READ controlStripModules NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)
    Q_PROPERTY(QUrl menuEmblem READ menuEmblem CONSTANT)
    Q_PROPERTY(QUrl wallpaper READ wallpaper NOTIFY changed)
    Q_PROPERTY(QString wallpaperPath READ wallpaperPath NOTIFY changed)
    Q_PROPERTY(QString wallpaperLayout READ wallpaperLayout NOTIFY changed)
    // A picture of its own for a monitor (Lily Hewitt, 2026-11-06), by output name ("DP-2"):
    // {name: {path, layout, url}}; path "" is the solid desktop colour there. Every other monitor
    // shows the desktop picture above, each monitor its own copy, never one spread across them.
    Q_PROPERTY(QVariantMap monitorPictures READ monitorPictures NOTIFY changed)
    // The Control Panel's desktop pattern in use: 16 hex digits, a byte a row (top first, high bit
    // left, set = black), or "" while the desktop shows a picture instead.
    Q_PROPERTY(QString desktopPattern READ desktopPattern NOTIFY changed)
    Q_PROPERTY(QString defaultWallpaperPath READ defaultWallpaperPath CONSTANT)
    Q_PROPERTY(QString picturesFolder READ picturesFolder CONSTANT)
    // Appearance → Appearance variation: "blue" (default) or "graphite" (F01, V922 E11).
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY changed)
    // Appearance → Highlight Color (9.2.2): selected text's colour in the desktop's own fields.
    // One of highlightChoices()' names; Purple, as the reference 9.2.2 shows, by default.
    Q_PROPERTY(QString highlight READ highlight NOTIFY changed)
    Q_PROPERTY(QColor highlightColor READ highlightColor NOTIFY changed)
    Q_PROPERTY(QColor highlightTextColor READ highlightTextColor NOTIFY changed)
    // Appearance → Options: double-click a title bar to collapse the window (on by default).
    Q_PROPERTY(bool doubleClickCollapse READ doubleClickCollapse NOTIFY changed)
    Q_PROPERTY(bool keepScreenAwake READ keepScreenAwake NOTIFY changed)
    // Date & Time → the menu bar clock: {visible, hours24, seconds, weekday, date, flash}.
    Q_PROPERTY(QVariantMap clock READ clock NOTIFY changed)
    // General Controls (9.2.2): {showDesktop (the desktop's icons while another application is in
    // front), menuBlinks (times a chosen menu item blinks, 0-3), caretBlink (0 slow, 1 medium,
    // 2 fast), barSize (the menu bar, its menus and the Control Strip, 100-150 percent; not in
    // 9.2.2, Lily Hewitt's request 2026-11-09)}.
    Q_PROPERTY(QVariantMap general READ general NOTIFY changed)
    // Apple Menu Options: {submenus, rememberRecent, recentApplications} (AppleMenuOptionsPanel.qml).
    Q_PROPERTY(QVariantMap appleMenu READ appleMenu NOTIFY changed)
    // The insertion point's full on-and-off cycle in milliseconds, from caretBlink.
    Q_PROPERTY(int caretFlashTime READ caretFlashTime NOTIFY changed)
    // The Application Switcher palette torn off the Application menu: {visible, x, y}.
    Q_PROPERTY(QVariantMap switcher READ switcher NOTIFY changed)
  public:
    explicit AppearanceSettings(QString profile, QObject *parent = nullptr);
    int shadeDuration() const
    {
        return m_duration;
    }
    bool controlStripVisible() const
    {
        return m_stripVisible;
    }
    bool controlStripCollapsed() const
    {
        return m_stripCollapsed;
    }
    int controlStripX() const
    {
        return m_stripX;
    }
    int controlStripY() const
    {
        return m_stripY;
    }
    int controlStripModules() const
    {
        return m_stripModules;
    }
    QString fontFamily() const
    {
        return classicInterfaceFontFamily();
    }
    QUrl menuEmblem() const
    {
        return m_emblem;
    }
    QUrl wallpaper() const
    {
        return m_wallpaper;
    }
    QString wallpaperPath() const
    {
        return m_wallpaperPath;
    }
    QString wallpaperLayout() const
    {
        return m_wallpaperLayout;
    }
    QString accent() const
    {
        return m_accent;
    }
    QColor accentColor() const;
    QString highlight() const { return m_highlight; }
    QColor highlightColor() const;
    QColor highlightTextColor() const;
    // [{name, color}] in menu order.
    Q_INVOKABLE QVariantList highlightChoices() const;
    Q_INVOKABLE bool setHighlight(QString name);
    bool doubleClickCollapse() const
    {
        return m_doubleClickCollapse;
    }
    bool keepScreenAwake() const { return m_keepScreenAwake; }
    QVariantMap clock() const;
    QVariantMap general() const;
    QVariantMap appleMenu() const;
    // One of appleMenu()'s keys: submenus and rememberRecent on or off, recentApplications 1-99.
    Q_INVOKABLE bool setAppleMenuOption(QString key, QVariant value);
    QVariantMap switcher() const;
    int caretFlashTime() const;
    QString defaultWallpaperPath() const;
    QVariantMap monitorPictures() const;
    // monitor "" sets the picture for every monitor and forgets their own ones.
    Q_INVOKABLE bool applyMonitorPicture(QString monitor, QString path, QString layout);
    QString picturesFolder() const;
    Q_INVOKABLE bool apply(int duration, bool stripVisible);
    Q_INVOKABLE bool applyOptions(QString accent, bool doubleClickCollapse);
    Q_INVOKABLE bool setKeepScreenAwake(bool keepAwake);
    // One of clock()'s keys; any other is refused.
    Q_INVOKABLE bool setClockOption(QString key, bool on);
    // One of general()'s keys with a value in its range; anything else is refused.
    Q_INVOKABLE bool setGeneralOption(QString key, QVariant value);
    // Shows (at x, y on the primary display) or hides the Application Switcher.
    Q_INVOKABLE bool setSwitcher(bool visible, int x, int y);
    // The computer's time zone, as the system sets it: {id ("America/Denver"), name ("MST"),
    // offset ("7 hours behind UTC")}.
    Q_INVOKABLE QVariantMap timeZone() const;
    Q_INVOKABLE bool setControlStripCollapsed(bool collapsed);
    Q_INVOKABLE bool setControlStripPosition(int x, int y);
    // Sets the length (0 = every module) and expands the strip, as releasing a tab drag does.
    Q_INVOKABLE bool setControlStripModules(int modules);
    Q_INVOKABLE bool applyDesktopPicture(QString path, QString layout);
    // Tiles the desktop with an 8x8 black-and-white pattern (as desktopPattern), saved as a small
    // PNG in the profile's patterns folder; the previous pattern's file is removed.
    Q_INVOKABLE bool setDesktopPattern(QString pattern);
    QString desktopPattern() const;
    Q_INVOKABLE QUrl previewDesktopPicture(QString path) const;
    Q_INVOKABLE QVariantMap listDesktopPictures(QString folder) const;
    // Collections under a folder (the user's imported Mac OS 9 desktop pictures): [{name, path}],
    // readable real directories only, sorted, at most 32.
    Q_INVOKABLE QVariantList pictureCollections(QString root) const;
  signals:
    void changed();
    void errorOccurred(QString message);

  private:
    void applyCaretFlash() const;
    bool save(int duration, bool visible, bool collapsed, int x, int y, const QString &picture,
              const QString &layout);
    QString m_profile;
    QUrl m_emblem, m_wallpaper;
    QString m_wallpaperPath, m_wallpaperLayout = "fill";
    QMap<QString, std::pair<QString, QString>> m_monitorPictures; // name → (path, layout)
    int m_duration = 240, m_stripX = 8, m_stripY = -1, m_stripModules = 0;
    bool m_stripVisible = true, m_stripCollapsed = false;
    QString m_accent = "blue";
    QString m_highlight = "Purple";
    bool m_doubleClickCollapse = true;
    bool m_keepScreenAwake = false;
    // Shown, 12-hour, no seconds, no weekday or date, steady separators: as before this setting.
    // As 9.2.2 ships: the desktop shown, menus blink three times, medium insertion point.
    bool m_showDesktop = true;
    bool m_appleSubmenus = true, m_rememberRecent = true;
    int m_recentApplications = 10;
    int m_menuBlinks = 3, m_caretBlink = 1;
    int m_barSize = 100;
    bool m_switcherVisible = false;
    int m_switcherX = 0, m_switcherY = 0;
    QMap<QString, bool> m_clock{{"visible", true}, {"hours24", false}, {"seconds", false},
                                {"weekday", false}, {"date", false}, {"flash", false}};
};
