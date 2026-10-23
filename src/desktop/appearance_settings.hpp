#pragma once
#include "appearance/interface_font.hpp"
#include <QColor>
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
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)
    Q_PROPERTY(QUrl menuEmblem READ menuEmblem CONSTANT)
    Q_PROPERTY(QUrl wallpaper READ wallpaper NOTIFY changed)
    Q_PROPERTY(QString wallpaperPath READ wallpaperPath NOTIFY changed)
    Q_PROPERTY(QString wallpaperLayout READ wallpaperLayout NOTIFY changed)
    Q_PROPERTY(QString defaultWallpaperPath READ defaultWallpaperPath CONSTANT)
    Q_PROPERTY(QString picturesFolder READ picturesFolder CONSTANT)
    // Appearance → Appearance variation: "blue" (default) or "graphite" (F01, V922 E11).
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY changed)
    // Appearance → Options: double-click a title bar to collapse the window (on by default).
    Q_PROPERTY(bool doubleClickCollapse READ doubleClickCollapse NOTIFY changed)
    Q_PROPERTY(bool keepScreenAwake READ keepScreenAwake NOTIFY changed)
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
    bool doubleClickCollapse() const
    {
        return m_doubleClickCollapse;
    }
    bool keepScreenAwake() const { return m_keepScreenAwake; }
    QString defaultWallpaperPath() const;
    QString picturesFolder() const;
    Q_INVOKABLE bool apply(int duration, bool stripVisible);
    Q_INVOKABLE bool applyOptions(QString accent, bool doubleClickCollapse);
    Q_INVOKABLE bool setKeepScreenAwake(bool keepAwake);
    Q_INVOKABLE bool setControlStripCollapsed(bool collapsed);
    Q_INVOKABLE bool setControlStripPosition(int x, int y);
    Q_INVOKABLE bool applyDesktopPicture(QString path, QString layout);
    Q_INVOKABLE QUrl previewDesktopPicture(QString path) const;
    Q_INVOKABLE QVariantMap listDesktopPictures(QString folder) const;
    // Collections under a folder (the user's imported Mac OS 9 desktop pictures): [{name, path}],
    // readable real directories only, sorted, at most 32.
    Q_INVOKABLE QVariantList pictureCollections(QString root) const;
  signals:
    void changed();
    void errorOccurred(QString message);

  private:
    bool save(int duration, bool visible, bool collapsed, int x, int y, const QString &picture,
              const QString &layout);
    QString m_profile;
    QUrl m_emblem, m_wallpaper;
    QString m_wallpaperPath, m_wallpaperLayout = "fill";
    int m_duration = 240, m_stripX = 8, m_stripY = -1;
    bool m_stripVisible = true, m_stripCollapsed = false;
    QString m_accent = "blue";
    bool m_doubleClickCollapse = true;
    bool m_keepScreenAwake = false;
};
