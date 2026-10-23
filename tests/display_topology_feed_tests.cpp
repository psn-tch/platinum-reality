#include "desktop/display_topology_feed.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>

class DisplayTopologyFeedTests final:public QObject
{
    Q_OBJECT
    QByteArray golden;
    QJsonObject document(int generation=1) const
    {
        auto object=QJsonDocument::fromJson(golden).object();
        object.insert("generation",generation);
        return object;
    }
    bool publish(const QString &path,const QJsonObject &object)
    {
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)) return false;
        const auto bytes=QJsonDocument(object).toJson();
        return file.write(bytes)==bytes.size() && file.commit();
    }
  private slots:
    void initTestCase()
    {
        QFile file(QFINDTESTDATA("fixtures/display-topology.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        golden=file.readAll();
    }
    void parsesTheSharedProducerConsumerFixture()
    {
        QString error;
        const auto snapshot=parseDisplaySnapshot(golden,&error);
        QVERIFY2(snapshot,qPrintable(error));
        QCOMPARE(snapshot->generation,quint64(1));
        QCOMPARE(snapshot->layout.hostBounds(),QRect(0,0,2080,760));
        QCOMPARE(snapshot->layout.primary(),QString("HEADLESS-1"));
        QCOMPARE(snapshot->layout.outputs().last().transform,QString("90"));
    }
    void rejectsMalformedVersionsCoordinatesAndInventories()
    {
        QVERIFY(!parseDisplaySnapshot("[]"));
        QVERIFY(!parseDisplaySnapshot("{"));
        QVERIFY(!parseDisplaySnapshot(QByteArray(65537,'x')));
        auto object=document(); object["version"]=2;
        QVERIFY(!parseDisplaySnapshot(QJsonDocument(object).toJson()));
        object=document(); object["generation"]=true;
        QVERIFY(!parseDisplaySnapshot(QJsonDocument(object).toJson()));
        object=document(); object["generation"]=1.5;
        QVERIFY(!parseDisplaySnapshot(QJsonDocument(object).toJson()));
        object=document(); object["mode"]="span-cropped";
        QVERIFY(!parseDisplaySnapshot(QJsonDocument(object).toJson()));
        object=document(); auto outputs=object["outputs"].toArray();auto output=outputs[0].toObject();
        output["width"]="1120";outputs[0]=output;object["outputs"]=outputs;
        QVERIFY(!parseDisplaySnapshot(QJsonDocument(object).toJson()));
        object=document(); outputs=object["outputs"].toArray();output=outputs[1].toObject();
        output["x"]=500;outputs[1]=output;object["outputs"]=outputs;
        QVERIFY(!parseDisplaySnapshot(QJsonDocument(object).toJson()));
    }
    void invalidOrStaleUpdatesRetainTheLastGoodLayout()
    {
        QTemporaryDir directory;
        const auto path=directory.filePath("display-topology.json");
        QVERIFY(publish(path,document()));
        DisplayTopologyFeed feed(path);
        QSignalSpy changes(&feed,&DisplayTopologyFeed::changed);
        QVERIFY(feed.refresh()); QCOMPARE(changes.count(),1);
        auto bad=document(2); bad["outputs"]=QJsonArray{};
        QVERIFY(publish(path,bad)); QVERIFY(!feed.refresh());
        QCOMPARE(feed.snapshot()->generation,quint64(1));
        auto next=document(2);next["primary"]="HEADLESS-2";
        QVERIFY(publish(path,next));QVERIFY(feed.refresh());
        QCOMPARE(feed.snapshot()->layout.primary(),QString("HEADLESS-2"));
        QCOMPARE(changes.count(),2);
        QVERIFY(publish(path,document()));QVERIFY(!feed.refresh());
        QCOMPARE(feed.snapshot()->generation,quint64(2));
        QVERIFY(publish(path,document(2)));QVERIFY(!feed.refresh()); // Same serial, different data.
        next=document(3);next["session"]="ffffffffffffffffffffffffffffffff";
        QVERIFY(publish(path,next));QVERIFY(!feed.refresh());
        QCOMPARE(changes.count(),2);
    }
    void watchesRepeatedAtomicReplacementAndAcknowledgesOnlyAppliedGeneration()
    {
        QTemporaryDir directory;
        const auto path=directory.filePath("display-topology.json");
        QVERIFY(publish(path,document()));
        DisplayTopologyFeed feed(path);
        QSignalSpy changes(&feed,&DisplayTopologyFeed::changed);
        QVERIFY(feed.refresh());
        QVERIFY(publish(path,document(2)));
        QTRY_COMPARE(feed.snapshot()->generation,quint64(2));
        QVERIFY(publish(path,document(3)));
        QTRY_COMPARE(feed.snapshot()->generation,quint64(3));
        QCOMPARE(changes.count(),3);
        QVERIFY(!feed.acknowledge(2));
        QVERIFY(!QFileInfo::exists(path+".applied"));
        QVERIFY(feed.acknowledge(3));
        QVERIFY(feed.error().isEmpty());
        QFile acknowledgement(path+".applied");
        QVERIFY(acknowledgement.open(QIODevice::ReadOnly));
        const auto reply=QJsonDocument::fromJson(acknowledgement.readAll()).object();
        QCOMPARE(reply["session"].toString(),feed.snapshot()->session);
        QCOMPARE(reply["generation"].toInt(),3);
        const auto permissions=QFileInfo(acknowledgement).permissions();
        QVERIFY(!(permissions&(QFileDevice::ReadGroup|QFileDevice::WriteGroup|QFileDevice::ReadOther|QFileDevice::WriteOther)));
        QTest::qWait(80); // Acknowledgement's directory event must not publish again.
        QCOMPARE(changes.count(),3);
        // Even a duplicate atomic replacement must re-arm the file watch.
        // A later in-place correction should not need a directory-entry event.
        QVERIFY(publish(path,document(3)));
        QTest::qWait(80);
        QFile inPlace(path);QVERIFY(inPlace.open(QIODevice::WriteOnly|QIODevice::Truncate));
        const auto next=QJsonDocument(document(4)).toJson();
        QCOMPARE(inPlace.write(next),qint64(next.size()));inPlace.close();
        QTRY_COMPARE(feed.snapshot()->generation,quint64(4));
    }
    void refusesPublicOrSymbolicLinkInputs()
    {
        QTemporaryDir directory;
        const auto path=directory.filePath("display-topology.json");
        QVERIFY(publish(path,document()));
        QVERIFY(QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ReadOther));
        DisplayTopologyFeed feed(path);QVERIFY(!feed.refresh());
        QVERIFY(publish(path,document()));QVERIFY(feed.refresh());
        const auto link=directory.filePath("link.json");
        QVERIFY(QFile::link(path,link));
        DisplayTopologyFeed linked(link);QVERIFY(!linked.refresh());
        QVERIFY(QFile::setPermissions(directory.path(),QFileDevice::ReadOwner|QFileDevice::WriteOwner|
                                     QFileDevice::ExeOwner|QFileDevice::ReadOther|QFileDevice::ExeOther));
        QVERIFY(!feed.refresh());
        QVERIFY(QFile::setPermissions(directory.path(),QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
    }
};
QTEST_GUILESS_MAIN(DisplayTopologyFeedTests)
#include "display_topology_feed_tests.moc"
