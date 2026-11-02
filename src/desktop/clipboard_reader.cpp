#include "clipboard_reader.hpp"
#include <QMimeData>
#include <QSocketNotifier>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

ClipboardReader::ClipboardReader(QObject *parent) : ClipboardReader(Limits{}, parent) {}
ClipboardReader::ClipboardReader(Limits limits, QObject *parent) : QObject(parent), m_limits(limits)
{
    m_limits.maxBytes = std::max<qsizetype>(1, m_limits.maxBytes);
    m_limits.maxFormats = std::clamp(m_limits.maxFormats, 1, 64);
    m_limits.timeoutMs = std::max(1, m_limits.timeoutMs);
    m_limits.perActivation = std::max<qsizetype>(1, m_limits.perActivation);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { stop(true); });
}
ClipboardReader::~ClipboardReader() { closeRead(); }
bool ClipboardReader::idle() const { return m_mode == Mode::Idle; }

bool ClipboardReader::begin(QStringList types, Send send, Done done)
{
    if (!idle() || !send)
        return false;
    m_types.clear();
    // Prefer generally useful formats if a later oversized/stalled format
    // prevents a complete capture. Never retain a partial MIME payload.
    for (const auto &type : {QStringLiteral("text/plain;charset=utf-8"),
                             QStringLiteral("text/plain"), QStringLiteral("image/png")})
        if (types.contains(type) && m_types.size() < m_limits.maxFormats)
            m_types.push_back(type);
    for (const auto &type : types)
        if (m_types.size() < m_limits.maxFormats && !m_types.contains(type) &&
            !type.isEmpty() && !type.contains(QChar(0)) && type.toUtf8().size() <= 4096)
            m_types.push_back(type);
    m_index = 0;
    m_completedBytes = 0;
    m_buffer.clear();
    m_data = std::make_unique<QMimeData>();
    m_send = std::move(send);
    m_done = std::move(done);
    m_mode = Mode::Capturing;
    m_timer.start(m_limits.timeoutMs);
    next();
    return true;
}

void ClipboardReader::closeRead()
{
    delete m_notifier;
    m_notifier = nullptr;
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}
void ClipboardReader::announceAvailable()
{
    QMetaObject::invokeMethod(this, [this] {
        if (idle() && available)
            available();
    }, Qt::QueuedConnection);
}
void ClipboardReader::finished()
{
    closeRead();
    m_timer.stop();
    m_mode = Mode::Idle;
    m_send = {};
    m_types.clear();
    m_buffer.clear();
    m_completedBytes = 0;
    auto done = std::move(m_done);
    auto data = std::move(m_data);
    announceAvailable();
    if (done)
        done(std::move(data));
}
void ClipboardReader::stop(bool publishCompleted)
{
    if (m_mode != Mode::Capturing)
        return;
    m_timer.stop();
    m_send = {};
    m_types.clear();
    m_buffer.clear(); // The current MIME is incomplete: do not offer it.
    m_completedBytes = 0;
    auto done = std::move(m_done);
    auto data = std::move(m_data);
    m_mode = m_fd >= 0 ? Mode::Draining : Mode::Idle;
    if (idle())
        announceAvailable();
    if (publishCompleted && done)
        done(std::move(data));
}
void ClipboardReader::cancel() { stop(false); }
void ClipboardReader::sourceGone() { m_send = {}; }

void ClipboardReader::next()
{
    if (!m_send || m_index >= m_types.size())
    {
        finished();
        return;
    }
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0)
    {
        finished();
        return;
    }
    if (::fcntl(fds[0], F_SETFL, O_NONBLOCK) < 0)
    {
        ::close(fds[0]);
        ::close(fds[1]);
        finished();
        return;
    }
    m_fd = fds[0];
    m_buffer.clear();
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &ClipboardReader::readable);
    m_send(m_types.at(m_index), fds[1]);
    ::close(fds[1]);
}
void ClipboardReader::readable()
{
    char chunk[65536];
    qsizetype consumed = 0;
    while (consumed < m_limits.perActivation)
    {
        const auto count = ::read(m_fd, chunk,
            size_t(std::min<qsizetype>(sizeof chunk, m_limits.perActivation - consumed)));
        if (count > 0)
        {
            consumed += count;
            if (m_mode == Mode::Capturing)
            {
                if (count > m_limits.maxBytes - m_completedBytes - m_buffer.size())
                    stop(true);
                else
                    m_buffer.append(chunk, count);
            }
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        closeRead();
        if (m_mode == Mode::Draining)
        {
            m_mode = Mode::Idle;
            announceAvailable();
        }
        else if (count == 0)
        {
            m_completedBytes += m_buffer.size();
            m_data->setData(m_types.at(m_index), std::move(m_buffer));
            ++m_index;
            next();
        }
        else
            finished(); // Read error: keep only previously completed types.
        return;
    }
}
