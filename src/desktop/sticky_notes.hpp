#pragma once
#include <QAbstractListModel>
#include <QDateTime>
#include <QTimer>

// Sticky Notes (psn_tch, 2026-11-02): small colored notes on the desktop, kept in the profile's
// stickies.json and restored at login where they were left. Each note is a row; StickyNote.qml
// draws it. Text is saved a moment after typing stops (and when the desktop quits), never on
// every key. Bounded: at most 100 notes of 32,768 characters, inside a 4 MB file.
class StickyNotes final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Hide All Notes: the notes stay saved and come back with Show All Notes.
    Q_PROPERTY(bool hidden READ hidden NOTIFY hiddenChanged)
  public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        TextRole,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        ColorRole,
        TextSizeRole,
        CollapsedRole,
        FloatingRole,
        OrderRole,
        CreatedRole
    };
    static const QStringList &colors();
    static const QStringList &textSizes();
    explicit StickyNotes(QString profile, QObject *parent = nullptr);
    ~StickyNotes() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_notes.size()); }
    bool hidden() const { return m_hidden; }

    // A new empty note, in front; 0 when there are already as many notes as allowed.
    Q_INVOKABLE int create(int x, int y, const QString &color = "yellow");
    // The text as typed; the note's row is not changed (the note shows its own text).
    Q_INVOKABLE void setText(int id, const QString &text);
    Q_INVOKABLE void move(int id, int x, int y);
    Q_INVOKABLE void resize(int id, int width, int height);
    // "color" (one of colors()), "textSize" (one of textSizes()), "collapsed" or "floating" (bool).
    Q_INVOKABLE bool set(int id, const QString &key, const QVariant &value);
    // In front of the other notes.
    Q_INVOKABLE void raise(int id);
    Q_INVOKABLE void remove(int id);
    Q_INVOKABLE void setHidden(bool hidden);
    Q_INVOKABLE QString text(int id) const;
    // Written at once (the desktop quitting, tests).
    Q_INVOKABLE bool saveNow();

  signals:
    void countChanged();
    void hiddenChanged();
    void errorOccurred(QString message);

  private:
    struct Note
    {
        int id = 0;
        QString text;
        int x = 0, y = 0, width = 220, height = 160;
        QString color = "yellow", textSize = "medium";
        bool collapsed = false, floating = false;
        int order = 0;
        QDateTime created;
    };
    int rowOf(int id) const;
    void changed(int row, const QList<int> &roles);
    void scheduleSave();
    void load();
    QString m_profile;
    QList<Note> m_notes;
    bool m_hidden = false;
    int m_nextId = 1;
    QTimer m_saveDelay;
};
