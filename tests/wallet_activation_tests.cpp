#include "desktop/wallet_activation.hpp"
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class WalletActivationTests final : public QObject
{
    Q_OBJECT
  private slots:
    void activationEnvironmentUsesOnlyThePrivateDisplayAndNormalSettings()
    {
        QProcessEnvironment environment;
        environment.insert("HOME", "/fixture/home");
        environment.insert("XDG_CONFIG_HOME", "/fixture/config");
        environment.insert("XDG_DATA_HOME", "/fixture/data");
        environment.insert("XDG_RUNTIME_DIR", "/fixture/private-runtime");
        environment.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=/fixture/private-runtime/bus");
        environment.insert("WAYLAND_DISPLAY", "native-desktop");
        environment.insert("QT_QPA_PLATFORM", "wayland");
        environment.insert("DISPLAY", ":17");
        environment.insert("XAUTHORITY", "/fixture/x11/authority");
        environment.insert("WAYLAND_SOCKET", "99");
        environment.insert("APPLICATION_API_TOKEN", "must not be copied");
        environment.insert("PLATINUM_WALLET_LOGIN", "1");
        const auto values = WalletActivation::activationEnvironment(environment);
        for (const auto *key : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_RUNTIME_DIR",
                                "DBUS_SESSION_BUS_ADDRESS", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM", "DISPLAY", "XAUTHORITY"})
            QCOMPARE(values.value(key), environment.value(key));
        QCOMPARE(values.value("WAYLAND_SOCKET"), QString());
        QVERIFY(!values.contains("APPLICATION_API_TOKEN"));
        QVERIFY(!values.contains("PLATINUM_WALLET_LOGIN"));
        QVERIFY(values.contains("QT_STYLE_OVERRIDE"));
        QVERIFY(values.value("QT_STYLE_OVERRIDE").isEmpty());
    }
    void unavailableBusDoesNotClaimWalletReadiness()
    {
        WalletActivation wallet(QDBusConnection(QString{}));
        QVERIFY(!wallet.ready());
        wallet.configure({});
        QTRY_VERIFY(!wallet.pending());
        QVERIFY(!wallet.ready());
        QVERIFY(!wallet.error().isEmpty());
        // Reconfiguration retires an older pending generation.
        wallet.configure({});
        wallet.configure({});
        QTRY_VERIFY(!wallet.pending());
        QVERIFY(!wallet.ready());
    }
    void anUnsetSystemDataPathStillFindsGtkMimeTypes()
    {
        // The prompt uses GTK/GIO, whose MIME lookup treats an exported empty
        // XDG_DATA_DIRS differently from an absent value. No display or bus needed.
        QTemporaryDir home;
        QProcessEnvironment input;
        input.insert("HOME", home.path());
        QProcessEnvironment activated;
        const auto values = WalletActivation::activationEnvironment(input);
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            activated.insert(it.key(), it.value());
        QProcess probe;
        probe.setProcessEnvironment(activated);
        probe.start("/usr/bin/python3", {"-I", "-c",
            "import gi; from gi.repository import Gio; print(Gio.content_type_guess('wallet-prompt.svg', None)[0])"});
        QVERIFY(probe.waitForFinished(5000));
        QCOMPARE(probe.exitCode(), 0);
        QCOMPARE(probe.readAllStandardOutput().trimmed(), QByteArray("image/svg+xml"));
        input.insert("XDG_DATA_DIRS", "/fixture/data:/usr/share");
        QCOMPARE(WalletActivation::activationEnvironment(input).value("XDG_DATA_DIRS"),
                 QString("/fixture/data:/usr/share"));
    }
};
QTEST_GUILESS_MAIN(WalletActivationTests)
#include "wallet_activation_tests.moc"
