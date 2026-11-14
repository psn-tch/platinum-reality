#include "appearance/interface_font.hpp"
#include "authentication/authentication_controller.hpp"
#include "authentication/authentication_dialog.hpp"
#include "authentication/authentication_session.hpp"
#include "finder/platinum_style.hpp"
#include <QComboBox>
#include <QLabel>
#include <QDir>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

// The password box's rules (docs/password-box.md) with a scripted conversation in place of polkit's
// helper, so no polkitd, PAM or real password is involved.
namespace
{
class ScriptedSession final : public AuthenticationSession
{
  public:
    ScriptedSession(QString identity, QObject *parent) : AuthenticationSession(parent), identity(std::move(identity)) {}
    void initiate() override { ++initiated; }
    void respond(const QString &response) override { responses << response; }
    void cancel() override { ++canceled; }
    void ask(bool echo = false, const QString &prompt = "Password: ") { emit request(prompt, echo); }
    void say(const QString &text, bool error) { emit message(text, error); }
    void finish(bool gained) { emit completed(gained); }
    QString identity;
    QStringList responses;
    int initiated = 0, canceled = 0;
};

struct Harness
{
    explicit Harness(bool answerFirst = true) : answerFirst(answerFirst) {}
    bool answerFirst;
    QList<QPointer<ScriptedSession>> sessions;
    QStringList identities;
    int finished = 0;
    AuthenticationController controller{[this](const QString &identity, const QString &cookie, QObject *parent) {
                                            if (cookie != "cookie-1")
                                                qFatal("polkit's cookie was not passed on");
                                            auto *session = new ScriptedSession(identity, parent);
                                            sessions << session;
                                            identities << identity;
                                            return session;
                                        },
                                        answerFirst, "tester"};
    bool begin(QStringList who = {"unix-user:root", "unix-user:tester"})
    {
        return controller.begin({"org.platinumreality.update.install", "Installing an update requires a password.",
                                 "system-software-update", "cookie-1", std::move(who)},
                                [this] { ++finished; });
    }
    ScriptedSession *session() const { return sessions.isEmpty() ? nullptr : sessions.last().data(); }
    template <typename T> T *part(const char *name) const
    {
        return controller.dialog() ? controller.dialog()->findChild<T *>(name) : nullptr;
    }
    QLineEdit *password() const { return part<QLineEdit>("authenticationPassword"); }
    QPushButton *ok() const { return part<QPushButton>("authenticationOK"); }
    QLabel *status() const { return part<QLabel>("authenticationStatus"); }
};
} // namespace

class AuthenticationTests final : public QObject
{
    Q_OBJECT
    QTemporaryDir dataHome;
  private slots:
    void initTestCase()
    {
        // The person's own imported 9.2.2 icons never reach these tests.
        QVERIFY(dataHome.isValid());
        qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());
        qApp->setStyle(new PlatinumStyle);
        qApp->setFont(classicInterfaceFont());
    }
    void thePersonLoggedInIsAskedAndOneRightAnswerAuthorizes()
    {
        Harness harness;
        QVERIFY(harness.begin());
        QVERIFY(harness.controller.dialog());
        auto *message = harness.part<QLabel>("authenticationMessage");
        QVERIFY(message);
        QCOMPARE(message->text(), QString("Installing an update requires a password."));
        auto *names = harness.part<QComboBox>("authenticationName");
        QVERIFY(names);
        QCOMPARE(names->currentData().toString(), QString("unix-user:tester"));
        // The box asks at once, but PAM is not involved until OK.
        QVERIFY(harness.sessions.isEmpty());
        QVERIFY(harness.password()->isEnabled());
        QCOMPARE(harness.password()->echoMode(), QLineEdit::Password);
        QTest::keyClicks(harness.password(), "correct horse");
        QTest::mouseClick(harness.ok(), Qt::LeftButton);
        QCOMPARE(harness.identities, QStringList{"unix-user:tester"});
        QCOMPARE(harness.session()->initiated, 1);
        // The field does not keep the password, and OK cannot send it twice.
        QVERIFY(harness.password()->text().isEmpty());
        QVERIFY(!harness.ok()->isEnabled());
        QTest::mouseClick(harness.ok(), Qt::LeftButton);
        QCOMPARE(harness.sessions.size(), 1);
        // PAM's question takes the answer already typed.
        harness.session()->ask();
        QCOMPARE(harness.session()->responses, QStringList{"correct horse"});
        QVERIFY(!harness.password()->isEnabled());
        QCOMPARE(harness.finished, 0);
        harness.session()->finish(true);
        QCOMPARE(harness.finished, 1);
        QVERIFY(!harness.controller.dialog());
        QCOMPARE(harness.sessions.size(), 1);
    }
    void returnSendsTheAnswerToo()
    {
        Harness harness;
        QVERIFY(harness.begin({"unix-user:tester"}));
        // One person: Name is shown, not chosen.
        QVERIFY(harness.part<QLabel>("authenticationName"));
        QVERIFY(!harness.part<QComboBox>("authenticationName"));
        QTest::keyClicks(harness.password(), "secret");
        QTest::keyClick(harness.password(), Qt::Key_Return);
        harness.session()->ask();
        QCOMPARE(harness.session()->responses, QStringList{"secret"});
    }
    void aWrongAnswerAsksAgainAtMostThreeTimes()
    {
        Harness harness;
        QVERIFY(harness.begin());
        for (int attempt = 1; attempt <= AuthenticationController::attempts; ++attempt)
        {
            QCOMPARE(harness.sessions.size(), attempt - 1);
            QTest::keyClicks(harness.password(), "wrong");
            QTest::keyClick(harness.password(), Qt::Key_Return);
            auto *session = harness.session();
            session->ask();
            QCOMPARE(session->responses, QStringList{"wrong"});
            session->finish(false);
            // A finished conversation is never canceled; polkit needs a new one per attempt.
            QCOMPARE(session->canceled, 0);
            if (attempt < AuthenticationController::attempts)
            {
                QVERIFY(harness.controller.dialog());
                // 9.2.2 says so in a stop alert over the box, not under the fields.
                auto *alert = harness.part<QMessageBox>("authenticationFailed");
                QVERIFY(alert);
                QVERIFY(alert->text().contains("failed authentication"));
                alert->done(QMessageBox::Ok);
                QVERIFY(harness.password()->text().isEmpty());
                QVERIFY(harness.password()->isEnabled());
                QCOMPARE(harness.finished, 0);
            }
        }
        // pam_faillock locks an account after three failures: the box does not offer a fourth.
        QCOMPARE(harness.finished, 1);
        QVERIFY(!harness.controller.dialog());
        QCOMPARE(harness.sessions.size(), AuthenticationController::attempts);
    }
    void aWrongPasswordShowsSelectedBulletsThatAreNeverSent()
    {
        // 9.2.2 (Keychain Access, 2026-11-14): after the stop alert the box shows the old password
        // selected, so typing replaces it. Ours only draws them; Return on them sends nothing.
        Harness harness;
        QVERIFY(harness.begin({"unix-user:tester"}));
        auto *dialog = harness.controller.dialog();
        dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(dialog));
        QTest::keyClicks(harness.password(), "wrong");
        const auto artifacts = QCoreApplication::applicationDirPath() + "/artifacts/";
        QDir().mkpath(artifacts);
        QVERIFY(dialog->grab().save(artifacts + "password-box.png"));
        QTest::keyClick(harness.password(), Qt::Key_Return);
        harness.session()->ask();
        harness.session()->finish(false);
        auto *alert = harness.part<QMessageBox>("authenticationFailed");
        QVERIFY(alert);
        QTRY_VERIFY(alert->isVisible());
        QCOMPARE(alert->icon(), QMessageBox::Critical);
        const auto name = harness.part<QLabel>("authenticationName")->text();
        QCOMPARE(alert->text(),
                 QString("The password for \u201c%1\u201d failed authentication. Please try again.").arg(name));
        QVERIFY(alert->grab().save(artifacts + "password-box-failed.png"));
        QVERIFY(!harness.status()->isVisibleTo(dialog));
        alert->done(QMessageBox::Ok);
        QVERIFY(harness.password()->text().isEmpty());
        QCOMPARE(harness.password()->property("shownBullets").toInt(), 5);
        QVERIFY(dialog->grab().save(artifacts + "password-box-retry.png"));
        // Neither Return nor OK sends the drawn bullets, and no conversation starts.
        const auto conversations = harness.sessions.size();
        QTest::keyClick(harness.password(), Qt::Key_Return);
        QTest::keyClick(harness.password(), Qt::Key_Enter);
        QTest::mouseClick(harness.ok(), Qt::LeftButton);
        QCOMPARE(harness.sessions.size(), conversations);
        // Typing replaces them, as it replaces a selection.
        QTest::keyClicks(harness.password(), "right");
        QCOMPARE(harness.password()->text(), QString("right"));
        QCOMPARE(harness.password()->property("shownBullets").toInt(), 0);
        QTest::keyClick(harness.password(), Qt::Key_Return);
        QCOMPARE(harness.sessions.size(), conversations + 1);
        harness.session()->ask();
        QCOMPARE(harness.session()->responses, QStringList{"right"});
    }
    void backspaceClearsTheSelectedBullets()
    {
        Harness harness;
        QVERIFY(harness.begin({"unix-user:tester"}));
        QTest::keyClicks(harness.password(), "abc");
        QTest::keyClick(harness.password(), Qt::Key_Return);
        harness.session()->ask();
        harness.session()->finish(false);
        QCOMPARE(harness.password()->property("shownBullets").toInt(), 3);
        QTest::keyClick(harness.password(), Qt::Key_Backspace);
        QCOMPARE(harness.password()->property("shownBullets").toInt(), 0);
        QVERIFY(harness.password()->text().isEmpty());
        QVERIFY(!harness.password()->isReadOnly());
    }
    void theBoxAlwaysHasAnIconAndUsesTheImportedKeychain()
    {
        // No icon theme in tests: the drawn padlock keeps the icon's place.
        {
            Harness harness;
            QVERIFY(harness.begin({"unix-user:tester"}));
            auto *icon = harness.part<QLabel>("authenticationIcon");
            QVERIFY(icon);
            QVERIFY(!icon->pixmap().isNull());
            QVERIFY(icon->pixmap().toImage().pixelColor(16, 22).alpha() > 0);
        }
        // The keychain from the person's own Keychain Access, once imported (9.2.2's Unlock box).
        const auto icons = dataHome.path() + "/platinum-reality/assets/icons/";
        QVERIFY(QDir().mkpath(icons));
        QImage keychain(32, 32, QImage::Format_ARGB32);
        keychain.fill(Qt::green);
        QVERIFY(keychain.save(icons + "keychain.png"));
        const auto clean = qScopeGuard([&] { QFile::remove(icons + "keychain.png"); });
        Harness harness;
        QVERIFY(harness.begin({"unix-user:tester"}));
        QCOMPARE(harness.part<QLabel>("authenticationIcon")->pixmap().toImage().pixelColor(16, 16), QColor(Qt::green));
    }
    void alertsUseTheImportedStopCautionAndNoteIcons()
    {
        const auto red = [](QStyle::StandardPixmap which) {
            return qApp->style()->standardIcon(which).pixmap(32, 32).toImage().pixelColor(16, 16) == QColor(Qt::red);
        };
        QVERIFY(!qApp->style()->standardIcon(QStyle::SP_MessageBoxCritical).isNull()); // original artwork
        QVERIFY(!red(QStyle::SP_MessageBoxCritical));
        const auto icons = dataHome.path() + "/platinum-reality/assets/icons/";
        QVERIFY(QDir().mkpath(icons));
        QImage stop(32, 32, QImage::Format_ARGB32);
        stop.fill(Qt::red);
        for (const auto *name : {"alert-stop", "alert-caution", "alert-note"})
            QVERIFY(stop.save(icons + name + ".png"));
        const auto clean = qScopeGuard([&] {
            for (const auto *name : {"alert-stop", "alert-caution", "alert-note"})
                QFile::remove(icons + name + ".png");
        });
        QVERIFY(red(QStyle::SP_MessageBoxCritical));
        QVERIFY(red(QStyle::SP_MessageBoxWarning));
        QVERIFY(red(QStyle::SP_MessageBoxInformation));
        QVERIFY(!red(QStyle::SP_MessageBoxQuestion)); // 9.2.2 had none: ours stays
    }
    void cancelBeforeOKNeverStartsPAM()
    {
        // An unanswered conversation counts as a failed password in pam_faillock (2026-11-14).
        Harness harness;
        QVERIFY(harness.begin());
        QTest::keyClicks(harness.password(), "half typed");
        auto *cancel = harness.part<QPushButton>("authenticationCancel");
        QVERIFY(cancel);
        QTest::mouseClick(cancel, Qt::LeftButton);
        QCOMPARE(harness.finished, 1);
        QVERIFY(!harness.controller.dialog());
        QVERIFY(harness.sessions.isEmpty());
    }
    void aWrongAnswerThenCancelIsOneFailureNotTwo()
    {
        Harness harness;
        QVERIFY(harness.begin());
        QTest::keyClicks(harness.password(), "wrong");
        QTest::keyClick(harness.password(), Qt::Key_Return);
        harness.session()->ask();
        harness.session()->finish(false);
        QTest::keyClick(harness.password(), Qt::Key_Escape);
        QCOMPARE(harness.finished, 1);
        QCOMPARE(harness.sessions.size(), 1);
    }
    void cancelWhilePAMDecidesEndsTheConversation()
    {
        Harness harness;
        QVERIFY(harness.begin());
        QTest::keyClicks(harness.password(), "typed");
        QTest::keyClick(harness.password(), Qt::Key_Return);
        auto *session = harness.session();
        QVERIFY(harness.controller.dialog());
        harness.controller.dialog()->reject();
        QCOMPARE(harness.finished, 1);
        QCOMPARE(session->canceled, 1);
        // The helper's late "not authorized" after Cancel changes nothing.
        if (session)
            session->finish(false);
        QCOMPARE(harness.finished, 1);
        QCOMPARE(harness.sessions.size(), 1);
    }
    void escapeCancels()
    {
        Harness harness;
        QVERIFY(harness.begin());
        QTest::keyClick(harness.password(), Qt::Key_Escape);
        QCOMPARE(harness.finished, 1);
        QVERIFY(harness.sessions.isEmpty());
    }
    void polkitWithdrawingTheRequestClosesTheBox()
    {
        Harness harness;
        QVERIFY(harness.begin());
        harness.controller.cancel();
        QCOMPARE(harness.finished, 1);
        QVERIFY(!harness.controller.dialog());
        QVERIFY(harness.sessions.isEmpty());
        // A later request gets a new box.
        QVERIFY(harness.begin());
        QVERIFY(harness.controller.dialog());
    }
    void oneBoxAtATime()
    {
        Harness harness;
        QVERIFY(harness.begin());
        QVERIFY(!harness.begin());
        QCOMPARE(harness.finished, 0);
    }
    void choosingAnotherNameAsksThatPerson()
    {
        Harness harness;
        QVERIFY(harness.begin());
        QTest::keyClicks(harness.password(), "typed for tester");
        auto *names = harness.part<QComboBox>("authenticationName");
        names->setCurrentIndex(names->findData("unix-user:root"));
        emit names->activated(names->currentIndex());
        QVERIFY(harness.sessions.isEmpty());
        QVERIFY(harness.password()->text().isEmpty());
        QVERIFY(harness.password()->isEnabled());
        QTest::keyClicks(harness.password(), "root's");
        QTest::keyClick(harness.password(), Qt::Key_Return);
        QCOMPARE(harness.identities, QStringList{"unix-user:root"});
        harness.session()->ask();
        QCOMPARE(harness.session()->responses, QStringList{"root's"});
    }
    void anotherAdministratorIsAskedWhenThePersonCannotAuthorize()
    {
        Harness harness;
        QVERIFY(harness.begin({"unix-user:admin", "unix-user:root"}));
        QTest::keyClicks(harness.password(), "x");
        QTest::keyClick(harness.password(), Qt::Key_Return);
        QCOMPARE(harness.identities, QStringList{"unix-user:admin"});
    }
    void aFurtherQuestionIsAskedWithPAMsOwnWords()
    {
        Harness harness;
        QVERIFY(harness.begin());
        QTest::keyClicks(harness.password(), "secret");
        QTest::keyClick(harness.password(), Qt::Key_Return);
        auto *session = harness.session();
        session->ask();
        QCOMPARE(session->responses, QStringList{"secret"});
        // A second question (not a secret) is shown as typed, with PAM's label.
        session->ask(true, "Verification code: ");
        QVERIFY(harness.password()->isEnabled());
        QCOMPARE(harness.password()->echoMode(), QLineEdit::Normal);
        QCOMPARE(harness.part<QLabel>("authenticationPrompt")->text(), QString("Verification code:"));
        QTest::keyClicks(harness.password(), "123456");
        QTest::keyClick(harness.password(), Qt::Key_Return);
        QCOMPARE(session->responses, (QStringList{"secret", "123456"}));
    }
    void withFingerprintsPAMStartsAtOnce()
    {
        // pam_fprintd waits for a finger before any password question: the conversation must be running.
        Harness harness(false);
        QVERIFY(harness.begin());
        QCOMPARE(harness.identities, QStringList{"unix-user:tester"});
        QCOMPARE(harness.session()->initiated, 1);
        QVERIFY(!harness.password()->isEnabled());
        harness.session()->say("Place your finger on the fingerprint reader", false);
        QVERIFY(harness.status()->isVisibleTo(harness.controller.dialog()));
        QCOMPARE(harness.status()->text(), QString("Place your finger on the fingerprint reader"));
        QCOMPARE(harness.status()->property("error").toBool(), false);
        // The fingerprint alone may authorize, without anything typed.
        harness.session()->finish(true);
        QCOMPARE(harness.finished, 1);
    }
    void withFingerprintsAPasswordStillWorks()
    {
        Harness harness(false);
        QVERIFY(harness.begin());
        harness.session()->ask();
        QTest::keyClicks(harness.password(), "secret");
        QTest::keyClick(harness.password(), Qt::Key_Return);
        QCOMPARE(harness.session()->responses, QStringList{"secret"});
        harness.session()->finish(false);
        // The next attempt starts its own conversation at once, too.
        QCOMPARE(harness.sessions.size(), 2);
        QCOMPARE(harness.sessions.last()->initiated, 1);
    }
    void namesComeFromTheAccount()
    {
        QCOMPARE(AuthenticationController::displayName("unix-user:root"), QString("root"));
        QCOMPARE(AuthenticationController::displayName("unix-user:no-such-person-here"),
                 QString("no-such-person-here"));
        QCOMPARE(AuthenticationController::displayName("unix-group:wheel"), QString("Group wheel"));
    }
};

QTEST_MAIN(AuthenticationTests)
#include "authentication_tests.moc"
