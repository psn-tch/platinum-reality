#pragma once
#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>

class QMimeData;
class QSocketNotifier;

// One automatic clipboard capture, without Wayland or a Qt private API. A Send
// callback borrows the write FD for its duration; it must send/duplicate it, not
// close it. A cancelled producer may still write, so keep at most ONE pipe in
// discard mode until EOF instead of giving the client SIGPIPE or accumulating
// an obsolete pipe on every copy. Live application-to-application paste is
// independent of this optional retention service.
class ClipboardReader final : public QObject
{
  public:
    struct Limits
    {
        qsizetype maxBytes = 64 * 1024 * 1024;
        int maxFormats = 64;
        int timeoutMs = 10000;
        qsizetype perActivation = 1024 * 1024;
    };
    using Send = std::function<void(const QString &, int)>;
    using Done = std::function<void(std::unique_ptr<QMimeData>)>;
    explicit ClipboardReader(QObject *parent = nullptr);
    ClipboardReader(Limits, QObject *parent = nullptr);
    ~ClipboardReader() override;
    bool idle() const;
    bool begin(QStringList, Send, Done);
    void cancel();
    // Finish the in-flight MIME, but never request more from a dead source.
    void sourceGone();
    // Queued after the single pipe is free. The owner may capture its latest
    // still-current selection; the reader never keeps a queue of old sources.
    std::function<void()> available;

  private:
    enum class Mode { Idle, Capturing, Draining };
    void next();
    void readable();
    void closeRead();
    void stop(bool publishCompleted);
    void finished();
    void announceAvailable();
    Limits m_limits;
    Mode m_mode = Mode::Idle;
    QTimer m_timer;
    QSocketNotifier *m_notifier = nullptr;
    int m_fd = -1;
    QStringList m_types;
    int m_index = 0;
    qsizetype m_completedBytes = 0;
    QByteArray m_buffer;
    std::unique_ptr<QMimeData> m_data;
    Send m_send;
    Done m_done;
};
