#pragma once
#include <QObject>
#include <QStringList>
#include <QTimer>

// Note Pad, the desk accessory: eight pages of plain text in the profile's notepad.json, and the
// page that was open. Text is saved a moment after typing stops and when the desktop quits.
// Bounded: 32,768 characters a page, a 1 MB file.
class NotePad final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int pageCount READ pageCount CONSTANT)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
  public:
    static constexpr int pages = 8;
    explicit NotePad(QString profile, QObject *parent = nullptr);
    ~NotePad() override;
    int pageCount() const { return pages; }
    int page() const { return m_page; }
    // 0-based; outside the pages is refused.
    void setPage(int page);
    Q_INVOKABLE QString text(int page) const;
    Q_INVOKABLE void setText(int page, const QString &text);
    // Written at once (the desktop quitting, tests).
    Q_INVOKABLE bool saveNow();

  signals:
    void pageChanged();
    void errorOccurred(QString message);

  private:
    void load();
    QString m_profile;
    QStringList m_text;
    int m_page = 0;
    bool m_dirty = false;
    QTimer m_saveDelay;
};
