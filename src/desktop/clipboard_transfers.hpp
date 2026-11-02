#pragma once
#include <QByteArray>
#include <memory>

// Owns each submitted FD, including rejected submissions. No Wayland or QObject
// is accessed by a writer. Cancellation outlives the compositor through shared
// state; normal pipe writers notice it within one polling interval.
class ClipboardTransfers
{
  public:
    struct Limits {
        unsigned maxConcurrent = 8;
        int idleTimeoutMs = 30000;
        int totalTimeoutMs = 120000;
        quint64 maxBytes = 64 * 1024 * 1024;
    };
    ClipboardTransfers();
    explicit ClipboardTransfers(Limits);
    ~ClipboardTransfers();
    ClipboardTransfers(const ClipboardTransfers &) = delete;
    ClipboardTransfers &operator=(const ClipboardTransfers &) = delete;
    bool submit(int fd, QByteArray bytes);
    void cancel();
    unsigned active() const;
    quint64 activeBytes() const;

  private:
    struct Control;
    struct Job;
    static void *run(void *);
    std::shared_ptr<Control> m_control;
};
