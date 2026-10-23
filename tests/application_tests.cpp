#include "applications/application_appearance.hpp"
#include "applications/application_catalog.hpp"
#include "applications/launch_environment.hpp"
#include "applications/emulator_launch.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
using namespace frankenstein::applications;
namespace
{
bool write(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
QByteArray entry(const QByteArray &command, const QByteArray &extra = {})
{
    return "[Desktop Entry]\nType=Application\nName=Test Program\nExec=" + command + "\n" + extra;
}
QStringList ids(ApplicationCatalog &catalog)
{
    QStringList result;
    for (int i = 0; i < catalog.count(); ++i)
        result << catalog.item(i).value("id").toString();
    return result;
}
} // namespace
class ApplicationTests : public QObject
{
    Q_OBJECT
  private slots:
    void normalApplicationsUseTheKdeWalletWithoutChangingExplicitChoices()
    {
        QTemporaryDir fixture;
        QProcessEnvironment environment;
        environment.insert("HOME", fixture.path());
        environment.insert("XDG_CONFIG_HOME", fixture.filePath("config"));
        QString error;
        for (const auto *program : {"/usr/bin/code", "/usr/share/code/bin/code", "/usr/share/code/code",
                                    "/usr/bin/chromium", "/usr/bin/google-chrome-stable"}) {
            if (!QFileInfo::exists(QString::fromLatin1(program))) continue;
            QStringList arguments{"--new-window", "https://example.invalid/"};
            const auto original = arguments;
            QVERIFY(configureKnownApplicationBackend(program, arguments, environment, error));
            QCOMPARE(arguments, original); // Private/nested mode keeps its existing behavior.
            QVERIFY(configureKnownApplicationBackend(program, arguments, environment, error, true));
            QCOMPARE(arguments, QStringList{"--password-store=kwallet6"} + original);
            for (const auto &explicitChoice : {QStringList{"--password-store=gnome-libsecret"},
                                              QStringList{"--password-store", "basic"}}) {
                auto selected = explicitChoice;
                QVERIFY(configureKnownApplicationBackend(program, selected, environment, error, true, true));
                QCOMPARE(selected, explicitChoice);
            }
            arguments = original;
            QVERIFY(!configureKnownApplicationBackend(program, arguments, environment, error, false, true));
            QCOMPARE(arguments, original);
            QVERIFY(!error.isEmpty());
            arguments = {"--", "--password-store=basic"}; // A document after -- is not a setting.
            QVERIFY(configureKnownApplicationBackend(program, arguments, environment, error, true));
            QCOMPARE(arguments.first(), QString("--password-store=kwallet6"));
        }
        const auto custom = fixture.filePath("code");
        QVERIFY(write(custom, "#!/bin/sh\nexit 0\n"));
        QStringList arguments;
        QVERIFY(configureKnownApplicationBackend(custom, arguments, environment, error, true, true));
        QVERIFY(arguments.isEmpty());
        QVERIFY(configureKnownApplicationBackend("/usr/bin/true", arguments, environment, error, true, true));
        QVERIFY(arguments.isEmpty());
    }
    void walletBackendHonorsRuntimeAndWrapperPreferences()
    {
        QTemporaryDir fixture;
        QProcessEnvironment environment;
        environment.insert("HOME", fixture.path());
        environment.insert("XDG_CONFIG_HOME", fixture.filePath("config"));
        QString error;
        for (const auto &choice : {qMakePair(QString("/usr/bin/code"), QString("code-flags.conf")),
                                   qMakePair(QString("/usr/bin/chromium"), QString("chromium-flags.conf")),
                                   qMakePair(QString("/usr/bin/google-chrome-stable"), QString("chrome-flags.conf"))}) {
            if (!QFileInfo::exists(choice.first)) continue;
            const auto path = fixture.filePath("config/" + choice.second);
            QVERIFY(write(path, "# --password-store=basic\n--new-window\n"));
            QStringList arguments;
            QVERIFY(configureKnownApplicationBackend(choice.first, arguments, environment, error, true));
            QCOMPARE(arguments, QStringList{"--password-store=kwallet6"});
            QVERIFY(write(path, "--password-store=gnome-libsecret\n"));
            arguments.clear();
            QVERIFY(configureKnownApplicationBackend(choice.first, arguments, environment, error, true));
            QVERIFY(arguments.isEmpty());
            QVERIFY(QFile::remove(path));
        }
        if (QFileInfo::exists("/usr/bin/code")) {
            QVERIFY(write(fixture.filePath(".vscode/argv.json"), "{\n // \"password-store\": \"basic\"\n /* \"password-store\": \"basic\" */\n \"enable-crash-reporter\": false\n}\n"));
            QStringList arguments;
            QVERIFY(configureKnownApplicationBackend("/usr/bin/code", arguments, environment, error, true, true));
            QCOMPARE(arguments, QStringList{"--password-store=kwallet6"});
            QVERIFY(write(fixture.filePath(".vscode/argv.json"), "{\n // Owner preference\n \"password-store\": \"kwallet6\"\n}\n"));
            arguments.clear();
            QVERIFY(configureKnownApplicationBackend("/usr/bin/code", arguments, environment, error, true, true));
            QVERIFY(arguments.isEmpty());
        }
    }
    void walletAllowanceNeverLeaksIntoLaunchedApplications()
    {
        QTemporaryDir fixture;
        QProcessEnvironment environment;
        for (const auto *key : {"PLATINUM_WALLET_LOGIN", "PLATINUM_WALLET_SERVICES",
                                "PLATINUM_WALLET_REQUESTED", "PLATINUM_WALLET_ERROR"})
            environment.insert(key, "1");
        const auto child = nativeApplicationEnvironment(environment, "private-socket", fixture.path(), {}, true);
        for (const auto *key : {"PLATINUM_WALLET_LOGIN", "PLATINUM_WALLET_SERVICES",
                                "PLATINUM_WALLET_REQUESTED", "PLATINUM_WALLET_ERROR"})
            QVERIFY(!child.contains(key));
        QCOMPARE(child.value("XDG_CURRENT_DESKTOP"), QString("Frankenstein"));
    }
    void kittyBackendRequiresPrivateX11AndPreservesExplicitChoices()
    {
        if(!QFileInfo::exists("/usr/bin/kitty"))QSKIP("Installed kitty compatibility fixture");
        QStringList arguments{"--title","Fixture"};
        const auto original=arguments;
        QProcessEnvironment environment;
        QString error;
        QVERIFY(!configureKnownApplicationBackend("/usr/bin/kitty",arguments,environment,error));
        QCOMPARE(arguments,original);
        QVERIFY(!error.isEmpty());
        environment.insert("DISPLAY",":17");
        environment.insert("XAUTHORITY","/owned/authority");
        QVERIFY(configureKnownApplicationBackend("/usr/bin/kitty",arguments,environment,error));
        QCOMPARE(arguments,QStringList({"--override","linux_display_server=x11"})+original);
        QStringList explicitWayland{"--override","linux_display_server=wayland"};
        QVERIFY(configureKnownApplicationBackend("/usr/bin/kitty",explicitWayland,environment,error));
        QCOMPARE(explicitWayland,QStringList({"--override","linux_display_server=wayland"}));
        arguments=original;
        QVERIFY(configureKnownApplicationBackend("/usr/bin/konsole",arguments,environment,error));
        QCOMPARE(arguments,original);
    }
    void terminalIsWhicheverTerminalIsInstalled()
    {
        QTemporaryDir first,second;
        const auto install=[](const QString &directory,const QString &name)
        {
            QFile program(directory+"/"+name);
            return program.open(QIODevice::WriteOnly) &&
                   program.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
        };
        const auto path=first.path()+":"+second.path();
        QVERIFY(terminalCommand(path).isEmpty());
        QVERIFY(install(second.path(),"xterm"));
        QCOMPARE(terminalCommand(path),QStringList({second.path()+"/xterm"}));
        QVERIFY(install(first.path(),"foot"));
        QCOMPARE(terminalCommand(path),QStringList({first.path()+"/foot"}));
        QVERIFY(install(second.path(),"konsole"));
        QCOMPARE(terminalCommand(path),QStringList({second.path()+"/konsole","--separate"}));
        QCOMPARE(terminalCommand(path,"xterm"),QStringList({second.path()+"/xterm"}));
        QCOMPARE(terminalCommand(path,"not-installed"),QStringList({second.path()+"/konsole","--separate"}));
    }
    void emulatorEntriesRetainExistingSettingsWithoutTheHostDisplay()
    {
        QTemporaryDir root;
        const auto path=root.filePath("emulator.desktop");
        QVERIFY(write(path,entry("/usr/bin/true","Categories=Game;Emulator;\n")));
        auto environment=QProcessEnvironment::systemEnvironment();
        environment.insert("XDG_CONFIG_HOME",root.filePath("Old settings"));
        environment.insert("DISPLAY",":99");
        auto metadata=readDesktopEntry(path,"emulator.desktop",environment);
        auto plan=planLaunch(metadata,{},root.path(),environment);
        QVERIFY(plan && plan.existingApplicationSettings);
        const auto child=nativeApplicationEnvironment(environment,"private-socket",root.filePath("profile"),{},
                                                       plan.existingApplicationSettings);
        QCOMPARE(child.value("XDG_CONFIG_HOME"),root.filePath("Old settings"));
        QCOMPARE(child.value("WAYLAND_DISPLAY"),QString("private-socket"));
        QVERIFY(!child.contains("DISPLAY"));
        QVERIFY(write(path,entry("/usr/bin/true","Categories=System;TerminalEmulator;\n")));
        metadata=readDesktopEntry(path,"terminal.desktop",environment);
        QVERIFY(!planLaunch(metadata,{},root.path(),environment).existingApplicationSettings);
    }
    void ymirEnvironmentPrefersOpenGLWithASoftwareFallback()
    {
        QProcessEnvironment environment;
        environment.insert("SDL_RENDER_DRIVER", "direct3d");
        configureKnownEmulatorEnvironment("/usr/bin/true", environment);
        QCOMPARE(environment.value("SDL_RENDER_DRIVER"), QString("direct3d")); // Other programs untouched.
        if (!QFileInfo::exists("/usr/bin/ymir-sdl3")) QSKIP("Optional installed Ymir adapter.");
        configureKnownEmulatorEnvironment("/usr/bin/ymir-sdl3", environment);
        QCOMPARE(environment.value("SDL_VIDEODRIVER"), QString("wayland"));
        QCOMPARE(environment.value("SDL_RENDER_DRIVER"), QString("opengl,software"));
    }
    void ymirAdapterPreservesTheKnownProfileAndRefusesToRewriteCustomWrappers()
    {
        if (!QFileInfo::exists("/usr/bin/ymir-sdl3")) QSKIP("Optional installed Ymir adapter.");
        QTemporaryDir root;
        QVERIFY(QDir().mkpath(root.filePath("Ymir-compatible-profile")));
        const auto path=root.filePath("ymir-compatible.sh");
        const QByteArray wrapper="#!/usr/bin/env bash\nset -euo pipefail\n"
            "base=\"$(cd -- \"$(dirname -- \"${BASH_SOURCE[0]}\")\" && pwd)\"\n"
            "export SDL_VIDEODRIVER=x11\nexport SDL_RENDER_DRIVER=opengl\n"
            "exec /usr/bin/ymir-sdl3 --profile \"$base/Ymir-compatible-profile\" \"$@\"\n";
        QVERIFY(write(path,wrapper));
        LaunchPlan plan{path,{"--paused"},{},{}};
        QString error;
        QVERIFY2(adaptKnownEmulatorLaunch(plan,error),qPrintable(error));
        QCOMPARE(plan.program,QString("/usr/bin/ymir-sdl3"));
        QCOMPARE(plan.arguments,(QStringList{"--profile",root.filePath("Ymir-compatible-profile"),"--paused"}));
        QVERIFY(plan.existingApplicationSettings);
        QFile source(path);QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),wrapper);source.close();
        QVERIFY(write(path,wrapper+"# custom behavior\n"));
        plan={path,{}, {},{}};
        QVERIFY(adaptKnownEmulatorLaunch(plan,error));
        QCOMPARE(plan.program,path);
        QVERIFY(plan.arguments.isEmpty());
    }
    void browserFrameRequiresExactlyTheOwnedExplicitProfile()
    {
        QTemporaryDir profile, other;
        QVERIFY(QDir().mkpath(profile.filePath("browsers/firefox")));
        QVERIFY(ownedFirefoxProfileArguments({"--profile", profile.filePath("browsers/firefox")},
                                             profile.path()));
        QVERIFY(ownedFirefoxProfileArguments({"--profile=" + profile.filePath("browsers/firefox")},
                                             profile.path()));
        QVERIFY(!ownedFirefoxProfileArguments({}, profile.path()));
        QVERIFY(!ownedFirefoxProfileArguments({"--profile", other.path()}, profile.path()));
        QVERIFY(!ownedFirefoxProfileArguments({"--profile", "browsers/firefox"}, profile.path()));
        QVERIFY(!ownedFirefoxProfileArguments(
            {"--profile", profile.filePath("browsers/firefox"), "--profile", other.path()},
            profile.path()));
        QVERIFY(!ownedFirefoxFrame(QCoreApplication::applicationPid(), profile.path()));
        QVERIFY(!ownedFirefoxFrame(0, profile.path()));
        // The frame itself is for every Firefox, the user's own profile included.
        QVERIFY(!firefoxClient(QCoreApplication::applicationPid()));
        QVERIFY(!firefoxClient(0));
        QVERIFY(!firefoxLaunch("/usr/bin/true"));
        if (QFileInfo::exists("/usr/lib/firefox/firefox"))
            QVERIFY(firefoxLaunch("/usr/lib/firefox/firefox"));
        QVERIFY(!ownedFirefoxLaunch(
            "/usr/bin/true", {"--profile", profile.filePath("browsers/firefox")}, profile.path()));
        if (QFileInfo::exists("/usr/lib/firefox/firefox"))
            QVERIFY(ownedFirefoxLaunch("/usr/lib/firefox/firefox",
                                       {"--profile", profile.filePath("browsers/firefox")},
                                       profile.path()));
    }
    void launchedGioSettingsPersistInTheProfileWithoutDconfRuntimeFiles()
    {
        QTemporaryDir profile;
        QVERIFY(QDir().mkpath(profile.filePath("runtime")));
        auto environment = nativeApplicationEnvironment(QProcessEnvironment::systemEnvironment(),
                                                        "unused", profile.path(), {});
        environment.insert("XDG_RUNTIME_DIR", profile.filePath("runtime"));
        const auto run = [&](const QStringList &arguments, QByteArray *output = nullptr)
        {
            QProcess child;
            child.setProcessEnvironment(environment);
            child.start("/usr/bin/gsettings", arguments);
            if (!child.waitForFinished() || child.exitStatus() != QProcess::NormalExit)
                return false;
            if (output)
                *output = child.readAllStandardOutput();
            return child.exitCode() == 0;
        };
        QVERIFY(run({"set", "org.gtk.Settings.FileChooser", "show-hidden", "true"}));
        QByteArray value;
        QVERIFY(run({"get", "org.gtk.Settings.FileChooser", "show-hidden"}, &value));
        QCOMPARE(value.trimmed(), QByteArray("true"));
        QVERIFY(QFileInfo::exists(profile.filePath("config/glib-2.0/settings/keyfile")));
        QCOMPARE(QDir(profile.filePath("runtime"))
                     .entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                     .size(),
                 0);
    }
    void gtkAppearancePreservesCustomRulesAndPreparationIsIdempotent()
    {
        QTemporaryDir profile;
        const auto cssPath = profile.filePath("config/gtk-3.0/gtk.css");
        const QByteArray custom = "button { padding: 7px; }\n";
        QVERIFY(write(cssPath, custom));
        QString error;
        QVERIFY2(prepareApplicationAppearance(profile.path(), error), qPrintable(error));
        QFile css(cssPath);
        QVERIFY(css.open(QIODevice::ReadOnly));
        const auto first = css.readAll();
        QVERIFY(first.startsWith("@import url(\"platinum-dreams.css\");\n"));
        QVERIFY(first.endsWith(custom));
        css.close();
        QVERIFY(prepareApplicationAppearance(profile.path(), error));
        QVERIFY(css.open(QIODevice::ReadOnly));
        QCOMPARE(css.readAll(), first);
        for (const auto &version : {"3.0", "4.0"})
        {
            QFile managed(
                profile.filePath(QString("config/gtk-%1/platinum-dreams.css").arg(version)));
            QVERIFY(managed.open(QIODevice::ReadOnly));
            const auto contents = managed.readAll();
            QVERIFY(contents.contains("Charcoal"));
            QVERIFY(contents.contains("scrollbar slider"));
        }
        // A file written before the rename (its old header) is still ours: replaced, no warning.
        const auto older = profile.filePath("config/gtk-3.0/platinum-dreams.css");
        QVERIFY(write(older, "/* Platinum Dreams application appearance v1. Original native adaptation. */\nold\n"));
        QVERIFY2(prepareApplicationAppearance(profile.path(), error), qPrintable(error));
        QFile replaced(older);
        QVERIFY(replaced.open(QIODevice::ReadOnly));
        QVERIFY(replaced.readAll().startsWith("/* Platinum Reality application appearance v1."));
        // Anything else is the user's and stays.
        QVERIFY(write(older, custom));
        QVERIFY(!prepareApplicationAppearance(profile.path(), error));
        QVERIFY(error.contains("custom contents"));
    }
    void privateGtkThemeImportsTheBaseThemeAndRefusesCustomContents()
    {
        // Normal application settings: a theme selected with GTK_THEME, inside the session profile.
        QTemporaryDir profile;
        QString error;
        const auto data = preparePrivateGtkTheme(profile.path(), error);
        QVERIFY2(!data.isEmpty(), qPrintable(error));
        QCOMPARE(data, QDir(profile.path()).absolutePath() + "/share");
        for (const auto &[version, base] : {std::pair{"3.0", "theme/Adwaita/gtk-contained.css"},
                                            std::pair{"4.0", "theme/Default/Default-light.css"}})
        {
            const auto directory = QString("%1/themes/%2/gtk-%3/").arg(data, privateGtkThemeName, version);
            QFile theme(directory + "gtk.css");
            QVERIFY(theme.open(QIODevice::ReadOnly));
            const auto contents = theme.readAll();
            QVERIFY(contents.contains(base));
            QVERIFY(contents.indexOf(base) < contents.indexOf("platinum-dreams.css"));
            QFile platinum(directory + "platinum-dreams.css");
            QVERIFY(platinum.open(QIODevice::ReadOnly));
            QVERIFY(platinum.readAll().contains("Charcoal"));
        }
        QCOMPARE(preparePrivateGtkTheme(profile.path(), error), data); // Idempotent.
        // A hand-edited theme is preserved, never overwritten.
        const auto custom = data + "/themes/" + privateGtkThemeName + "/gtk-3.0/gtk.css";
        QVERIFY(write(custom, "button { padding: 3px; }\n"));
        QVERIFY(preparePrivateGtkTheme(profile.path(), error).isEmpty());
        QVERIFY(error.contains("custom contents"));
    }
    void gtkAppearanceRejectsSymlinksWithoutChangingTheirTargets()
    {
        QTemporaryDir profile, outside;
        const auto privatePath = outside.filePath("Private.css");
        QVERIFY(write(privatePath, "unchanged"));
        QVERIFY(QDir().mkpath(profile.filePath("config/gtk-3.0")));
        QVERIFY(QFile::link(privatePath, profile.filePath("config/gtk-3.0/gtk.css")));
        QString error;
        QVERIFY(!prepareApplicationAppearance(profile.path(), error));
        QVERIFY(error.contains("symlink"));
        QFile original(privatePath);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("unchanged"));
    }
    void installedFirefoxGetsOnlyAnOwnedBrowserUiProfileAndLiteralArguments()
    {
        if (!QFileInfo::exists("/usr/lib/firefox/firefox"))
            QSKIP("The optional local Firefox adapter needs an installed Firefox executable.");
        QTemporaryDir profile;
        LaunchPlan plan{"/usr/lib/firefox/firefox", {"file:///tmp/a%20file%3B.html"}, {}, {}};
        QString error;
        QVERIFY2(prepareStyledApplication(plan, profile.path(), error), qPrintable(error));
        QCOMPARE(plan.arguments, (QStringList{"--profile", profile.filePath("browsers/firefox"),
                                              "file:///tmp/a%20file%3B.html"}));
        QFile preferences(profile.filePath("browsers/firefox/user.js"));
        QVERIFY(preferences.open(QIODevice::ReadOnly));
        const auto prefs = preferences.readAll();
        QVERIFY(prefs.contains("toolkit.legacyUserProfileCustomizations.stylesheets"));
        QVERIFY(prefs.contains("user_pref(\"browser.tabs.inTitlebar\", 1)"));
        QVERIFY(!prefs.contains("sandbox"));
        QVERIFY(!prefs.contains("security."));
        QFile chrome(profile.filePath("browsers/firefox/chrome/userChrome.css"));
        QVERIFY(chrome.open(QIODevice::ReadOnly));
        QVERIFY(chrome.readAll().contains("chrome://browser/content/browser.xhtml"));
        QVERIFY(!QFileInfo::exists(profile.filePath("browsers/firefox/chrome/userContent.css")));
        QVERIFY(!QFileInfo::exists(profile.filePath("browsers/firefox/profiles.ini")));
    }
    void applicationSpecificProfilesAndOtherExecutablesArePreserved()
    {
        QTemporaryDir profile;
        QString error;
        for (const auto &args :
             {QStringList{"--profile", "/custom/profile"}, QStringList{"-P", "Named profile"},
              QStringList{"--profile=/custom/profile"}, QStringList{"--ProfileManager"}})
        {
            LaunchPlan plan{"/usr/lib/firefox/firefox", args, {}, {}};
            QVERIFY(prepareStyledApplication(plan, profile.path(), error));
            QCOMPARE(plan.arguments, args);
            QVERIFY(!QFileInfo::exists(profile.filePath("browsers")));
        }
        LaunchPlan unrelated{"/usr/bin/true", {"--literal"}, {}, {}};
        QVERIFY(prepareStyledApplication(unrelated, profile.path(), error));
        QCOMPARE(unrelated.arguments, QStringList{"--literal"});
        QVERIFY(!QFileInfo::exists(profile.filePath("browsers")));
    }
    void browserCustomPreferencesAreNotOverwritten()
    {
        if (!QFileInfo::exists("/usr/lib/firefox/firefox"))
            QSKIP("The optional local Firefox adapter needs an installed Firefox executable.");
        QTemporaryDir profile;
        const auto preferencePath = profile.filePath("browsers/firefox/user.js");
        QVERIFY(write(preferencePath, "// personal preferences stay here\n"));
        LaunchPlan plan{"/usr/lib/firefox/firefox", {}, {}, {}};
        QString error;
        QVERIFY(!prepareStyledApplication(plan, profile.path(), error));
        QVERIFY(error.contains("preserved"));
        QVERIFY(plan.arguments.isEmpty());
        QFile input(preferencePath);
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), QByteArray("// personal preferences stay here\n"));
    }
    void fieldExpansionIsExplicitAndNeverRetokenizesDocuments()
    {
        QTemporaryDir root;
        const auto path = root.filePath("test.desktop");
        QVERIFY(write(path, entry("/usr/bin/true %F %c %k %i %%", "Icon=some icon\n")));
        const auto metadata =
            readDesktopEntry(path, "test.desktop", QProcessEnvironment::systemEnvironment());
        QVERIFY(metadata.unavailable.isEmpty());
        const QStringList documents{root.filePath("a file; $(touch NEVER).txt"),
                                    root.filePath("--option.txt")};
        const auto plan =
            planLaunch(metadata, documents, root.path(), QProcessEnvironment::systemEnvironment());
        QVERIFY2(bool(plan), qPrintable(plan.error));
        QCOMPARE(plan.arguments,
                 documents + QStringList({"Test Program", path, "--icon", "some icon", "%"}));
    }
    void unsafeOrAmbiguousCommandsAreRejected_data()
    {
        QTest::addColumn<QByteArray>("command");
        for (const auto &command :
             {"/usr/bin/true %Q", "/usr/bin/true %f %U", "/usr/bin/true --files=%F",
              "/usr/bin/true \"%f\"", "/usr/bin/true ; touch anything", "/usr/bin/true $(anything)",
              "/usr/bin/true \"unterminated", "%c argument"})
            QTest::newRow(command) << QByteArray(command);
    }
    void unsafeOrAmbiguousCommandsAreRejected()
    {
        QFETCH(QByteArray, command);
        QTemporaryDir root;
        QVERIFY(write(root.filePath("test.desktop"), entry(command)));
        const auto metadata = readDesktopEntry(root.filePath("test.desktop"), "id",
                                               QProcessEnvironment::systemEnvironment());
        QVERIFY(!metadata.unavailable.isEmpty());
        QVERIFY(!planLaunch(metadata, {}, root.path(), QProcessEnvironment::systemEnvironment()));
    }
    void urlsAndSingularDocumentLimits()
    {
        QTemporaryDir root;
        QVERIFY(write(root.filePath("test.desktop"), entry("/usr/bin/true %u")));
        auto metadata = readDesktopEntry(root.filePath("test.desktop"), "id",
                                         QProcessEnvironment::systemEnvironment());
        const auto plan = planLaunch(metadata, {root.filePath("two words#.txt")}, root.path(),
                                     QProcessEnvironment::systemEnvironment());
        QVERIFY(plan);
        QVERIFY(plan.arguments.first().contains("two%20words%23.txt"));
        QVERIFY(!planLaunch(metadata, {"/one", "/two"}, root.path(),
                            QProcessEnvironment::systemEnvironment()));
        QCOMPARE(planLaunch(metadata, {}, root.path(), QProcessEnvironment::systemEnvironment())
                     .arguments.size(),
                 0);
        metadata.tokens = {"/usr/bin/true", "trailing%"};
        QVERIFY(!planLaunch(metadata, {}, root.path(), QProcessEnvironment::systemEnvironment()));
    }
    void metadataBoundsAndDbusOnlyEntries()
    {
        QTemporaryDir root;
        const auto path = root.filePath("test.desktop");
        QVERIFY(write(path, entry("/usr/bin/true", QByteArray(512 * 1024, 'x'))));
        QVERIFY(!readDesktopEntry(path, "id", {}).unavailable.isEmpty());
        QVERIFY(write(path, entry({}, "DBusActivatable=true\n")));
        QVERIFY(readDesktopEntry(path, "id", {}).unavailable.contains("D-Bus"));
        QVERIFY(write(path, entry("/usr/bin/true", "DBusActivatable=true\n")));
        QVERIFY(readDesktopEntry(path, "id", {}).unavailable.isEmpty());
    }
    void bluetoothIsSearchableButNeverADocumentHandler()
    {
        QTemporaryDir root;
        const auto directory = root.filePath("apps");
        // A disk entry cannot replace the desktop's reserved control-panel identity.
        QVERIFY(write(directory + "/org.frankenstein.Bluetooth.desktop",
                      entry("/usr/bin/false %f", "Name=Impostor\nMimeType=text/plain;\n")));
        ApplicationCatalog catalog({directory}, root.path(), root.path(),
                                   QCoreApplication::applicationDirPath());
        catalog.setQuery("Bluetooth");
        QCOMPARE(catalog.count(), 1);
        QCOMPARE(catalog.item(0).value("id").toString(), QString("org.frankenstein.Bluetooth.desktop"));
        QVERIFY(catalog.item(0).value("launchable").toBool());
        QCOMPARE(catalog.builtInAction("org.frankenstein.Bluetooth.desktop"), QString("bluetooth"));
        QVERIFY(catalog.builtInAction("unknown.desktop").isEmpty());
        catalog.refresh();
        QTRY_VERIFY(catalog.ready());
        QCOMPARE(catalog.count(), 1);
        QCOMPARE(catalog.item(0).value("name").toString(), QString("Bluetooth"));
        QCOMPARE(catalog.builtInAction("org.frankenstein.Bluetooth.desktop"), QString("bluetooth"));
        const auto document = root.filePath("file.txt");
        QVERIFY(write(document, "document"));
        catalog.setDocument(document);
        catalog.setShowAll(true);
        QCOMPARE(catalog.count(), 0);
        QString error;
        QVERIFY(!catalog.rememberApplication("org.frankenstein.Bluetooth.desktop", document, error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!catalog.launchPlan("org.frankenstein.Bluetooth.desktop", {document}, {}));
    }
    void discoveryHonorsPrecedenceVisibilityAndSearch()
    {
        QTemporaryDir root;
        const auto user = root.filePath("user"), system = root.filePath("system");
        QVERIFY(
            write(user + "/hidden.desktop", "[Desktop Entry]\nType=Application\nHidden=true\n"));
        QVERIFY(write(system + "/hidden.desktop", entry("/usr/bin/true")));
        QVERIFY(
            write(system + "/tools/test.desktop",
                  entry("/usr/bin/true %f", "Keywords=needle;haystack;\nMimeType=text/plain;\n")));
        QVERIFY(write(system + "/nodisplay.desktop",
                      entry("/usr/bin/true %f", "NoDisplay=true\nMimeType=text/plain;\n")));
        QVERIFY(write(system + "/kde.desktop", entry("/usr/bin/true", "OnlyShowIn=KDE;\n")));
        ApplicationCatalog catalog({user, system}, root.path(), root.path(),
                                   QCoreApplication::applicationDirPath());
        catalog.refresh();
        QTRY_VERIFY(catalog.ready());
        QVERIFY(ids(catalog).contains("tools-test.desktop"));
        QVERIFY(!ids(catalog).contains("hidden.desktop"));
        QVERIFY(!ids(catalog).contains("nodisplay.desktop"));
        QVERIFY(!ids(catalog).contains("kde.desktop"));
        catalog.setQuery("HAYSTACK needle");
        QCOMPARE(ids(catalog), QStringList{"tools-test.desktop"});
        catalog.setQuery({});
        QVERIFY(write(root.filePath("document.txt"), "document"));
        catalog.setDocument(root.filePath("document.txt"));
        QVERIFY(ids(catalog).contains("nodisplay.desktop"));
    }
    void staleEntriesAreRejectedAndChoicesAreProfileLocal()
    {
        QTemporaryDir root;
        const auto directory = root.filePath("apps"), document = root.filePath("file.txt");
        QVERIFY(write(directory + "/test.desktop",
                      entry("/usr/bin/true %f", "MimeType=text/plain;\n")));
        QVERIFY(write(document, "text document"));
        ApplicationCatalog catalog({directory}, root.path(), root.path(),
                                   QCoreApplication::applicationDirPath());
        catalog.refresh();
        QTRY_VERIFY(catalog.ready());
        QString error;
        QVERIFY(catalog.rememberApplication("test.desktop", document, error));
        QCOMPARE(catalog.preferredApplication(document), QString("test.desktop"));
        ApplicationCatalog restored({directory}, root.path(), root.path(),
                                    QCoreApplication::applicationDirPath());
        restored.refresh();
        QTRY_VERIFY(restored.ready());
        QCOMPARE(restored.preferredApplication(document), QString("test.desktop"));
        QCOMPARE(restored.preferredApplication(directory + "/test.desktop"),
                 QString("org.frankenstein.Editor.desktop"));
        QVERIFY(write(directory + "/test.desktop", entry("/usr/bin/false %f")));
        QVERIFY(
            catalog.launchPlan("test.desktop", {document}, QProcessEnvironment::systemEnvironment())
                .error.contains("changed"));
    }
    void actualChildReceivesLiteralArgumentsAndPrivateEnvironment()
    {
        QTemporaryDir root;
        const auto probe = QCoreApplication::applicationDirPath() + "/application-probe";
        const auto entryPath = root.filePath("probe.desktop");
        QVERIFY(write(entryPath, entry('"' + probe.toUtf8() + "\" %F")));
        auto environment = QProcessEnvironment::systemEnvironment();
        for (const auto &key : {"DISPLAY", "XAUTHORITY", "WAYLAND_SOCKET", "DESKTOP_STARTUP_ID",
                                "XDG_ACTIVATION_TOKEN", "GTK_THEME"})
            environment.insert(key, "host-only-value");
        environment =
            nativeApplicationEnvironment(environment, "private-wayland", root.path(),
                                         QCoreApplication::applicationDirPath() + "/plugins");
        environment.insert("FRANKENSTEIN_PROBE_RESULT", root.filePath("result.json"));
        const auto metadata = readDesktopEntry(entryPath, "id", environment);
        const QStringList documents{root.filePath("$(touch NEVER); & literal.txt")};
        const auto plan = planLaunch(metadata, documents, root.path(), environment);
        QVERIFY2(bool(plan), qPrintable(plan.error));
        QProcess child;
        child.setProcessEnvironment(environment);
        child.setWorkingDirectory(plan.workingDirectory);
        child.start(plan.program, plan.arguments);
        QVERIFY(child.waitForFinished());
        QCOMPARE(child.exitCode(), 0);
        QFile result(root.filePath("result.json"));
        QVERIFY(result.open(QIODevice::ReadOnly));
        const auto object = QJsonDocument::fromJson(result.readAll()).object();
        QCOMPARE(object["arguments"].toArray(), QJsonArray::fromStringList(documents));
        QCOMPARE(object["directory"].toString(), root.path());
        const auto env = object["environment"].toObject();
        QCOMPARE(env["WAYLAND_DISPLAY"].toString(), QString("private-wayland"));
        QCOMPARE(env["XDG_CONFIG_HOME"].toString(), root.path() + "/config");
        QCOMPARE(env["QT_STYLE_OVERRIDE"].toString(), QString("Frankenstein"));
        QCOMPARE(env["GSETTINGS_BACKEND"].toString(), QString("keyfile"));
        QVERIFY(!environment.contains("GTK_THEME"));
        for (const auto &key : {"DISPLAY", "XAUTHORITY", "WAYLAND_SOCKET", "DESKTOP_STARTUP_ID",
                                "XDG_ACTIVATION_TOKEN"})
            QVERIFY(env[key].toString().isEmpty());
        QVERIFY(!QFileInfo(root.filePath("NEVER")).exists());
    }
};
QTEST_GUILESS_MAIN(ApplicationTests)
#include "application_tests.moc"
