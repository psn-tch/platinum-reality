#include "desktop/graphics_integration.hpp"
#include <QtTest>

class GraphicsIntegrationTests final : public QObject
{
    Q_OBJECT
  private slots:
    void aMissingEglDeviceUsesShmRatherThanAdvertisingAnUnusableDmabufGlobal()
    {
        int probes = 0;
        auto missingDevice = [&] { ++probes; return CompositorDmabufSupport{false, "no DRM device"}; };
        const auto choice = chooseCompositorGraphics("wayland", {}, true, missingDevice);
        QCOMPARE(probes, 1);
        QVERIFY(choice.automatic);
        QCOMPARE(choice.integration, QString("wayland-egl"));
        QCOMPARE(choice.reason, QString("no DRM device"));
    }
    void aWorkingEglDeviceRetainsAcceleratedClientBuffers()
    {
        const auto choice = chooseCompositorGraphics("wayland", {}, true,
            [] { return CompositorDmabufSupport{true, {}}; });
        QVERIFY(choice.automatic);
        QCOMPARE(choice.integration, QString("linux-dmabuf-v1"));
    }
    void explicitIntegrationChoicesAreNeverProbedOrOverwritten()
    {
        for (const auto *key : {"QT_WAYLAND_HARDWARE_INTEGRATION", "QT_WAYLAND_CLIENT_BUFFER_INTEGRATION"})
            for (const auto &value : {QString("linux-dmabuf-v1"), QString("wayland-egl"), QString()})
            {
                QProcessEnvironment environment;
                environment.insert(key, value);
                int probes = 0;
                const auto choice = chooseCompositorGraphics("wayland", environment, true, [&] {
                    ++probes; return CompositorDmabufSupport{};
                });
                QCOMPARE(probes, 0);
                QVERIFY(!choice.automatic);
                QCOMPARE(choice.integration, value);
            }
        QProcessEnvironment environment;
        environment.insert("QT_WAYLAND_HARDWARE_INTEGRATION", "wayland-egl");
        environment.insert("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "shm");
        QCOMPARE(chooseCompositorGraphics("wayland", environment, true, {}).integration, QString("wayland-egl"));
    }
    void otherPlatformsAndRendererChoicesNeverTouchEgl()
    {
        for (const auto &platform : {QString("offscreen"), QString("xcb"), QString("wayland")})
            for (const auto *key : {"QSG_RHI_BACKEND", "QT_QUICK_BACKEND"})
            {
                QProcessEnvironment environment;
                environment.insert(key, QString(key) == "QSG_RHI_BACKEND" ? "vulkan" : "software");
                const auto choice = chooseCompositorGraphics(platform, environment, true, {});
                QVERIFY(!choice.automatic && choice.integration.isEmpty());
            }
        QVERIFY(chooseCompositorGraphics("wayland", {}, false, {}).integration.isEmpty());
    }
    void anOffscreenPlatformHasNoInventedEglDevice()
    {
        QVERIFY(QGuiApplication::platformName() == "offscreen");
        const auto support = compositorDmabufSupport();
        QVERIFY(!support.available);
        QVERIFY(!support.reason.isEmpty());
    }
};
QTEST_MAIN(GraphicsIntegrationTests)
#include "graphics_integration_tests.moc"
