#include "scrapbook.hpp"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace
{
constexpr qint64 maximumText = 1024 * 1024;
constexpr qint64 maximumPicture = 32 * 1024 * 1024;
constexpr qint64 maximumIndex = 256 * 1024;
bool validName(const QString &name)
{
    // Only names this class writes: no paths, no links out of the folder.
    static const QRegularExpression pattern("^[0-9a-f]{16}\\.(txt|png)$");
    return pattern.match(name).hasMatch();
}
QByteArray readBounded(const QString &path, qint64 limit)
{
    QFile file(path);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > limit)
        return {};
    return file.read(limit + 1);
}
} // namespace

Scrapbook::Scrapbook(QString profile, QObject *parent)
    : QObject(parent), m_profile(QDir(profile).absolutePath())
{
    load();
}
QString Scrapbook::folder() const
{
    return m_profile + "/scrapbook";
}
void Scrapbook::load()
{
    const auto bytes = readBounded(folder() + "/index.json", maximumIndex);
    const auto object = QJsonDocument::fromJson(bytes).object();
    if (object.value("version").toInt() != 1)
        return;
    for (const auto &value : object.value("items").toArray())
    {
        const auto entry = value.toObject();
        Item item{entry.value("kind").toString(), entry.value("file").toString(),
                  entry.value("size").toInteger(), entry.value("width").toInt(), entry.value("height").toInt()};
        if ((item.kind != "text" && item.kind != "picture") || !validName(item.file) ||
            item.file.endsWith(".png") != (item.kind == "picture") || !QFileInfo(folder() + "/" + item.file).isFile() ||
            QFileInfo(folder() + "/" + item.file).isSymLink())
            continue;
        m_items.append(item);
        if (m_items.size() == maximumItems)
            break;
    }
}
bool Scrapbook::saveIndex()
{
    QJsonArray items;
    for (const auto &item : m_items)
        items.append(QJsonObject{{"kind", item.kind}, {"file", item.file}, {"size", item.size},
                                 {"width", item.width}, {"height", item.height}});
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"items", items}}).toJson();
    QSaveFile file(folder() + "/index.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        emit errorOccurred("The Scrapbook could not be saved: " + file.errorString());
        return false;
    }
    return true;
}
bool Scrapbook::insert(Item item, const QByteArray &bytes, int at)
{
    if (m_items.size() >= maximumItems)
    {
        emit errorOccurred("The Scrapbook is full. Clear an item first.");
        return false;
    }
    if (!QDir().mkpath(folder()))
    {
        emit errorOccurred("The Scrapbook folder could not be created.");
        return false;
    }
    item.file = QString::number(QRandomGenerator::global()->generate64(), 16).rightJustified(16, '0') +
                (item.kind == "picture" ? ".png" : ".txt");
    QSaveFile file(folder() + "/" + item.file);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        emit errorOccurred("The Scrapbook item could not be saved: " + file.errorString());
        return false;
    }
    item.size = bytes.size();
    m_items.insert(std::clamp(at, 0, int(m_items.size())), item);
    if (!saveIndex())
    {
        m_items.removeAt(std::clamp(at, 0, int(m_items.size()) - 1));
        QFile::remove(folder() + "/" + item.file);
        return false;
    }
    emit changed();
    return true;
}
bool Scrapbook::addText(const QString &text, int at)
{
    const auto bytes = text.toUtf8();
    if (text.isEmpty() || bytes.size() > maximumText)
        return false;
    return insert({"text", {}, 0, 0, 0}, bytes, at);
}
bool Scrapbook::addPicture(const QUrl &picture, int at)
{
    QByteArray encoded;
    if (picture.scheme() == "data")
    {
        const auto url = picture.toString(QUrl::FullyEncoded).toLatin1();
        const auto comma = url.indexOf(',');
        if (comma < 0 || !url.left(comma).endsWith(";base64"))
            return false;
        encoded = QByteArray::fromBase64(url.mid(comma + 1));
    }
    else if (picture.isLocalFile())
        encoded = readBounded(picture.toLocalFile(), maximumPicture);
    if (encoded.isEmpty() || encoded.size() > maximumPicture)
        return false;
    QBuffer input(&encoded);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 16 * 1024 * 1024)
        return false;
    const auto image = reader.read();
    if (image.isNull())
        return false;
    // Kept as PNG whatever it arrived as.
    QByteArray png;
    QBuffer output(&png);
    output.open(QIODevice::WriteOnly);
    if (!image.save(&output, "PNG") || png.size() > maximumPicture)
        return false;
    return insert({"picture", {}, 0, image.width(), image.height()}, png, at);
}
bool Scrapbook::remove(int index)
{
    if (index < 0 || index >= m_items.size())
        return false;
    const auto item = m_items.takeAt(index);
    if (!saveIndex())
    {
        m_items.insert(index, item);
        return false;
    }
    QFile::remove(folder() + "/" + item.file);
    emit changed();
    return true;
}
QVariantMap Scrapbook::item(int index) const
{
    if (index < 0 || index >= m_items.size())
        return {};
    const auto &item = m_items[index];
    const auto path = folder() + "/" + item.file;
    QVariantMap result{{"kind", item.kind}, {"size", item.size}, {"width", item.width}, {"height", item.height}};
    if (item.kind == "text")
        result.insert("text", QString::fromUtf8(readBounded(path, maximumText)));
    else
        result.insert("picture", QUrl::fromLocalFile(path));
    return result;
}
