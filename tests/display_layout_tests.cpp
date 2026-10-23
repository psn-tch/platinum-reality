#include "desktop/display_layout.hpp"
#include <QtTest>
#include <limits>

class DisplayLayoutTests final:public QObject
{
    Q_OBJECT
    static QVector<DisplayOutput> pair()
    {
        return {{"DP-3",{0,0,1120,760},1.5,"normal"},{"eDP-1",{1120,0,960,600},2,"90"}};
    }
  private slots:
    void normalizesNegativeOriginWithoutScalingLogicalGeometryAgain()
    {
        const auto layout=DisplayLayout::create({{"eDP-1",{-1114,0,1114,696},2.3,"90"},
                                                {"DP-3",{0,0,1423,800},1.35,"normal"}},"DP-3",DisplayLayout::Mode::Extend);
        QVERIFY(layout);
        QCOMPARE(layout->hostBounds(),QRect(0,0,2537,800));
        QCOMPARE(layout->primaryRect(),QRect(1114,0,1423,800));
        QCOMPARE(layout->outputs().first().scale,1.35);
    }
    void rejectsUnsafeOverlappingAndUnboundedLayouts()
    {
        QString error;
        auto invalid=pair(); invalid[1].name="DP-3; exit";
        QVERIFY(!DisplayLayout::create(invalid,"",DisplayLayout::Mode::Extend,&error));
        QVERIFY(!error.isEmpty());
        invalid=pair(); invalid[1].name=invalid[0].name;
        QVERIFY(!DisplayLayout::create(invalid,"",DisplayLayout::Mode::Extend));
        invalid=pair(); invalid[1].logical.moveLeft(1000);
        QVERIFY(!DisplayLayout::create(invalid,"",DisplayLayout::Mode::Mirror));
        invalid=pair(); invalid[1].scale=std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!DisplayLayout::create(invalid,"",DisplayLayout::Mode::Extend));
        invalid=pair(); invalid[1].transform="45";
        QVERIFY(!DisplayLayout::create(invalid,"",DisplayLayout::Mode::Extend));
        invalid=pair(); invalid[1].logical=QRect(9000,9000,8000,8000);
        QVERIFY(!DisplayLayout::create(invalid,"",DisplayLayout::Mode::Extend));
        QVERIFY(!DisplayLayout::create({},"",DisplayLayout::Mode::Extend));
    }
    void missingPrimaryFallsBackToExternalThenInternal()
    {
        auto outputs=pair();
        auto layout=DisplayLayout::create(outputs,"DP-2",DisplayLayout::Mode::Extend);
        QVERIFY(layout); QCOMPARE(layout->primary(),QString("DP-3"));
        outputs.removeFirst();
        layout=DisplayLayout::create(outputs,"DP-2",DisplayLayout::Mode::Extend);
        QVERIFY(layout); QCOMPARE(layout->primary(),QString("eDP-1"));
        QCOMPARE(layout->primaryRect(),QRect(0,0,960,600));
    }
    void fullScreenBelongsToTheMonitorWithMostWindowArea()
    {
        const auto layout=*DisplayLayout::create(pair(),"DP-3",DisplayLayout::Mode::Extend);
        QCOMPARE(layout.screenForWindow({1250,80,360,240}),QRect(1120,0,960,600));
        QCOMPARE(layout.screenForWindow({950,80,360,240}),QRect(1120,0,960,600));
        QCOMPARE(layout.screenForWindow({100,80,360,240}),QRect(0,0,1120,760));
    }
    void windowGripCannotGetLostInLayoutGaps()
    {
        const auto layout=*DisplayLayout::create(pair(),"DP-3",DisplayLayout::Mode::Extend);
        QCOMPARE(layout.reachablePosition({1250,80},{360,240}),QPoint(1250,80));
        QCOMPARE(layout.reachablePosition({950,80},{360,240}),QPoint(950,80));
        QCOMPARE(layout.reachablePosition({1090,80},{360,240}),QPoint(1090,80));
        QCOMPARE(layout.reachablePosition({1100,700},{360,240}),QPoint(1040,700));
        QCOMPARE(layout.reachablePosition({150,0},{360,240}),QPoint(150,26));
        QCOMPARE(layout.reachablePosition({1250,0},{360,240}),QPoint(1250,0));
        const auto unplugged=DisplayLayout::single({1120,760});
        QCOMPARE(unplugged.reachablePosition({1250,80},{360,240},true),QPoint(754,80));
    }
    void mirrorFitsUnequalAspectRatiosAndRejectsLetterboxes()
    {
        const auto layout=*DisplayLayout::create(pair(),"DP-3",DisplayLayout::Mode::Mirror);
        QCOMPARE(layout.desktopBounds(),QRect(0,0,1120,760));
        const auto fit=layout.mirrorPicture("eDP-1");
        QCOMPARE(fit.height(),600.0);
        QVERIFY(qAbs(fit.width()-884.210526)<0.001);
        QCOMPARE(fit.center(),QPointF(1600,300));
        QVERIFY(!layout.hostToDesktop({1130,200}));
        QVERIFY(!layout.hostToDesktop({2070,200}));
        const auto center=layout.hostToDesktop(fit.center());
        QVERIFY(center);
        QVERIFY(QLineF(*center,{560,380}).length()<0.001);
        QCOMPARE(*layout.hostToDesktop({100,80}),QPointF(100,80));
        QVERIFY(!layout.hostToDesktop({1500,700}));
    }
    void extendMappingRejectsGapsAndUsesHalfOpenEdges()
    {
        const auto layout=*DisplayLayout::create(pair(),"DP-3",DisplayLayout::Mode::Extend);
        QCOMPARE(*layout.hostToDesktop({1120,50}),QPointF(1120,50));
        QVERIFY(!layout.hostToDesktop({2080,50}));
        QVERIFY(!layout.hostToDesktop({1500,600}));
        QVERIFY(!layout.hostToDesktop({-1,50}));
    }
    void mirrorPrimaryCanBeTheOtherScreen()
    {
        const auto layout=*DisplayLayout::create(pair(),"eDP-1",DisplayLayout::Mode::Mirror);
        QCOMPARE(layout.primaryRect(),QRect(0,0,960,600));
        QCOMPARE(layout.desktopBounds(),QRect(0,0,960,600));
        QCOMPARE(layout.mirrorPicture("DP-3").size(),QSizeF(1120,700));
        QCOMPARE(*layout.hostToDesktop({1600,300}),QPointF(480,300));
        QVERIFY(!layout.hostToDesktop({200,10}));
    }
};
QTEST_GUILESS_MAIN(DisplayLayoutTests)
#include "display_layout_tests.moc"
