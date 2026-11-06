#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <QProcessEnvironment>
#include <functional>

// The HTTP endpoint is independent of the D-Bus system connection. A private or
// blocked system bus must never make a native test use the host's real hhd token.
struct DesktopHandheldEndpoint
{
    QUrl base;
    QString tokenFile;
};
DesktopHandheldEndpoint desktopHandheldEndpoint(bool hasCompositor, const QProcessEnvironment &environment);

// Handheld Daemon (hhd) for the Control Strip's Handheld module: controller emulation, TDP,
// fan and charge limit, as hhd describes them for this device (titles, limits and choices come
// from hhd's own settings). It talks only to hhd's local API on the loopback address, with the
// token hhd writes for its own interface (world-readable by hhd's design). Nothing is sent
// anywhere else. Every request is asynchronous; base URL and token file are injectable for tests.
class HandheldDaemon final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // {controller, tdp, boost, fan, chargeLimit}: each {key, value, ...} and present only when
    // hhd offers it. controller/fan/chargeLimit carry options [{value, title}]; tdp has min, max.
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    // The built-in controller On or Off (psn_tch, 2026-11-06: with an external controller plugged
    // in, games should make that one player 1). Off is hhd's Hidden mode, which hides the built-in
    // controller from games; On goes back to the mode it had before (Xbox when that's unknown).
    Q_PROPERTY(bool controllerSwitchable READ controllerSwitchable NOTIFY changed)
    Q_PROPERTY(bool controllerOn READ controllerOn NOTIFY changed)
  public:
    explicit HandheldDaemon(QUrl base = QUrl("http://127.0.0.1:5335"),
                            QString tokenFile = "/etc/hhd/.token", QObject *parent = nullptr);
    bool available() const
    {
        return m_available;
    }
    // A change on its way to hhd, or the very first look before anything is known. Reading fresh
    // values while the menu is open never counts: the menu's choices stay usable.
    bool busy() const
    {
        return m_setting || (m_refreshing && !m_available);
    }
    QVariantMap state() const
    {
        return m_state;
    }
    Q_INVOKABLE void refresh();
    // A key from state (for example the controller's "...controller_mode.mode") and its new value.
    Q_INVOKABLE void set(const QString &key, const QVariant &value);
    bool controllerSwitchable() const;
    bool controllerOn() const;
    Q_INVOKABLE void setControllerOn(bool on);

  signals:
    void changed();
    void failed(QString message);

  private:
    void request(const QString &path, const QByteArray &body, std::function<void(const QJsonObject &)> done);
    // Emits changed only when something did, unless force (busy() changed).
    void apply(const QJsonObject &state, bool force = false);
    QUrl m_base;
    QString m_tokenFile;
    QNetworkAccessManager m_network;
    QJsonObject m_settings; // hhd's settings tree, read once
    QVariantMap m_state;
    QString m_controllerMode; // the controller's last mode while it was On
    bool m_available = false, m_refreshing = false, m_setting = false;
    int m_generation = 0; // a change made during a refresh outranks that refresh's older answer
};
