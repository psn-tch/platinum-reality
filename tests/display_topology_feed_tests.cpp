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
    void managedSessionsNameMonitorsAndListDisplaysThatAreOff()
    {
        // The Monitors panel's details are optional: an unmanaged snapshot has none.
        auto plain=parseDisplaySnapshot(golden);
        QVERIFY(plain && !plain->details.managed && plain->details.available.isEmpty() && !plain->details.pending);
        auto object=document();
        object["labels"]=QJsonObject{{"HEADLESS-1","PM1561P"},{"HEADLESS-2","Built-in panel"}};
        object["available"]=QJsonArray{QJsonObject{{"name","HDMI-A-1"},{"label","Samsung"},{"lid_closed",false}},
                                       QJsonObject{{"name","eDP-1"},{"label","eDP-1"},{"lid_closed",true}}};
        object["pending"]=true;
        QString error;
        auto managed=parseDisplaySnapshot(QJsonDocument(object).toJson(),&error);
        QVERIFY2(managed,qPrintable(error));
        QVERIFY(managed->details.managed && managed->details.pending);
        QCOMPARE(managed->details.labels.value("HEADLESS-1").toString(),QString("PM1561P"));
        QCOMPARE(managed->details.available.size(),2);
        QCOMPARE(managed->details.available[1].toMap().value("lidClosed").toBool(),true);
        // Bad names, an "off" display that is also on, a non-boolean flag: the whole snapshot is refused.
        const auto refused=[&](const char *key,const QJsonValue &value){
            auto bad=object; bad[key]=value;
            return !parseDisplaySnapshot(QJsonDocument(bad).toJson());
        };
        QVERIFY(refused("labels",QJsonObject{{"DP-1; exit","x"}}));
        QVERIFY(refused("labels",QJsonObject{{"DP-1",QString(65,'x')}}));
        QVERIFY(refused("labels",QJsonArray{}));
        QVERIFY(refused("available",QJsonArray{QJsonObject{{"name","HEADLESS-1"},{"label","on"},{"lid_closed",false}}}));
        QVERIFY(refused("available",QJsonArray{QJsonObject{{"name","DP-9"},{"label","x"}}}));
        QVERIFY(refused("available",QJsonObject{}));
        QVERIFY(refused("pending",1));
    }
    void mirroringShowsTheMainDisplayInsideABiggerCanvas()
    {
        auto object=document();
        object["outputs"]=QJsonArray{QJsonObject{{"name","DP-2"},{"x",0},{"y",40},{"width",1280},{"height",720},
                                                 {"scale",1.5},{"transform","normal"}}};
        object["primary"]="DP-2";
        object["canvas"]=QJsonObject{{"width",1280},{"height",800}};
        object["labels"]=QJsonObject{{"DP-2","PM1561P"}};
        object["mirrors"]=QJsonArray{QJsonObject{{"name","eDP-1"},{"label","eDP-1"}}};
        QString error;
        const auto snapshot=parseDisplaySnapshot(QJsonDocument(object).toJson(),&error);
        QVERIFY2(snapshot,qPrintable(error));
        QCOMPARE(snapshot->layout.hostBounds(),QRect(0,0,1280,800));
        QCOMPARE(snapshot->layout.primaryRect(),QRect(0,40,1280,720)); // Not moved to the canvas origin.
        QCOMPARE(snapshot->details.mirrors.size(),1);
        // The display must lie inside the canvas; mirrors need a canvas and cannot also be screens.
        auto bad=object; bad["canvas"]=QJsonObject{{"width",1280},{"height",700}};
        QVERIFY(!parseDisplaySnapshot(QJsonDocument(bad).toJson()));
        bad=object; bad.remove("canvas");
        QVERIFY(!parseDisplaySnapshot(QJsonDocument(bad).toJson()));
        bad=object; bad["mirrors"]=QJsonArray{QJsonObject{{"name","DP-2"},{"label","x"}}};
        QVERIFY(!parseDisplaySnapshot(QJsonDocument(bad).toJson()));
        bad=object; bad["canvas"]=QJsonObject{{"width",99999},{"height",800}};
        QVERIFY(!parseDisplaySnapshot(QJsonDocument(bad).toJson()));
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
