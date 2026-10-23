#pragma once
#include <QObject>
#include <QUrl>
#include <QWaylandQuickCompositor>
#include <memory>

class QMimeData;

// The desktop's compositor. It reports each copy of the clipboard it keeps
// (Qt's retained selection), so the shell can show and share it.
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
//   retained selection keeps a copy, offered while no live source exists. An
//   application that deliberately clears the clipboard keeps it cleared.
// - The shell's own text fields (rename, search) use the desktop process's
//   clipboard. With a PlatinumCompositor, applications' copies reach it and
//   the shell's copies reach applications. Nothing is read from a clipboard
//   the desktop did not write itself.
// - Edit > Show Clipboard displays what was last copied: text or a picture.
class Clipboard final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QUrl picture READ picture NOTIFY changed)
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

    // For clipboard tools (data_control.cpp), which read and set the clipboard without
    // having the keyboard. The types the clipboard holds now (empty: nothing).
    QStringList selectionMimeTypes() const;
    // Writes one type of what the clipboard holds now to fd, which it takes and closes.
    void sendSelection(const QString &mimeType, int fd);
    // A tool set the clipboard; its data was already read.
    void setSelectionFromTool(std::unique_ptr<QMimeData> data);
    // Counts clipboard changes, so a stale offer can be told from a current one.
    quint64 selectionGeneration() const;
  signals:
    void changed();
    // What the clipboard holds changed (an application copied, quit, cleared it, or a tool set it).
    void selectionChanged();

  private:
    void retained(const QMimeData *data);
    void shellClipboardChanged();
    void remember(const QMimeData *data);
    struct State;
    std::unique_ptr<State> m_state;
    QString m_kind = "none";
    QString m_text;
    QUrl m_picture;
};
