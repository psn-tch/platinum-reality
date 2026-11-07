#include "appearance/interface_font.hpp"
#include "finder/platinum_style.hpp"
#include "installer/engine_client.hpp"
#include "installer/installer_window.hpp"
#include <QCheckBox>
#include <QTextBrowser>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QDialog>
#include <QStandardItemModel>
#include <QPushButton>
#include <QMessageBox>
#include <QTimer>
#include <QTemporaryDir>
#include <QtTest>
#include "qt_theme_evidence.hpp"

class InstallerTests final : public QObject
{
    Q_OBJECT
    QList<QJsonObject> calls(const QString &log)
    {
        QList<QJsonObject> result;
        QFile file(log);
        if (file.open(QIODevice::ReadOnly))
            for (const auto &line : file.readAll().split('\n'))
                if (!line.isEmpty())
                    result.append(QJsonDocument::fromJson(line).object());
        return result;
    }
    template <typename T> T *find(InstallerWindow &window, const char *name)
    {
        auto *widget = window.findChild<T *>(name);
        Q_ASSERT(widget);
        return widget;
    }
    // Welcome → Destination (the second disk) → Information → License → Account, filled in.
    void reachInstall(InstallerWindow &window)
    {
        auto *next = find<QPushButton>(window, "installer-continue");
        QCOMPARE(window.page(), InstallerWindow::Welcome);
        QVERIFY(!find<QPushButton>(window, "installer-back")->isEnabled());
        next->click();
        QCOMPARE(window.page(), InstallerWindow::Destination);
        auto *disks = find<QComboBox>(window, "installer-disks");
        QTRY_COMPARE(disks->count(), 2);
        // The installation disk itself is listed dimmed and can't be chosen; as in 9.2.2, the first disk
        // that can be is chosen to begin with, and the button is Select.
        auto *model = qobject_cast<QStandardItemModel *>(disks->model());
        QVERIFY(model && !model->item(0)->isEnabled());
        QVERIFY(disks->itemText(0).contains("the installation disk"));
        QCOMPARE(disks->currentIndex(), 1);
        QVERIFY(disks->currentText().contains("Hard Disk (/dev/vda) — 64 GB"));
        QCOMPARE(next->text(), QString("Select"));
        QVERIFY(next->isEnabled());
        QVERIFY(find<QLabel>(window, "installer-destination-note")->text().contains("erased when you click Start"));
        next->click();
        QCOMPARE(window.page(), InstallerWindow::Information);
        next->click();
        QCOMPARE(window.page(), InstallerWindow::License);
        window.setConfirmationAnswers(false, false);
        next->click();
        QCOMPARE(window.page(), InstallerWindow::License); // Disagree stays.
        window.setConfirmationAnswers(true, false);
        next->click();
        QCOMPARE(window.page(), InstallerWindow::Account);
        QVERIFY(!next->isEnabled());
        auto *name = find<QLineEdit>(window, "installer-full-name");
        name->setFocus();
        QTest::keyClicks(name, "Pat Example");
        QCOMPARE(find<QLineEdit>(window, "installer-user-name")->text(), QString("patexample"));
        // The engine's own rules (Rocky's review, 2026-10-30): nothing it would refuse reaches Install.
        const auto problemWith = [&](const char *field, const QString &text) {
            auto *edit = find<QLineEdit>(window, field);
            const auto before = edit->text();
            edit->setText(text);
            const auto problem = window.accountProblem();
            edit->setText(before);
            return problem;
        };
        find<QLineEdit>(window, "installer-password")->setText("platinum");
        find<QLineEdit>(window, "installer-verify")->setText("platinum");
        QCOMPARE(window.accountProblem(), QString());
        QVERIFY(problemWith("installer-full-name", "Pat: Example").contains("colon"));
        QVERIFY(problemWith("installer-full-name", QString(129, 'a')).contains("128"));
        QVERIFY(problemWith("installer-user-name", QString(32, 'a')).contains("up to 31"));
        QCOMPARE(problemWith("installer-user-name", QString(31, 'a')), QString());
        QVERIFY(problemWith("installer-user-name", "2pat").contains("starts with a letter"));
        for (const auto *reserved : {"root", "liveuser", "sddm", "nobody"})
            QVERIFY(problemWith("installer-user-name", reserved).contains("belongs to the system"));
        find<QLineEdit>(window, "installer-verify")->setText("");
        QVERIFY(problemWith("installer-password", "seven77").contains("at least 8"));
        QVERIFY(problemWith("installer-password", QString(1025, 'p')).contains("up to 1024"));
        QVERIFY(problemWith("installer-password", "two\nlines").contains("one line"));
        find<QLineEdit>(window, "installer-verify")->setText("platinum");
        for (const auto *host : {"Platinum", "g4-", "-g4", "bad name!", ""})
            QVERIFY2(problemWith("installer-computer-name", host).contains("lowercase"), host);
        QCOMPARE(problemWith("installer-computer-name", "g4"), QString());
        find<QLineEdit>(window, "installer-verify")->setText("platinun");
        QCOMPARE(window.accountProblem(), QString("The two passwords are not the same."));
        QVERIFY(!next->isEnabled());
        find<QLineEdit>(window, "installer-verify")->setText("platinum");
        QVERIFY(next->isEnabled());
        find<QLineEdit>(window, "installer-computer-name")->setText("bad name!");
        QVERIFY(!next->isEnabled());
        find<QLineEdit>(window, "installer-computer-name")->setText("platinum-g4");
        next->click();
        QCOMPARE(window.page(), InstallerWindow::Install);
        QCOMPARE(next->text(), QString("Start"));
        QVERIFY(find<QLabel>(window, "installer-summary")->text().contains("/dev/vda"));
        QVERIFY(find<QLabel>(window, "installer-summary")->text().contains("The disk will be encrypted"));
        // Go Back keeps every answer.
        find<QPushButton>(window, "installer-back")->click();
        QCOMPARE(window.page(), InstallerWindow::Account);
        QCOMPARE(find<QLineEdit>(window, "installer-password")->text(), QString("platinum"));
        next->click();
        QCOMPARE(window.page(), InstallerWindow::Install);
    }
  private slots:
    void realQuestionUsesItsExplicitDefault_data()
    {
        QTest::addColumn<bool>("license");
        QTest::addColumn<int>("focusChoice");
        QTest::addColumn<int>("key");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("erase-return-initial-focus") << false << -1 << int(Qt::Key_Return) << false;
        QTest::newRow("license-return-initial-focus") << true << -1 << int(Qt::Key_Return) << true;
        QTest::newRow("erase-return-focused-accept") << false << 1 << int(Qt::Key_Return) << false;
        QTest::newRow("erase-enter-focused-accept") << false << 1 << int(Qt::Key_Enter) << false;
        QTest::newRow("erase-return-focused-cancel") << false << 0 << int(Qt::Key_Return) << false;
        QTest::newRow("erase-space-focused-accept") << false << 1 << int(Qt::Key_Space) << true;
        QTest::newRow("erase-space-focused-cancel") << false << 0 << int(Qt::Key_Space) << false;
        QTest::newRow("erase-escape-focused-accept") << false << 1 << int(Qt::Key_Escape) << false;
        QTest::newRow("license-return-focused-reject") << true << 0 << int(Qt::Key_Return) << true;
    }
    void realQuestionUsesItsExplicitDefault()
    {
        if (qEnvironmentVariableIsSet("PLATINUM_TEST_REQUIRE_KDE_THEME"))
            QVERIFY2(kdePlatformThemeLoaded(), "The actual KDE Qt platform theme is not loaded; fallback cannot qualify this test.");
        QFETCH(bool, license); QFETCH(int, focusChoice); QFETCH(int, key); QFETCH(bool, accepted);
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qunsetenv("FAKE_ENGINE_FAIL");
        QApplication::setStyle(new PlatinumStyle);
        const auto restore = qScopeGuard([] { QApplication::setStyle("Fusion"); });
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        find<QPushButton>(window, "installer-continue")->click(); // Destination starts disk discovery.
        QTRY_COMPARE(find<QComboBox>(window, "installer-disks")->count(), 2);
        bool observed = false, timedOut = false;
        QTimer poll, watchdog;
        poll.setInterval(10); watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &window, [&] {
            timedOut = true; poll.stop();
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->reject();
        });
        connect(&poll, &QTimer::timeout, &window, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            poll.stop();
            QPushButton *focused = nullptr;
            if (focusChoice < 0)
                focused = qobject_cast<QPushButton *>(box->focusWidget());
            else
                for (auto *button : box->buttons())
                    if (box->buttonRole(button) == (focusChoice ? QMessageBox::AcceptRole : QMessageBox::RejectRole))
                        focused = qobject_cast<QPushButton *>(button);
            if (!focused) { box->reject(); return; }
            if (focusChoice >= 0) focused->setFocus(Qt::TabFocusReason);
            QCoreApplication::processEvents();
            observed = focused->hasFocus();
            qInfo() << "Question" << box->windowTitle() << "focus" << focused->text()
                    << "default" << (box->defaultButton() ? box->defaultButton()->text() : QString());
            const auto evidence = qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR");
            if (!evidence.isEmpty())
                box->grab().save(evidence + "/installer-question-" + QTest::currentDataTag() + ".png");
            QTest::keyClick(focused, Qt::Key(key), key == Qt::Key_Enter ? Qt::KeypadModifier : Qt::NoModifier);
        });
        poll.start(); watchdog.start(3000);
        // Real QMessageBox::exec and key routing. No setConfirmationAnswers.
        const bool result = window.ask(license ? "License" : "Erase Disk", "Synthetic test question.",
                                       license ? "Agree" : "Erase and Install", license ? "Disagree" : "Cancel", license);
        poll.stop(); watchdog.stop();
        QVERIFY(!timedOut); QVERIFY(observed); QCOMPARE(result, accepted);
        const auto made = calls(root.filePath("engine.jsonl"));
        for (const auto &call : made) QVERIFY(call.value("argv").toArray().first().toString() != "install");
    }
    void installsOnTheChosenDiskWithThePasswordOnlyOnStandardInput()
    {
        QTemporaryDir root;
        const auto log = root.filePath("engine.jsonl");
        qputenv("FAKE_ENGINE_LOG", log.toLocal8Bit());
        qunsetenv("FAKE_ENGINE_FAIL");
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        reachInstall(window);
        auto *next = find<QPushButton>(window, "installer-continue");
        window.setConfirmationAnswers(true, false);
        next->click();
        // The erase question comes after the read-only plan and says what the disk holds now.
        QTRY_VERIFY(window.lastQuestion().startsWith("Erase"));
        QTRY_VERIFY(next->isEnabled());
        QCOMPARE(window.page(), InstallerWindow::Install); // Cancel erases nothing.
        QCOMPARE(calls(log).size(), 2);                      // The disk list and the plan, nothing else.
        const auto question = window.lastQuestion();
        QVERIFY2(question.contains("This disk has 1 partition now:\n  /dev/vda1  64 GB  ext4  “old”"),
                 qPrintable(question));
        QVERIFY(question.contains("every listed partition will be removed"));
        window.setConfirmationAnswers(true, true);
        next->click();
        QTRY_COMPARE(window.page(), InstallerWindow::Finished);
        QVERIFY(find<QPushButton>(window, "installer-restart")->isVisible());
        QVERIFY(find<QLabel>(window, "installer-finished-text")->text().startsWith("The installation process has finished."));
        QVERIFY(find<QPushButton>(window, "installer-restart")->isDefault());
        const auto made = calls(log);
        QCOMPARE(made.size(), 4); // Disks, the cancelled plan, the plan, the installation.
        QCOMPARE(made[2].value("argv").toArray().first().toString(), QString("plan"));
        const auto install = made[3];
        const auto argv = install.value("argv").toArray().toVariantList();
        QVERIFY(argv.contains("--confirm-erase") && argv.contains("/dev/vda"));
        QVERIFY(!QJsonDocument(install.value("argv").toArray()).toJson().contains("platinum\""));
        QCOMPARE(install.value("password_length").toInt(), 8);
        // Encrypted by default, unlocked with the same password.
        QCOMPARE(install.value("passphrase_length").toInt(), 8);
        const auto config = install.value("config").toObject();
        QCOMPARE(config.value("disk").toString(), QString("/dev/vda"));
        QCOMPARE(config.value("hostname").toString(), QString("platinum-g4"));
        QCOMPARE(config.value("user").toObject().value("name").toString(), QString("patexample"));
        QVERIFY(!config.contains("password"));
        QVERIFY(config.value("encrypt").toBool());
        QVERIFY(!QJsonDocument(config).toJson().contains("platinum\"")); // Not in the config file either.
        // The fields are cleared once the engine has the password.
        QVERIFY(find<QLineEdit>(window, "installer-password")->text().isEmpty());
    }
    void aTourOfEveryPageInThePlatinumLook()
    {
        // Each page as people see it: the Platinum style and font, saved to FRANKENSTEIN_EVIDENCE_DIR
        // when that is set (for visual review), and every page must at least render.
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qunsetenv("FAKE_ENGINE_FAIL");
        QApplication::setStyle(new PlatinumStyle);
        const auto font = QApplication::font();
        QApplication::setFont(classicInterfaceFont());
        const auto restore = qScopeGuard([&] { QApplication::setFont(font); QApplication::setStyle("Fusion"); });
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Platinum Reality is a Mac OS 9-style desktop for Arch Linux.\n\n"
                               "The installer erases the whole disk you choose.", "MIT License\n\nCopyright (c) psn_tch");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto evidence = qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR");
        const auto snap = [&](const QString &name) {
            QTest::qWait(50);
            const auto picture = window.grab();
            QVERIFY(!picture.isNull() && picture.width() > 300);
            if (!evidence.isEmpty())
                QVERIFY(picture.save(evidence + "/installer-" + name + ".png"));
        };
        auto *next = find<QPushButton>(window, "installer-continue");
        snap("1-welcome");
        next->click();
        QTRY_COMPARE(find<QComboBox>(window, "installer-disks")->count(), 2);
        snap("2-destination");
        next->click();
        snap("3-information");
        next->click();
        snap("4-license");
        window.setConfirmationAnswers(true, false);
        next->click();
        auto *name = find<QLineEdit>(window, "installer-full-name");
        name->setFocus();
        QTest::keyClicks(name, "Pat Example"); // Typed, so the short name fills in as it would for people.
        find<QLineEdit>(window, "installer-password")->setText("platinum");
        find<QLineEdit>(window, "installer-verify")->setText("platinum");
        find<QLineEdit>(window, "installer-computer-name")->setText("platinum-g4");
        QCOMPARE(window.accountProblem(), QString());
        snap("5-account");
        next->click();
        QCOMPARE(window.page(), InstallerWindow::Install);
        snap("6-install");
        window.setConfirmationAnswers(true, true);
        connect(&window, &InstallerWindow::pageChanged, &window, [&](InstallerWindow::Page shown) {
            if (shown == InstallerWindow::Progress && !evidence.isEmpty())
                window.grab().save(evidence + "/installer-6b-progress.png");
        });
        next->click();
        QTRY_COMPARE(window.page(), InstallerWindow::Finished);
        auto *alert = window.findChild<QDialog *>("installer-finished");
        QTRY_VERIFY(alert && alert->isVisible());
        if (!evidence.isEmpty())
            QVERIFY(alert->grab().save(evidence + "/installer-7-finished.png"));
    }
    void returnPressesContinueFromWhateverHasFocus()
    {
        // Mac OS 9: Return is the ringed button, Space the focused one. Qt does this only in dialogs.
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qunsetenv("FAKE_ENGINE_FAIL");
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *back = find<QPushButton>(window, "installer-back");
        auto *disks = find<QComboBox>(window, "installer-disks");
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.page(), InstallerWindow::Destination);
        QTest::keyClick(&window, Qt::Key_Return); // Before the disks are listed, Select is dimmed.
        QCOMPARE(window.page(), InstallerWindow::Destination);
        QTRY_COMPARE(disks->count(), 2);
        back->setFocus();
        QTest::keyClick(back, Qt::Key_Return); // Go Back has focus, but Return is Continue.
        QCOMPARE(window.page(), InstallerWindow::Information);
        QTextBrowser *information = nullptr;
        for (auto *browser : window.findChildren<QTextBrowser *>())
            if (browser->isVisible())
                information = browser;
        QVERIFY(information);
        information->setFocus();
        QTest::keyClick(information, Qt::Key_Enter, Qt::KeypadModifier); // The read-only text, keypad Enter.
        QCOMPARE(window.page(), InstallerWindow::License);
        QTest::keyClick(back, Qt::Key_Space);    // Space is the focused button.
        QCOMPARE(window.page(), InstallerWindow::Information);
        QTest::keyClick(&window, Qt::Key_Return, Qt::ShiftModifier); // Not with a modifier.
        QCOMPARE(window.page(), InstallerWindow::Information);
        QTest::keyClick(&window, Qt::Key_Return);
        window.setConfirmationAnswers(true, false);
        QTest::keyClick(&window, Qt::Key_Return); // Agree.
        QCOMPARE(window.page(), InstallerWindow::Account);
        auto *name = find<QLineEdit>(window, "installer-full-name");
        name->setFocus();
        QTest::keyClicks(name, "Pat Example");
        find<QLineEdit>(window, "installer-password")->setText("platinum");
        find<QLineEdit>(window, "installer-verify")->setText("platinum");
        find<QLineEdit>(window, "installer-computer-name")->setText("platinum-g4");
        QTest::keyClick(name, Qt::Key_Return);    // From a text field, too.
        QCOMPARE(window.page(), InstallerWindow::Install);
        QTest::keyClick(&window, Qt::Key_Return); // Start asks first; the answer here is Cancel.
        QCOMPARE(window.page(), InstallerWindow::Install);
    }
    void keyboardOnlyTourUsesRealQuestionsAndReturnNeverInstalls()
    {
        // A widget/fixture proof, not an immutable-image installation. No pointer clicks,
        // setFocus/setCurrentRow/setText, or confirmation-answer bypasses in this tour.
        QTemporaryDir root;
        const auto log=root.filePath("keyboard-engine.jsonl");
        qputenv("FAKE_ENGINE_LOG",log.toLocal8Bit());
        qunsetenv("FAKE_ENGINE_FAIL");
        QApplication::setStyle(new PlatinumStyle);
        const auto restore=qScopeGuard([]{QApplication::setStyle("Fusion");});
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine,"Read me.","MIT License");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        const auto key=[&](Qt::Key code,Qt::KeyboardModifiers mods=Qt::NoModifier){
            QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : &window,code,mods);
            QCoreApplication::processEvents(); // Includes queued focus restoration after a modal closes.
        };
        const auto tabTo=[&](const QString &name){
            for(int n=0;n<24;++n){
                if(QApplication::focusWidget() && QApplication::focusWidget()->objectName()==name)return true;
                key(Qt::Key_Tab);
            }
            qInfo()<<"Keyboard tour could not reach"<<name<<"active"<<QApplication::activeWindow()
                   <<"focus"<<QApplication::focusWidget();
            return false;
        };
        key(Qt::Key_Return);
        QCOMPARE(window.page(),InstallerWindow::Destination);
        auto *disks=find<QComboBox>(window,"installer-disks");
        QTRY_COMPARE(disks->count(),2);
        QVERIFY(tabTo("installer-disks"));
        key(Qt::Key_Up); // The dimmed installation disk can't be chosen from the keyboard either.
        QCOMPARE(disks->currentIndex(),1);
        key(Qt::Key_Return);
        QCOMPARE(window.page(),InstallerWindow::Information);
        QVERIFY(tabTo("installer-back"));
        key(Qt::Key_Space);
        QCOMPARE(window.page(),InstallerWindow::Destination);
        key(Qt::Key_Return);
        QCOMPARE(window.page(),InstallerWindow::Information);
        key(Qt::Key_Return);
        QCOMPARE(window.page(),InstallerWindow::License);
        int questionMode=0,answered=0;
        bool timedOut=false,unexpected=false;
        QTimer poll,watchdog;
        poll.setInterval(10);watchdog.setSingleShot(true);
        connect(&watchdog,&QTimer::timeout,&window,[&]{
            timedOut=true;poll.stop();
            if(auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))box->reject();
        });
        connect(&poll,&QTimer::timeout,&window,[&]{
            auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if(!box)return;
            if(!box->focusWidget() || !box->focusWidget()->hasFocus())return;
            poll.stop();
            const bool erase=box->windowTitle().contains("Erase");
            if(erase!=(questionMode>0)){unexpected=true;box->reject();return;}
            ++answered;
            if(questionMode==2){
                for(int n=0;n<8 && box->buttonRole(qobject_cast<QAbstractButton *>(QApplication::focusWidget()))!=QMessageBox::AcceptRole;++n)
                    QTest::keyClick(box->focusWidget(),Qt::Key_Tab);
                if(box->buttonRole(qobject_cast<QAbstractButton *>(QApplication::focusWidget()))!=QMessageBox::AcceptRole){
                    unexpected=true;box->reject();return;
                }
                QTest::keyClick(box->focusWidget(),Qt::Key_Space); // Deliberate focused Erase.
            }else QTest::keyClick(box->focusWidget(),Qt::Key_Return);
            watchdog.stop();
        });
        poll.start();watchdog.start(3000);
        key(Qt::Key_Return); // Real license dialog, default Agree.
        QTRY_COMPARE(window.page(),InstallerWindow::Account);
        QVERIFY(!timedOut && !unexpected);QCOMPARE(answered,1);
        // Offscreen has no window manager to reactivate the parent after a modal
        // closes. Supply only that venue event; Tab still has to find every field.
        // Real Wayland/X11 runs must restore activation themselves.
        if(QGuiApplication::platformName()=="offscreen"){window.activateWindow();QCoreApplication::processEvents();}
        for(auto field:{std::pair{"installer-full-name","Pat Example"},
                       {"installer-user-name","keyboarduser"},{"installer-password","platinum"},
                       {"installer-verify","platinum"},{"installer-computer-name","keyboard-g4"}}){
            QVERIFY2(tabTo(field.first),field.first);
            key(Qt::Key_A,Qt::ControlModifier);
            QTest::keyClicks(QApplication::focusWidget(),field.second);
        }
        for(const auto *name:{"installer-language","installer-time-zone","installer-keymap"}){
            auto *combo=find<QComboBox>(window,name);
            QTRY_VERIFY(combo->count()>=2);
            QVERIFY2(tabTo(name),name);
            key(Qt::Key_Home);key(Qt::Key_Down);
            QCOMPARE(combo->currentIndex(),1);
        }
        QVERIFY(find<QCheckBox>(window,"installer-encrypt")->isChecked()); // Destination ▸ Options…
        key(Qt::Key_Return);
        QCOMPARE(window.page(),InstallerWindow::Install);
        questionMode=1;poll.start();watchdog.start(3000);
        key(Qt::Key_Return); // Real occupied-disk erase question: Return must cancel.
        QTRY_COMPARE(answered,2);
        QTRY_VERIFY(find<QPushButton>(window,"installer-continue")->isEnabled());
        QCOMPARE(window.page(),InstallerWindow::Install);
        QVERIFY(!timedOut && !unexpected);
        if(QGuiApplication::platformName()=="offscreen"){window.activateWindow();QCoreApplication::processEvents();}
        for(const auto &call:calls(log))QVERIFY(call.value("argv").toArray().first().toString()!="install");
        questionMode=2;poll.start();watchdog.start(3000);
        key(Qt::Key_Return);
        QTRY_COMPARE(window.page(),InstallerWindow::Finished);
        QVERIFY(!timedOut && !unexpected);QCOMPARE(answered,3);
        const auto made=calls(log);
        QCOMPARE(made.last().value("argv").toArray().first().toString(),QString("install"));
        const auto config=made.last().value("config").toObject();
        QCOMPARE(config.value("hostname").toString(),QString("keyboard-g4"));
        QCOMPARE(config.value("user").toObject().value("name").toString(),QString("keyboarduser"));
        QVERIFY(config.value("encrypt").toBool());
        for(auto pair:{std::pair{"locale","installer-language"},{"timezone","installer-time-zone"},{"keymap","installer-keymap"}})
            QCOMPARE(config.value(pair.first).toString(),find<QComboBox>(window,pair.second)->currentData().toString());
        auto *alert=window.findChild<QDialog *>("installer-finished");
        QTRY_VERIFY(alert && alert->isVisible());
        if(QGuiApplication::platformName()=="offscreen"){alert->activateWindow();QCoreApplication::processEvents();}
        QVERIFY(tabTo("installer-restart")); // Reachability only: do not restart anything.
    }
    void tabMovesThroughTheAccountPageInReadingOrder()
    {
        // Keyboard-only installation: Tab visits each control in the order it is read, then the buttons.
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qunsetenv("FAKE_ENGINE_FAIL");
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        auto *next = find<QPushButton>(window, "installer-continue");
        next->click();
        QTRY_COMPARE(find<QComboBox>(window, "installer-disks")->count(), 2);
        next->click();
        next->click();
        window.setConfirmationAnswers(true, false);
        next->click();
        QCOMPARE(window.page(), InstallerWindow::Account);
        find<QLineEdit>(window, "installer-full-name")->setFocus();
        QStringList visited;
        for (int i = 0; i < 10; ++i)
        {
            QTRY_VERIFY(QApplication::focusWidget());
            visited << QApplication::focusWidget()->objectName();
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
        }
        QCOMPARE(visited, (QStringList{"installer-full-name", "installer-user-name", "installer-password",
                                       "installer-verify", "installer-computer-name", "installer-language",
                                       "installer-time-zone", "installer-keymap", "installer-back",
                                       "installer-full-name"})); // Start is dimmed until the account is filled in.
    }
    void aFailedInstallSaysWhereItStoppedAndOffersNoRestart()
    {
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qputenv("FAKE_ENGINE_FAIL", "1");
        const auto unset = qScopeGuard([] { qunsetenv("FAKE_ENGINE_FAIL"); });
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        reachInstall(window);
        find<QPushButton>(window, "installer-back")->click();
        find<QCheckBox>(window, "installer-encrypt")->setChecked(false);
        find<QPushButton>(window, "installer-continue")->click();
        QVERIFY(!find<QLabel>(window, "installer-summary")->text().contains("encrypted"));
        window.setConfirmationAnswers(true, true);
        find<QPushButton>(window, "installer-continue")->click();
        QTRY_COMPARE(window.page(), InstallerWindow::Finished);
        QVERIFY(!find<QPushButton>(window, "installer-restart")->isVisible());
        const auto made = calls(root.filePath("engine.jsonl"));
        QCOMPARE(made.last().value("passphrase_length").toInt(), 0);
        QVERIFY(!made.last().value("config").toObject().value("encrypt").toBool());
        const auto text = find<QLabel>(window, "installer-finished-text")->text();
        QVERIFY(text.contains("The disk could not be erased."));
        // It failed after erasing began: say so plainly, never "nothing was changed".
        QVERIFY2(text.contains("already started erasing “Hard Disk (/dev/vda) — 64 GB”, so what was on it before is gone"),
                 qPrintable(text));
        QVERIFY(!text.contains("Nothing was changed"));
    }
    void aRefusalBeforeErasingSaysTheDiskWasNotChanged()
    {
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qputenv("FAKE_ENGINE_FAIL", "validate");
        const auto unset = qScopeGuard([] { qunsetenv("FAKE_ENGINE_FAIL"); });
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        reachInstall(window);
        window.setConfirmationAnswers(true, true);
        find<QPushButton>(window, "installer-continue")->click();
        QTRY_COMPARE(window.page(), InstallerWindow::Finished);
        const auto text = find<QLabel>(window, "installer-finished-text")->text();
        QVERIFY2(text.contains("Nothing was changed: “Hard Disk (/dev/vda) — 64 GB” was not erased."), qPrintable(text));
        QVERIFY(!text.contains("already started erasing"));
    }
    void theAccountPageOffersWhatTheLiveImageCanInstall()
    {
        // The engine's `choices` lists replace the built-in ones; malformed names are dropped.
        QTemporaryDir root;
        const auto log = root.filePath("engine.jsonl");
        qputenv("FAKE_ENGINE_LOG", log.toLocal8Bit());
        qunsetenv("FAKE_ENGINE_FAIL");
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        reachInstall(window); // Clicks on at once: the plan waits for the slow choices answer.
        auto *language = find<QComboBox>(window, "installer-language");
        QTRY_COMPARE(language->count(), 3);
        QCOMPARE(language->currentData().toString(), QString("en_US.UTF-8"));
        QVERIFY(language->currentText().startsWith("English (United States)"));
        QCOMPARE(find<QComboBox>(window, "installer-keymap")->count(), 2);
        QCOMPARE(find<QComboBox>(window, "installer-time-zone")->count(), 3);
        find<QPushButton>(window, "installer-back")->click();
        language->setCurrentIndex(language->findData("de_DE.UTF-8"));
        find<QComboBox>(window, "installer-keymap")->setCurrentIndex(find<QComboBox>(window, "installer-keymap")->findText("de"));
        find<QPushButton>(window, "installer-continue")->click();
        QTRY_COMPARE(window.page(), InstallerWindow::Install);
        window.setConfirmationAnswers(true, true);
        find<QPushButton>(window, "installer-continue")->click();
        QTRY_COMPARE(window.page(), InstallerWindow::Finished);
        const auto config = calls(log).last().value("config").toObject();
        QCOMPARE(config.value("locale").toString(), QString("de_DE.UTF-8"));
        QCOMPARE(config.value("keymap").toString(), QString("de"));
    }
    void anEngineWithoutChoicesKeepsTheBuiltInLists()
    {
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qputenv("FAKE_ENGINE_NO_CHOICES", "1");
        const auto unset = qScopeGuard([] { qunsetenv("FAKE_ENGINE_NO_CHOICES"); });
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        reachInstall(window);
        QTest::qWait(300);
        QCOMPARE(find<QComboBox>(window, "installer-language")->count(), 1);
        QCOMPARE(find<QComboBox>(window, "installer-keymap")->count(), 8);
        QVERIFY(find<QLabel>(window, "installer-summary")->text().contains("/dev/vda")); // No error shown.
    }
    void destinationOptionsChooseEncryptionAndCancelKeepsTheChoice()
    {
        // 9.2.2's Select Destination ▸ Options… asked about a clean installation; here it is encryption.
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qunsetenv("FAKE_ENGINE_FAIL");
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *options = find<QPushButton>(window, "installer-options");
        QVERIFY(!options->isVisible());
        find<QPushButton>(window, "installer-continue")->click();
        QTRY_COMPARE(find<QComboBox>(window, "installer-disks")->count(), 2);
        QVERIFY(options->isVisible());
        auto *encrypt = find<QCheckBox>(window, "installer-encrypt");
        const auto answer = [&](bool ok) {
            QTimer::singleShot(50, &window, [&, ok] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog && encrypt->isVisible());
                encrypt->click();
                ok ? dialog->accept() : dialog->reject();
            });
            options->click();
        };
        answer(false); // Cancel: still encrypted.
        QVERIFY(encrypt->isChecked());
        answer(true);  // OK: not encrypted.
        QVERIFY(!encrypt->isChecked());
    }
    void disksTooSmallToInstallOnAreDimmedAndNeverChosenFirst()
    {
        // The update-37 image's first actual install run: QEMU listed an empty DVD-ROM drive first,
        // and it was chosen to begin with ("too small", Select dimmed). Only a disk that can take the
        // installation is chosen; the others are listed dimmed, as the installation disk is.
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qputenv("FAKE_ENGINE_SMALL_DISKS", "1");
        qunsetenv("FAKE_ENGINE_FAIL");
        const auto unset = qScopeGuard([] { qunsetenv("FAKE_ENGINE_SMALL_DISKS"); });
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        find<QPushButton>(window, "installer-continue")->click();
        auto *disks = find<QComboBox>(window, "installer-disks");
        QTRY_COMPARE(disks->count(), 4);
        auto *model = qobject_cast<QStandardItemModel *>(disks->model());
        QVERIFY(model);
        QVERIFY(!model->item(0)->isEnabled()); // the installation disk
        QVERIFY(!model->item(1)->isEnabled() && disks->itemText(1).contains("QEMU DVD-ROM") &&
                disks->itemText(1).endsWith("too small"));
        QVERIFY(!model->item(2)->isEnabled() && disks->itemText(2).endsWith("too small"));
        QVERIFY(model->item(3)->isEnabled());
        QCOMPARE(disks->currentIndex(), 3);
        QVERIFY(disks->currentText().contains("/dev/vda"));
        QVERIFY(find<QPushButton>(window, "installer-continue")->isEnabled());
        QCOMPARE(window.config().value("disk").toString(), QString("/dev/vda"));
    }
    void theTimeZoneStartsOnTheComputersOwnOrUtcNeverTheFirstInTheList()
    {
        // On the live disk the clock is UTC: it used to start on Africa/Abidjan, the first zone.
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qunsetenv("FAKE_ENGINE_FAIL");
        const auto zone = qgetenv("TZ");
        qputenv("TZ", "UTC");
        const auto restore = qScopeGuard([&] { zone.isNull() ? qunsetenv("TZ") : qputenv("TZ", zone); });
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        auto *zones = find<QComboBox>(window, "installer-time-zone");
        QCOMPARE(zones->currentText(), QString("UTC"));
        find<QPushButton>(window, "installer-continue")->click(); // the engine's own lists arrive
        QTRY_COMPARE(zones->count(), 3);
        QCOMPARE(zones->currentText(), QString("UTC"));
    }
    void anEngineRefusalSaysWhyOnTheDestinationPage()
    {
        // psn_tch's first boot of the live USB on a real computer said only "could not list disks".
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qputenv("FAKE_ENGINE_REFUSE_HOST", "1");
        const auto unset = qScopeGuard([] { qunsetenv("FAKE_ENGINE_REFUSE_HOST"); });
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        find<QPushButton>(window, "installer-continue")->click();
        QTRY_VERIFY(find<QLabel>(window, "installer-destination-note")->text().contains("isn't turned on yet"));
        QVERIFY(find<QLabel>(window, "installer-destination-note")->text().contains("Nothing on your disks was changed"));
        QVERIFY(!find<QPushButton>(window, "installer-continue")->isEnabled());
    }
    void onARealComputerInternalDisksAreDimmedAndAUsbDriveIsChosen()
    {
        // psn_tch's potato: several internal disks, a USB SSD for a portable install.
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qputenv("FAKE_ENGINE_PHYSICAL", "usb");
        const auto unset = qScopeGuard([] { qunsetenv("FAKE_ENGINE_PHYSICAL"); });
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        find<QPushButton>(window, "installer-continue")->click();
        auto *disks = find<QComboBox>(window, "installer-disks");
        QTRY_COMPARE(disks->count(), 3);
        auto *model = qobject_cast<QStandardItemModel *>(disks->model());
        QVERIFY(model && !model->item(1)->isEnabled() && disks->itemText(1).endsWith("internal disk"));
        QVERIFY(model->item(2)->isEnabled());
        QCOMPARE(window.config().value("disk").toString(), QString("/dev/sdc"));
        QVERIFY(find<QPushButton>(window, "installer-continue")->isEnabled());
    }
    void withoutAUsbDriveARealComputerSaysToPlugOneIn()
    {
        QTemporaryDir root;
        qputenv("FAKE_ENGINE_LOG", root.filePath("engine.jsonl").toLocal8Bit());
        qputenv("FAKE_ENGINE_PHYSICAL", "1");
        const auto unset = qScopeGuard([] { qunsetenv("FAKE_ENGINE_PHYSICAL"); });
        EngineClient engine(QFINDTESTDATA("fake_install_engine.py"));
        InstallerWindow window(&engine, "Read me.", "MIT License");
        window.show();
        find<QPushButton>(window, "installer-continue")->click();
        QTRY_COMPARE(find<QComboBox>(window, "installer-disks")->count(), 2);
        QVERIFY(find<QLabel>(window, "installer-destination-note")->text().contains("only onto a USB drive"));
        QVERIFY(!find<QPushButton>(window, "installer-continue")->isEnabled());
    }
    void aMissingEngineIsReportedOnTheDestinationPage()
    {
        EngineClient engine("/nonexistent/platinum-reality-install");
        InstallerWindow window(&engine, "", "");
        window.show();
        find<QPushButton>(window, "installer-continue")->click();
        QTRY_VERIFY(find<QLabel>(window, "installer-destination-note")->text().contains("could not start its disk tool"));
        QVERIFY(!find<QPushButton>(window, "installer-continue")->isEnabled());
    }
};

QTEST_MAIN(InstallerTests)
#include "installer_tests.moc"
