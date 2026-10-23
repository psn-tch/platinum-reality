#pragma once
#include <functional>
#include <QDBusContext>
#include <QMap>
#include <QObject>
#include <QVariantList>
#include <memory>

class IconStore;
class QTimer;

// org.freedesktop.Notifications for applications on the session's private bus, shown as
// Platinum alerts (docs/tray-and-notifications.md). Body markup is not advertised, so bodies
// are plain text. Text sizes, actions, images and the number of alerts are bounded.
class NotificationServer final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
    Q_PROPERTY(QVariantList alerts READ alerts NOTIFY alertsChanged)
  public:
    NotificationServer(bool connectBus, std::shared_ptr<IconStore> icons, QObject *parent = nullptr);
    ~NotificationServer() override;
    bool registered() const
    {
        return m_registered;
    }
    // {id, application, summary, body, icon, actions: [{key, label}], hasDefault}
    QVariantList alerts() const;
    // The user closed the alert.
    Q_INVOKABLE void dismiss(uint id);
    // The user chose an action ("default" for a click on the alert itself).
    Q_INVOKABLE void invoke(uint id, const QString &action);
    // Grants activation tokens for actions; without it no ActivationToken is sent.
    void setActivationTokens(std::function<QString()> grant) { m_activationTokens = std::move(grant); }

  public slots:
    Q_SCRIPTABLE QStringList GetCapabilities();
    Q_SCRIPTABLE uint Notify(const QString &app_name, uint replaces_id, const QString &app_icon,
                             const QString &summary, const QString &body, const QStringList &actions,
                             const QVariantMap &hints, int expire_timeout);
    Q_SCRIPTABLE void CloseNotification(uint id);
    Q_SCRIPTABLE QString GetServerInformation(QString &vendor, QString &version, QString &spec_version);

  signals:
    Q_SCRIPTABLE void NotificationClosed(uint id, uint reason);
    Q_SCRIPTABLE void ActionInvoked(uint id, const QString &action_key);
    // Spec 1.2: sent just before ActionInvoked, so the application may come forward
    // (xdg-activation) when the user acts on its alert.
    Q_SCRIPTABLE void ActivationToken(uint id, const QString &activation_token);
    void alertsChanged();

  private:
    std::function<QString()> m_activationTokens;
    struct Alert
    {
        QString application, summary, body, icon;
        QVariantList actions;
        bool resident = false;
        QTimer *timer = nullptr;
        quint64 order = 0;
    };
    enum Reason : uint
    {
        Expired = 1,
        Dismissed = 2,
        Closed = 3
    };
    void close(uint id, Reason reason);
    QString imageFor(uint id, const QString &icon, const QVariantMap &hints);
    std::shared_ptr<IconStore> m_icons;
    QMap<uint, Alert> m_alerts;
    uint m_nextId = 0;
    quint64 m_nextOrder = 0;
    quint64 m_imageRevision = 0;
    bool m_registered = false;
};
