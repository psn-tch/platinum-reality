#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QWaylandQuickCompositor>
#include <deque>
#include <memory>

class QMimeData;
class QWaylandSeat;

// The MIME types the drag in progress offers (empty when there is none). Read from Qt's own data
// device, which keeps them private.
QStringList dragOfferedTypes(QWaylandSeat *seat);

// The desktop's compositor. Its retained-selection notification remains
// available to the adapter; automatic bounded captures notify it directly.
class PlatinumCompositor : public QWaylandQuickCompositor
{
    Q_OBJECT
  public:
    ~PlatinumCompositor() override;
    // Also announces a modern seat whose pointers get frames (PointerFrames).
    void create() override;

  private:
    class PointerFrames *m_pointerFrames = nullptr;
  signals:
    void selectionRetained(const QMimeData *data);

  protected:
    void retainedSelectionReceived(QMimeData *data) override { emit selectionRetained(data); }
};

// Copy and paste between hosted applications (the Wayland data-device
// selection). Qt 6.12.0's compositor keeps the protocol; this closes its gaps
// without replacing its data device, which desktop drag and drop also uses:
//
// - GTK announces a new selection, adds SAVE_TARGETS to the same source and
//   sets it again. Qt cancels the previous source before installing the new
//   one, so it cancelled that very source and every Firefox/GTK copy vanished.
//   Setting the current source again is now a no-op, as in wlroots.
// - As on Mac OS, the clipboard outlives the application that copied it. Qt's
//   retained cache holds a bounded copy, offered while no live source exists. An
//   application that deliberately clears the clipboard keeps it cleared.
// - The shell's own text fields (rename, search) use the desktop process's
//   clipboard. With a PlatinumCompositor, applications' copies reach it and
//   the shell's copies reach applications. Nothing is read from a clipboard
//   the desktop did not write itself.
// - Edit > Show Clipboard displays what was last copied: text or a picture.
// Automatic retention is capped at 64 MiB / 64 MIME formats / 10 seconds;
// a live application's own clipboard and drag-and-drop are not truncated.
class Clipboard final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QUrl picture READ picture NOTIFY changed)
    // Recent Clippings (Lily Hewitt, 2026-11-12: "copy multiple things and be able to switch between
    // them"), newest first: [{label, kind ("text" or "picture"), thumbnail, current}]. Kept in memory
    // only, never written to disk; a password manager's copies (marked secret) are never kept.
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
  public:
    Clipboard(QWaylandCompositor *, QObject *parent);
    ~Clipboard() override;
    bool active() const { return bool(m_state); }
    // "none", "text" or "picture", as Mac OS 9's Clipboard window reports it.
    QString kind() const { return m_kind; }
    QString text() const { return m_text; }
    QUrl picture() const { return m_picture; }
    // The shell copies text (Edit > Copy of desktop icons): offered to applications.
    Q_INVOKABLE void copyText(const QString &text);
    // The shell copies a picture (Scrapbook): a local PNG or a data: URL; false when unreadable.
    Q_INVOKABLE bool copyImage(const QUrl &picture);

    QVariantList history() const;
    // Make a recent clipping the clipboard again; it moves to the top. False when it is gone.
    Q_INVOKABLE bool restore(int index);
    Q_INVOKABLE void clearHistory();
    static constexpr int historyLimit = 12;

    // For clipboard tools (data_control.cpp), which read and set the clipboard without
    // having the keyboard. The types the clipboard holds now (empty: nothing).
    QStringList selectionMimeTypes() const;
    // Writes one type of what the clipboard holds now to fd, which it takes and closes.
    void sendSelection(const QString &mimeType, int fd);
    // Reserve a tool copy's position in user intent order BEFORE reading it.
    // Retention/source destruction can change selectionGeneration without a
    // new user copy, so that generation is not a completion-order token.
    quint64 beginToolCopy();
    // A NULL tool selection is an explicit new clear, including retained data.
    void clearSelectionFromTool();
    // Commit only if this is still the latest intent. Completion is not a new
    // copy request and cannot supersede a later application or shell copy.
    bool setSelectionFromTool(quint64 intent, std::unique_ptr<QMimeData> data);
    // Counts clipboard changes, so a stale offer can be told from a current one.
    quint64 selectionGeneration() const;
  signals:
    void changed();
    // What the clipboard holds changed (an application copied, quit, cleared it, or a tool set it).
    void selectionChanged();
    void copyIntentChanged(quint64 intent);
    void historyChanged();

  private:
    void retained(const QMimeData *data);
    void shellClipboardChanged();
    void remember(const QMimeData *data);
    void keep(const QMimeData &data);
    struct Clipping
    {
        std::shared_ptr<const QMimeData> data;
        QByteArray key;
        QString kind, label;
        QUrl thumbnail;
        qsizetype bytes = 0;
    };
    std::deque<Clipping> m_history;
    qsizetype m_historyBytes = 0;
    QByteArray m_currentKey;
    struct State;
    std::unique_ptr<State> m_state;
    QString m_kind = "none";
    QString m_text;
    QUrl m_picture;
};
