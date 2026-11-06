#include "note_pad.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
constexpr int maximumText = 32768;
constexpr qint64 maximumFile = 1024 * 1024;
} // namespace

NotePad::NotePad(QString profile, QObject *parent)
    : QObject(parent), m_profile(QDir(profile).absolutePath())
{
    for (int i = 0; i < pages; ++i)
        m_text.append(QString());
    m_saveDelay.setSingleShot(true);
    m_saveDelay.setInterval(600);
    connect(&m_saveDelay, &QTimer::timeout, this, &NotePad::saveNow);
    load();
}
NotePad::~NotePad()
{
    if (m_dirty)
        saveNow();
}
void NotePad::load()
{
    QFile input(m_profile + "/notepad.json");
    if (QFileInfo(input.fileName()).isSymLink() || !input.open(QIODevice::ReadOnly) ||
        input.size() > maximumFile)
        return;
    const auto object = QJsonDocument::fromJson(input.read(maximumFile + 1)).object();
    if (object.value("version").toInt() != 1)
        return;
    const auto stored = object.value("pages").toArray();
    for (int i = 0; i < pages && i < stored.size(); ++i)
        if (stored[i].isString())
            m_text[i] = stored[i].toString().left(maximumText);
    if (const auto page = object.value("page").toInt(-1); page >= 0 && page < pages)
        m_page = page;
}
void NotePad::setPage(int page)
{
    if (page < 0 || page >= pages || page == m_page)
        return;
    m_page = page;
    m_dirty = true;
    m_saveDelay.start();
    emit pageChanged();
}
QString NotePad::text(int page) const
{
    return page >= 0 && page < pages ? m_text[page] : QString();
}
void NotePad::setText(int page, const QString &text)
{
    if (page < 0 || page >= pages || m_text[page] == text)
        return;
    m_text[page] = text.left(maximumText);
    m_dirty = true;
    m_saveDelay.start();
}
bool NotePad::saveNow()
{
    m_saveDelay.stop();
    if (!QDir().mkpath(m_profile))
    {
        emit errorOccurred("The Note Pad folder could not be created.");
        return false;
    }
    QJsonArray stored;
    for (const auto &page : m_text)
        stored.append(page);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"page", m_page}, {"pages", stored}}).toJson();
    QSaveFile file(m_profile + "/notepad.json");
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        emit errorOccurred("Note Pad could not be saved: " + file.errorString());
        return false;
    }
    m_dirty = false;
    return true;
}
