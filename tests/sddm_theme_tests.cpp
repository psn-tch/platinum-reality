// The SDDM theme (packaging/sddm/platinum-reality) against stand-ins for SDDM's own objects:
// `sddm` records login and power requests, the models carry SDDM's role names. No real
// greeter, account or power action is involved; docs/sddm-theme.md.
#include <QCoreApplication>
#include <QDir>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickView>
#include <QStandardItemModel>
#include <QTest>

class FakeSddm final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canReboot MEMBER canReboot CONSTANT)
    Q_PROPERTY(bool canPowerOff MEMBER canPowerOff CONSTANT)
  public:
    bool canReboot = true, canPowerOff = true;
    QStringList requests;
    Q_INVOKABLE void login(const QString &user, const QString &password, int session)
    {
        requests << QString("login %1 %2 %3").arg(user, password).arg(session);
    }
    Q_INVOKABLE void reboot() { requests << "reboot"; }
    Q_INVOKABLE void powerOff() { requests << "powerOff"; }
  signals:
    void loginFailed();
    void loginSucceeded();
};

// SDDM's UserModel and SessionModel roles, and their lastIndex property.
class FakeModel final : public QStandardItemModel
{
    Q_OBJECT
    Q_PROPERTY(int lastIndex MEMBER lastIndex CONSTANT)
  public:
    FakeModel(QStringList roles, QList<QStringList> rows, int last) : lastIndex(last)
    {
        for (int i = 0; i < roles.size(); ++i)
            m_roles.insert(Qt::UserRole + 1 + i, roles[i].toUtf8());
        for (const auto &row : rows)
        {
            auto *item = new QStandardItem;
            for (int i = 0; i < row.size(); ++i)
                item->setData(row[i], Qt::UserRole + 1 + i);
            appendRow(item);
        }
    }
    QHash<int, QByteArray> roleNames() const override { return m_roles; }
    int lastIndex;

  private:
    QHash<int, QByteArray> m_roles;
};

class FakeCapsLock final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled MEMBER enabled NOTIFY changed)
  public:
    bool enabled = false;
  signals:
    void changed();
};

class FakeKeyboard final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *capsLock READ capsLock CONSTANT)
  public:
    FakeCapsLock lock;
    QObject *capsLock() { return &lock; }
};

class SddmThemeTests final : public QObject
{
    Q_OBJECT
    // By the visual tree: Repeater rows are not QObject children of their parent.
    static QQuickItem *find(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (auto *child : from->childItems())
            if (auto *found = find(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *item(QQuickView &view, const QString &name) { return find(view.rootObject(), name); }
    void type(QQuickView &view, const QString &text)
    {
        for (const auto character : text)
            QTest::keyClick(&view, character.toLatin1());
    }
    void click(QQuickView &view, QQuickItem *target)
    {
        QVERIFY(target && target->isVisible());
        QTest::mouseClick(&view, Qt::LeftButton, {},
                          target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
    }
    struct Greeter
    {
        FakeSddm sddm;
        FakeModel users, sessions;
        QQmlPropertyMap config;
        FakeKeyboard keyboard;
        QQuickView view;
        Greeter(QList<QStringList> people, int lastUser)
            : users({"name", "realName", "homeDir", "icon", "needsPassword"}, people, lastUser),
              sessions({"directory", "file", "type", "name"},
                       {{"/usr/share/wayland-sessions", "platinum-reality.desktop", "wayland", "Platinum Reality"},
                        {"/usr/share/wayland-sessions", "plasma.desktop", "wayland", "Plasma (Wayland)"}}, 0)
        {
            config.insert("background", "");
            config.insert("backgroundColor", "#666699");
            auto *context = view.rootContext();
            context->setContextProperty("sddm", &sddm);
            context->setContextProperty("userModel", &users);
            context->setContextProperty("sessionModel", &sessions);
            context->setContextProperty("config", &config);
            context->setContextProperty("keyboard", &keyboard);
            view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.resize(1024, 768);
            view.setSource(QUrl::fromLocalFile(QStringLiteral(SDDM_THEME_DIR) + "/Main.qml"));
        }
    };

  private slots:
    void oneAccountLogsInWithTheChosenSession()
    {
        Greeter greeter({{"pat", "Pat Example", "/home/pat", "", "true"}}, 0);
        auto &view = greeter.view;
        QCOMPARE(view.status(), QQuickView::Ready);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(item(view, "login-name")->property("text").toString(), QString("Welcome, Pat Example"));
        QVERIFY(!item(view, "login-user")->isVisible()); // One account: nothing to choose.
        QVERIFY(!item(view, "login-button")->property("usable").toBool());
        // The second session, then the password and Return.
        click(view, item(view, "login-session"));
        QTRY_VERIFY(item(view, "login-session-list")->isVisible());
        click(view, item(view, "login-session-1"));
        QTRY_VERIFY(!item(view, "login-session-list")->isVisible());
        type(view, "secret");
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(greeter.sddm.requests, QStringList({"login pat secret 1"}));
        QVERIFY(item(view, "login-password")->property("readOnly").toBool()); // Busy until SDDM answers.
        // A wrong password: said plainly, the field is cleared and takes typing again.
        emit greeter.sddm.loginFailed();
        QCOMPARE(item(view, "login-message")->property("text").toString(),
                 QString("Sorry, that password is incorrect. Please try again."));
        QCOMPARE(item(view, "login-password")->property("text").toString(), QString());
        type(view, "right");
        click(view, item(view, "login-button"));
        QCOMPARE(greeter.sddm.requests.last(), QString("login pat right 1"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/sddm-theme.png"));
    }
    void severalAccountsAskWhoIsLoggingIn()
    {
        Greeter greeter({{"pat", "Pat Example", "/home/pat", "", "true"},
                         {"sam", "", "/home/sam", "", "true"}}, 1);
        auto &view = greeter.view;
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        // The last user is chosen at first; without a full name, the account name shows.
        QCOMPARE(item(view, "login-name")->property("text").toString(), QString("Welcome, sam"));
        click(view, item(view, "login-user"));
        QTRY_VERIFY(item(view, "login-user-list")->isVisible());
        click(view, item(view, "login-user-0"));
        QCOMPARE(item(view, "login-name")->property("text").toString(), QString("Welcome, Pat Example"));
        type(view, "pw");
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(greeter.sddm.requests, QStringList({"login pat pw 0"}));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/sddm-theme-users.png"));
    }
    void restartShutDownAndCapsLock()
    {
        Greeter greeter({{"pat", "Pat Example", "/home/pat", "", "true"}}, 0);
        greeter.sddm.canPowerOff = false; // Hidden when SDDM cannot.
        auto &view = greeter.view;
        view.setSource({});
        view.setSource(QUrl::fromLocalFile(QStringLiteral(SDDM_THEME_DIR) + "/Main.qml"));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(!item(view, "login-shut-down")->isVisible());
        click(view, item(view, "login-restart"));
        QCOMPARE(greeter.sddm.requests, QStringList({"reboot"}));
        greeter.keyboard.lock.enabled = true;
        emit greeter.keyboard.lock.changed();
        QCOMPARE(item(view, "login-message")->property("text").toString(), QString("Caps Lock is on."));
    }
};

QTEST_MAIN(SddmThemeTests)
#include "sddm_theme_tests.moc"
