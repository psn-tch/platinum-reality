#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantMap>

// Scrapbook, the desk accessory: text and pictures kept from the clipboard, in order, in the
// profile's scrapbook folder (index.json and one file an item). Bounded: 100 items, 1 MB of text
// or a 32 MB picture each.
class Scrapbook final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
  public:
    static constexpr int maximumItems = 100;
    explicit Scrapbook(QString profile, QObject *parent = nullptr);
    int count() const { return int(m_items.size()); }
    // {kind ("text" or "picture"), text, picture (file URL), size (bytes), width, height}.
    Q_INVOKABLE QVariantMap item(int index) const;
    // Inserted before `at` (count() appends); false when full or unreadable.
    Q_INVOKABLE bool addText(const QString &text, int at);
    // A data: URL or file URL of a picture (Clipboard.picture).
    Q_INVOKABLE bool addPicture(const QUrl &picture, int at);
    Q_INVOKABLE bool remove(int index);

  signals:
    void changed();
    void errorOccurred(QString message);

  private:
    struct Item
    {
        QString kind, file;
        qint64 size = 0;
        int width = 0, height = 0;
    };
    QString folder() const;
    bool saveIndex();
    void load();
    bool insert(Item item, const QByteArray &bytes, int at);
    QString m_profile;
    QList<Item> m_items;
};
