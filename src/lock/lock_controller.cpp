#include "lock_controller.hpp"
#include "authenticator.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

LockController::LockController(Authenticator *authenticator, QString fullName, QUrl picture,
                               QUrl desktopPicture, QObject *parent)
    : QObject(parent), m_authenticator(authenticator), m_fullName(std::move(fullName)),
      m_picture(std::move(picture)), m_desktopPicture(std::move(desktopPicture))
{
    connect(m_authenticator, &Authenticator::message, this,
            [this](const QString &text, bool error)
            {
                // PAM's own words (an account lockout, an expired password) beat our generic ones.
                m_pamMessage = true;
                showStatus(text, error);
            });
    connect(m_authenticator, &Authenticator::finished, this,
            [this](bool success)
            {
                if (success)
                {
                    showStatus(tr("Logging in…"), false);
                    emit authenticated();
                    return;
                }
                ++m_failures;
                if (!m_pamMessage || !m_messageIsError)
                    showStatus(tr("The password is incorrect. Please try again."), true);
                emit rejected();
                emit changed();
            });
}

bool LockController::busy() const
{
    return m_authenticator->busy();
}

bool LockController::submit(const QString &password)
{
    if (!m_ready || password.isEmpty() || !m_authenticator->submit(password))
        return false;
    m_pamMessage = false;
    showStatus(tr("Checking your password…"), false);
    return true;
}

void LockController::setReady(bool ready)
{
    m_ready = ready;
    emit changed();
}

void LockController::setKeyboardOnLock(bool keyboard)
{
    if (m_keyboard == keyboard)
        return;
    m_keyboard = keyboard;
    emit changed();
}

void LockController::setFingerprintAvailable(bool available)
{
    m_fingerprint = available;
    emit changed();
}

void LockController::showStatus(const QString &text, bool error)
{
    m_message = text;
    m_messageIsError = error;
    emit changed();
}

QUrl userPicture(const QString &user, const QString &home)
{
    for (const auto &candidate : {home + "/.face.icon", home + "/.face", "/var/lib/AccountsService/icons/" + user})
    {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isReadable() && info.size() > 0 && info.size() < 4 * 1024 * 1024)
            return QUrl::fromLocalFile(info.canonicalFilePath());
    }
    return {};
}

QString userFullName(const QString &user, const QString &gecos)
{
    const auto name = gecos.section(',', 0, 0).trimmed();
    return name.isEmpty() ? user : name;
}

QUrl desktopPictureFrom(const QString &profile, const QString &home)
{
    QFile settings(QDir(profile).filePath("appearance.json"));
    if (!settings.open(QIODevice::ReadOnly) || settings.size() > 1024 * 1024)
        return {};
    auto path = QJsonDocument::fromJson(settings.readAll()).object().value("wallpaperPath").toString();
    if (path.startsWith("~/"))
        path = home + path.mid(1);
    const QFileInfo picture(path);
    if (path.isEmpty() || !picture.isAbsolute() || !picture.isFile() || !picture.isReadable() ||
        picture.size() > 64 * 1024 * 1024)
        return {};
    return QUrl::fromLocalFile(picture.absoluteFilePath());
}

void LockController::setSession(const QString &session)
{
    if (!m_loginMode || (session != "platinum" && session != "kde") || session == m_session)
        return;
    m_session = session;
    emit changed();
}

void LockController::setLoginMode(const QString &defaultSession)
{
    m_loginMode = true;
    m_session = defaultSession == "kde" ? "kde" : "platinum";
}
