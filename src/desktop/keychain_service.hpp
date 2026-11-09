#pragma once
#include <QDBusConnection>
#include <QObject>
#include <QVariantList>

// The Control Strip's Keychain module (docs/fidelity/control-strip.md), over the freedesktop
// Secret Service (org.freedesktop.secrets: GNOME Keyring, KeePassXC, ...): each keychain
// (collection) with its label and whether it is locked; Lock and Lock All lock at once, Unlock
// asks through the password store's own prompt, so the desktop never sees a password. With no
// Secret Service on the bus the list is empty. Everything is asynchronous.
class KeychainService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList keychains READ keychains NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
public:
    explicit KeychainService(QDBusConnection bus, QObject *parent = nullptr);
    QVariantList keychains() const { return m_keychains; }
    bool available() const { return m_available; }
    Q_INVOKABLE void lock(const QString &path);
    Q_INVOKABLE void lockAll();
    Q_INVOKABLE void unlock(const QString &path);
public slots:
    void refresh();
signals:
    void changed();
private:
    void call(const QString &method, const QStringList &paths);
    QDBusConnection m_bus;
    QVariantList m_keychains;
    bool m_available = false;
    int m_generation = 0;
};
