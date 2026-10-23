#include "desktop/menu_broker.hpp"
#include <QDBusVariant>
#include <QtTest>

namespace
{
QVariantMap node(int id, QString label, QVariantList children = {}, QVariantMap properties = {})
{
    properties.insert("label", label);
    return {{"id", id}, {"properties", properties}, {"children", children}};
}
QVariantMap decode(const QVariant &value, bool &valid)
{
    int count = 0, bytes = 0;
    valid = true;
    return MenuBroker::decodeLayout(value, 0, count, bytes, valid);
}
} // namespace
class MenuTests : public QObject
{
    Q_OBJECT
  private slots:
    void modalContextRetiresActionsWithoutDiscardingEndpointIdentity()
    {
        MenuBroker broker(false);
        broker.select(1, "org.example.Editor", "/Menu", 100);
        broker.setInteractionContext(1, false);
        const auto generation = broker.generation();
        const auto beforeModal = broker.revision();
        broker.setInteractionContext(2, true);
        QVERIFY(broker.interactionBlocked());
        QCOMPARE(broker.generation(), generation);
        QVERIFY(broker.revision() > beforeModal);
        const auto modalRevision = broker.revision();
        broker.setInteractionContext(2, true);
        QCOMPARE(broker.revision(), modalRevision); // Repeated focus sync is a no-op.
        broker.setInteractionContext(1, false);
        QVERIFY(!broker.interactionBlocked());
        QCOMPARE(broker.generation(), generation);
        QVERIFY(broker.revision() > modalRevision);
    }
    void preservesLiteralUnderscoresAndState()
    {
        bool valid;
        auto result = decode(node(0, "",
                                  {node(1, "_File",
                                        {node(2, "Save __copy", {},
                                              {{"enabled", false},
                                               {"toggle-type", "checkmark"},
                                               {"toggle-state", 1}})})}),
                             valid);
        QVERIFY(valid);
        auto file = result["children"].toList().first().toMap();
        QCOMPARE(file["label"].toString(), QString("File"));
        auto save = file["children"].toList().first().toMap();
        QCOMPARE(save["label"].toString(), QString("Save _copy"));
        QVERIFY(!save["enabled"].toBool());
        QVERIFY(save["checked"].toBool());
        QVERIFY(save["checkable"].toBool());
    }
    void duplicateIdentitiesRejected()
    {
        bool valid;
        decode(node(0, "", {node(1, "First"), node(1, "Second")}), valid);
        QVERIFY(!valid);
    }
    void excessiveDepthRejected()
    {
        QVariant tree = node(20, "Leaf");
        for (int i = 19; i >= 0; --i)
            tree = node(i, "Branch", {tree});
        bool valid;
        decode(tree, valid);
        QVERIFY(!valid);
    }
    void excessiveItemsAndLabelsRejected()
    {
        QVariantList children;
        for (int i = 1; i < 4097; ++i)
            children.push_back(node(i, "Item"));
        bool valid;
        decode(node(0, "", children), valid);
        QVERIFY(!valid);
        decode(node(0, QString(4097, 'x')), valid);
        QVERIFY(!valid);
    }
    void malformedStructureRejected()
    {
        bool valid;
        decode(QString("not a menu"), valid);
        QVERIFY(!valid);
        decode(QVariantMap{{"properties", QVariantMap{}}, {"children", QVariantList{}}}, valid);
        QVERIFY(!valid);
        decode(node(-1, "invalid"), valid);
        QVERIFY(!valid);
    }
    void ownerChangeInvalidatesQueuedActions()
    {
        MenuBroker broker(false);
        broker.select(1, "org.example.One", "/Menu", 100);
        const auto first = broker.generation();
        broker.select(2, "org.example.Two", "/Menu", 200);
        QVERIFY(broker.generation() > first);
        QVERIFY(broker.menus().isEmpty());
        // There is no authenticated endpoint in this fixture. Neither a stale
        // nor a guessed action can create one, or create an advertised menu.
        broker.invoke(1, first, 1);
        QVERIFY(broker.menus().isEmpty());
        const auto second = broker.generation();
        broker.select(2, "org.example.Two", "/Menu", 201);
        QVERIFY(broker.generation() > second);
    }
};
QTEST_GUILESS_MAIN(MenuTests)
#include "menu_tests.moc"
