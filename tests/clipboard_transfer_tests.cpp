#include "desktop/clipboard_transfers.hpp"
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QtTest>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <utility>
#include <unistd.h>

namespace {
std::atomic<bool> refuseNextThread{false};
std::atomic<int> closeReaderBeforeWrite{-1};
struct Pipe {
    int reader=-1, writer=-1;
    Pipe()
    {
        int fds[2];
        if (!pipe2(fds,O_CLOEXEC)) {
            reader=fds[0];writer=fds[1];
            // Only the reader starts nonblocking. The implementation must set
            // O_NONBLOCK on the submitted writer to bound a large stalled write.
            fcntl(reader,F_SETFL,fcntl(reader,F_GETFL)|O_NONBLOCK);
        }
    }
    ~Pipe() { if(reader>=0)::close(reader);if(writer>=0)::close(writer); }
    int takeWriter() {return std::exchange(writer,-1);}
};
bool readToEnd(int fd,QByteArray &bytes,int timeout=3000)
{
    QElapsedTimer timer;timer.start();
    char buffer[16384];
    while(timer.elapsed()<timeout) {
        const auto count=::read(fd,buffer,sizeof buffer);
        if(count>0) {bytes.append(buffer,count);continue;}
        if(!count)return true;
        if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)return false;
        pollfd readable{fd,POLLIN,0};::poll(&readable,1,20);
    }
    return false;
}
}

extern "C" int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
extern "C" ssize_t __real_write(int,const void *,size_t);
extern "C" ssize_t __wrap_write(int fd,const void *bytes,size_t size)
{
    // Close only our owned pipe, after poll reported it writable. This forces
    // write itself to raise SIGPIPE, rather than testing just POLLERR handling.
    const int reader=closeReaderBeforeWrite.exchange(-1);
    if(reader>=0)::close(reader);
    return __real_write(fd,bytes,size);
}
extern "C" int __wrap_pthread_create(pthread_t *thread,const pthread_attr_t *attributes,
                                      void *(*run)(void *),void *data)
{
    if(refuseNextThread.exchange(false))return EAGAIN;
    return __real_pthread_create(thread,attributes,run,data);
}

class ClipboardTransferTests : public QObject
{
    Q_OBJECT
  private slots:
    void successfulTransferPreservesEveryByte()
    {
        ClipboardTransfers transfers;
        Pipe pipe;QVERIFY(pipe.reader>=0);
        QByteArray expected(1024*1024,Qt::Uninitialized);
        for(qsizetype i=0;i<expected.size();++i)expected[i]=char(i%251);
        QVERIFY(transfers.submit(pipe.takeWriter(),expected));
        QByteArray received;QVERIFY(readToEnd(pipe.reader,received));
        QCOMPARE(received,expected);
        QTRY_COMPARE_WITH_TIMEOUT(transfers.active(),0u,1000);
    }
    void backpressureIsBoundedAndIdleReadersAreAbandoned()
    {
        ClipboardTransfers transfers({2,500,3000});
        Pipe first,second,rejected;
        QVERIFY(first.reader>=0 && second.reader>=0 && rejected.reader>=0);
        QVERIFY(!(fcntl(first.writer,F_GETFL)&O_NONBLOCK));
        // Smaller than the writer's chunk: poll-writable must not be mistaken
        // for permission to make an arbitrarily large blocking write.
        QCOMPARE(fcntl(first.writer,F_SETPIPE_SZ,4096),4096);
        QCOMPARE(fcntl(second.writer,F_SETPIPE_SZ,4096),4096);
        QElapsedTimer timer;timer.start();
        QVERIFY(transfers.submit(first.takeWriter(),QByteArray(4*1024*1024,'a')));
        QVERIFY(transfers.submit(second.takeWriter(),QByteArray(4*1024*1024,'b')));
        QVERIFY(timer.elapsed()<1000); // The caller never drains either reader.
        QCOMPARE(transfers.active(),2u);
        QVERIFY(!transfers.submit(rejected.takeWriter(),QByteArray("not admitted")));
        char byte;QCOMPARE(::read(rejected.reader,&byte,1),ssize_t(0));
        QTRY_COMPARE_WITH_TIMEOUT(transfers.active(),0u,3000);
        QByteArray discarded;
        QVERIFY(readToEnd(first.reader,discarded));
        QVERIFY(discarded.size()<4*1024*1024);
    }
    void earlyReaderCloseCannotDeliverProcessWideSigpipe()
    {
        struct sigaction previous{},plain{};plain.sa_handler=SIG_DFL;
        sigemptyset(&plain.sa_mask);
        QVERIFY(sigaction(SIGPIPE,&plain,&previous)==0);
        const auto restore=qScopeGuard([&]{sigaction(SIGPIPE,&previous,nullptr);});
        ClipboardTransfers transfers;
        Pipe pipe;QVERIFY(pipe.reader>=0);
        closeReaderBeforeWrite.store(std::exchange(pipe.reader,-1));
        const auto closeOnFailure=qScopeGuard([] {
            const int reader=closeReaderBeforeWrite.exchange(-1);
            if(reader>=0)::close(reader);
        });
        QVERIFY(transfers.submit(pipe.takeWriter(),QByteArray(1024*1024,'x')));
        QTRY_COMPARE_WITH_TIMEOUT(transfers.active(),0u,1000);
        QCOMPARE(closeReaderBeforeWrite.load(),-1);
        struct sigaction after{};QVERIFY(sigaction(SIGPIPE,nullptr,&after)==0);
        QVERIFY(after.sa_handler==SIG_DFL); // No process-wide SIGPIPE ignore workaround.
    }
    void totalDeadlineDoesNotDependOnIdleTimeout()
    {
        ClipboardTransfers transfers({1,3000,150});
        Pipe pipe;QVERIFY(pipe.reader>=0);
        QVERIFY(transfers.submit(pipe.takeWriter(),QByteArray(4*1024*1024,'x')));
        QTRY_COMPARE_WITH_TIMEOUT(transfers.active(),0u,1500);
        QByteArray discarded;QVERIFY(readToEnd(pipe.reader,discarded));
        QVERIFY(discarded.size()<4*1024*1024);
    }
    void threadCreationFailureClosesTheFdAndReleasesAdmission()
    {
        ClipboardTransfers transfers({1,100,1000});
        Pipe refused;QVERIFY(refused.reader>=0);
        refuseNextThread.store(true);
        QVERIFY(!transfers.submit(refused.takeWriter(),QByteArray("failure injection")));
        QCOMPARE(transfers.active(),0u);
        char byte;QCOMPARE(::read(refused.reader,&byte,1),ssize_t(0));
        Pipe next;QVERIFY(transfers.submit(next.takeWriter(),QByteArray("next request")));
        QByteArray bytes;QVERIFY(readToEnd(next.reader,bytes));
        QCOMPARE(bytes,QByteArray("next request"));
    }
    void cancellationOutlivesTheOwnerWithoutBlockingItsDestructor()
    {
        Pipe pipe;QVERIFY(pipe.reader>=0);
        auto transfers=std::make_unique<ClipboardTransfers>();
        QVERIFY(transfers->submit(pipe.takeWriter(),QByteArray(4*1024*1024,'x')));
        pollfd readable{pipe.reader,POLLIN,0};QVERIFY(::poll(&readable,1,1000)>0);
        QElapsedTimer timer;timer.start();transfers.reset();QVERIFY(timer.elapsed()<500);
        QByteArray discarded;QVERIFY(readToEnd(pipe.reader,discarded));
        QVERIFY(discarded.size()<4*1024*1024);
    }
    void emptyAndCancelledSubmissionsCloseTheirFds()
    {
        ClipboardTransfers transfers;
        Pipe empty;QVERIFY(transfers.submit(empty.takeWriter(),{}));
        char byte;QCOMPARE(::read(empty.reader,&byte,1),ssize_t(0));
        transfers.cancel();Pipe cancelled;
        QVERIFY(!transfers.submit(cancelled.takeWriter(),QByteArray("later")));
        QCOMPARE(::read(cancelled.reader,&byte,1),ssize_t(0));
    }
};
QTEST_GUILESS_MAIN(ClipboardTransferTests)
#include "clipboard_transfer_tests.moc"
