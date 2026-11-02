#include "desktop/clipboard_reader.hpp"
#include <QDir>
#include <QMimeData>
#include <QScopeGuard>
#include <QtTest>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

class ClipboardReaderTests : public QObject
{
    Q_OBJECT
  private slots:
    void completedTypesPreserveBytesAndPreferText()
    {
        ClipboardReader reader;
        QStringList requested;
        std::unique_ptr<QMimeData> retained;
        QVERIFY(reader.begin({"custom/type", "text/plain"},
            [&](const QString &type, int fd) {
                requested.push_back(type);
                const QByteArray bytes = type == "text/plain" ? QByteArray("hello\0world", 11)
                                                              : QByteArray("other bytes");
                QCOMPARE(::write(fd, bytes.constData(), size_t(bytes.size())), ssize_t(bytes.size()));
            }, [&](auto data) { retained = std::move(data); }));
        QTRY_VERIFY(retained);
        QVERIFY(reader.idle());
        QCOMPARE(requested, QStringList({"text/plain", "custom/type"}));
        QCOMPARE(retained->data("text/plain"), QByteArray("hello\0world", 11));
        QCOMPARE(retained->data("custom/type"), QByteArray("other bytes"));
    }
    void byteLimitDiscardsTheIncompleteTypeAndStillDrains()
    {
        ClipboardReader reader({32, 64, 1000, 16});
        int held = -1;
        const auto close = qScopeGuard([&] { if (held >= 0) ::close(held); });
        std::unique_ptr<QMimeData> retained;
        QVERIFY(reader.begin({"text/plain", "custom/oversized"},
            [&](const QString &type, int fd) {
                if (type == "text/plain")
                    QCOMPARE(::write(fd, "saved", 5), ssize_t(5));
                else
                {
                    held = ::dup(fd);
                    QVERIFY(held >= 0);
                    const QByteArray tooMuch(64, 'x');
                    QCOMPARE(::write(held, tooMuch.constData(), size_t(tooMuch.size())), ssize_t(64));
                }
            }, [&](auto data) { retained = std::move(data); }));
        QTRY_VERIFY(retained);
        QCOMPARE(retained->formats(), QStringList({"text/plain"}));
        QCOMPARE(retained->data("text/plain"), QByteArray("saved"));
        QVERIFY(!reader.idle());
        QVERIFY(!reader.begin({"text/plain"}, [](auto, int) {}, [](auto) {}));
        // The caller's writer remains valid after budget exhaustion: no SIGPIPE.
        QCOMPARE(::write(held, "tail", 4), ssize_t(4));
        ::close(std::exchange(held, -1));
        QTRY_VERIFY(reader.idle());
    }
    void supersededReadsUseOneDrainPipeAndCannotPublishOldBytes()
    {
        ClipboardReader reader;
        int held = -1, sends = 0, callbacks = 0;
        const auto close = qScopeGuard([&] { if (held >= 0) ::close(held); });
        const auto fds = [] { return QDir("/proc/self/fd").entryList(QDir::AllEntries |
                                                                   QDir::NoDotAndDotDot).size(); };
        const auto before = fds();
        const auto send = [&](const QString &, int fd) { ++sends; held = ::dup(fd); };
        QVERIFY(reader.begin({"text/plain"}, send, [&](auto) { ++callbacks; }));
        QVERIFY(held >= 0);
        reader.cancel();
        for (int i = 0; i < 64; ++i)
        {
            reader.cancel();
            QVERIFY(!reader.begin({"text/plain"}, send, [&](auto) { ++callbacks; }));
            QVERIFY(fds() <= before + 2);
        }
        QCOMPARE(sends, 1);
        QCOMPARE(::write(held, "old data", 8), ssize_t(8));
        ::close(std::exchange(held, -1));
        QTRY_VERIFY(reader.idle());
        QCOMPARE(callbacks, 0);
        QTRY_VERIFY(fds() <= before);
        std::unique_ptr<QMimeData> next;
        QVERIFY(reader.begin({"text/plain"}, [](auto, int fd) {
            QCOMPARE(::write(fd, "new data", 8), ssize_t(8));
        }, [&](auto data) { next = std::move(data); }));
        QTRY_VERIFY(next);
        QCOMPARE(next->data("text/plain"), QByteArray("new data"));
    }
    void timeLimitDoesNotCloseTheStalledProducersPipe()
    {
        ClipboardReader reader({64, 64, 200, 16});
        int held = -1;
        const auto close = qScopeGuard([&] { if (held >= 0) ::close(held); });
        std::unique_ptr<QMimeData> retained;
        QVERIFY(reader.begin({"text/plain", "custom/stalled"}, [&](const QString &type, int fd) {
            if (type == "text/plain")
                QCOMPARE(::write(fd, "saved", 5), ssize_t(5));
            else
                held = ::dup(fd);
        }, [&](auto data) { retained = std::move(data); }));
        QTRY_VERIFY_WITH_TIMEOUT(retained, 1000);
        QCOMPARE(retained->formats(), QStringList({"text/plain"}));
        QVERIFY(held >= 0 && !reader.idle());
        QCOMPARE(::write(held, "late bytes", 10), ssize_t(10));
        ::close(std::exchange(held, -1));
        QTRY_VERIFY(reader.idle());
        QCOMPARE(retained->data("text/plain"), QByteArray("saved"));
    }
    void sourceExitCompletesOnlyTheAlreadyRequestedMime()
    {
        ClipboardReader reader;
        int held = -1, sends = 0;
        const auto close = qScopeGuard([&] { if (held >= 0) ::close(held); });
        std::unique_ptr<QMimeData> retained;
        QVERIFY(reader.begin({"text/plain", "custom/never-requested"}, [&](auto, int fd) {
            ++sends;
            held = ::dup(fd);
        }, [&](auto data) { retained = std::move(data); }));
        reader.sourceGone();
        QCOMPARE(::write(held, "at exit", 7), ssize_t(7));
        ::close(std::exchange(held, -1));
        QTRY_VERIFY(retained);
        QCOMPARE(sends, 1);
        QCOMPARE(retained->formats(), QStringList({"text/plain"}));
        QCOMPARE(retained->data("text/plain"), QByteArray("at exit"));
    }
    void formatsAndMimeNameLengthsAreBounded()
    {
        ClipboardReader reader;
        QStringList types = {QString(), QString(4097, 'x'), QString::fromUtf8("bad\0type", 8)};
        for (int i = 0; i < 100; ++i)
            types.push_back(QString("custom/type-%1").arg(i));
        types.append(types);
        int sends = 0;
        std::unique_ptr<QMimeData> retained;
        QVERIFY(reader.begin(types, [&](auto, int fd) {
            ++sends;
            QCOMPARE(::write(fd, "x", 1), ssize_t(1));
        }, [&](auto data) { retained = std::move(data); }));
        QTRY_VERIFY(retained);
        QCOMPARE(sends, 64);
        QCOMPARE(retained->formats().size(), 64);
    }
};
QTEST_GUILESS_MAIN(ClipboardReaderTests)
#include "clipboard_reader_tests.moc"
