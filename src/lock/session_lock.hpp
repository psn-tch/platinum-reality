#pragma once
#include <QObject>
#include <memory>

struct ext_session_lock_manager_v1;
struct ext_session_lock_v1;
class QWindow;

// The compositor's session lock (ext-session-lock-v1). Once locked(), every output shows only
// this process's lock surfaces, and it stays locked if the process dies: only unlock() ends it.
// Qt private API (QtWaylandClient shell integration), confined to session_lock.cpp.
class SessionLock final : public QObject
{
    Q_OBJECT
  public:
    explicit SessionLock(QObject *parent = nullptr);
    ~SessionLock() override;
    bool available() const { return m_manager; }
    // Requests the lock; locked() or finished() follows.
    bool lock();
    // Before show(): this window becomes the lock surface for its screen.
    bool cover(QWindow *window);
    // Only after locked(): ends the lock. Returns once the compositor has the request.
    bool unlock();
    bool isLocked() const { return m_locked; }

  signals:
    void locked();
    // The compositor refused the lock, or ended it itself.
    void finished();

  private:
    static void handleLocked(void *data, ext_session_lock_v1 *lock);
    static void handleFinished(void *data, ext_session_lock_v1 *lock);
    class Integration;
    ext_session_lock_manager_v1 *m_manager = nullptr;
    ext_session_lock_v1 *m_lock = nullptr;
    bool m_locked = false;
    std::unique_ptr<Integration> m_integration;
};
