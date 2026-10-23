#include "clipboard_transfers.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <new>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <utility>

struct ClipboardTransfers::Control
{
    Limits limits;
    std::atomic<unsigned> active{0};
    std::atomic<bool> cancelled{false};
};
struct ClipboardTransfers::Job
{
    std::shared_ptr<Control> control;
    int fd;
    QByteArray bytes;
};

ClipboardTransfers::ClipboardTransfers() : ClipboardTransfers(Limits{}) {}
ClipboardTransfers::ClipboardTransfers(Limits limits) : m_control(std::make_shared<Control>())
{
    limits.maxConcurrent = std::max(1u, limits.maxConcurrent);
    limits.idleTimeoutMs = std::max(1, limits.idleTimeoutMs);
    limits.totalTimeoutMs = std::max(1, limits.totalTimeoutMs);
    m_control->limits = limits;
}
ClipboardTransfers::~ClipboardTransfers() { cancel(); }
void ClipboardTransfers::cancel() { m_control->cancelled.store(true); }
unsigned ClipboardTransfers::active() const { return m_control->active.load(); }

bool ClipboardTransfers::submit(int fd, QByteArray bytes)
{
    if (fd < 0)
        return false;
    auto fail = [&] { ::close(fd); return false; };
    if (m_control->cancelled.load())
        return fail();
    if (bytes.isEmpty()) {
        ::close(fd);
        return true;
    }
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        return fail();
    auto count = m_control->active.load();
    do {
        if (count >= m_control->limits.maxConcurrent)
            return fail();
    } while (!m_control->active.compare_exchange_weak(count, count + 1));
    auto *job = new (std::nothrow) Job{m_control, fd, std::move(bytes)};
    if (!job) {
        --m_control->active;
        return fail();
    }
    // Detached at creation: no throwing std::thread constructor/detach path can
    // unwind through libwayland's C dispatcher or terminate a joinable temporary.
    pthread_attr_t attributes;
    const int initialized = pthread_attr_init(&attributes);
    int error = initialized;
    if (!error)
        error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    if (!error)
        error = pthread_create(&thread, &attributes, &ClipboardTransfers::run, job);
    if (!initialized)
        pthread_attr_destroy(&attributes);
    if (error) {
        delete job;
        --m_control->active;
        return fail();
    }
    return true;
}

void *ClipboardTransfers::run(void *argument)
{
    std::unique_ptr<Job> job(static_cast<Job *>(argument));
    const auto control = job->control;
    sigset_t pipe;
    sigemptyset(&pipe);
    sigaddset(&pipe, SIGPIPE);
    const bool protectedFromPipe = pthread_sigmask(SIG_BLOCK, &pipe, nullptr) == 0;
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    auto progress = started;
    qsizetype offset = 0;
    while (protectedFromPipe && offset < job->bytes.size() && !control->cancelled.load()) {
        const auto now = Clock::now();
        const auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(now-progress).count();
        const auto total = std::chrono::duration_cast<std::chrono::milliseconds>(now-started).count();
        const auto remaining = std::min(control->limits.idleTimeoutMs-idle,
                                        control->limits.totalTimeoutMs-total);
        if (remaining <= 0)
            break;
        pollfd writable{job->fd, POLLOUT, 0};
        const auto result = ::poll(&writable, 1, int(std::min<long long>(remaining, 100)));
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0 || (writable.revents & (POLLERR | POLLHUP | POLLNVAL)))
            break;
        if (!result)
            continue;
        const auto count = std::min<qsizetype>(64*1024, job->bytes.size()-offset);
        const auto written = ::write(job->fd, job->bytes.constData()+offset, size_t(count));
        if (written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (written <= 0)
            break;
        offset += written;
        progress = Clock::now();
    }
    ::close(job->fd);
    if (protectedFromPipe) {
        const timespec immediate{0,0};
        while (sigtimedwait(&pipe, nullptr, &immediate) > 0) {}
    }
    --control->active;
    return nullptr;
}
