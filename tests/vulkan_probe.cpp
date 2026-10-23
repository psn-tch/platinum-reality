#include <QDBusConnection>
#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTimer>
#include <QVulkanFunctions>
#include <QVulkanWindow>
#include <QtGui/qguiapplication_platform.h>
#include <cstdio>
#include <cstdlib>
#include <sys/prctl.h>
#if FRANKENSTEIN_PROBE_DRI3
#include <xcb/dri3.h>
#endif

// Owned qualification client. Its render pass clears two distinct regions via
// Vulkan commands; the compositor tests the presented pixels, not this counter.
static QElapsedTimer startupClock;
class VulkanProbe;
class ProbeRenderer final : public QVulkanWindowRenderer
{
  public:
    explicit ProbeRenderer(VulkanProbe *window) : m_window(window) {}
    void initResources() override;
    void initSwapChainResources() override;
    void startNextFrame() override;
  private:
    VulkanProbe *m_window;
    QVulkanDeviceFunctions *m_functions = nullptr;
};
class VulkanProbe final : public QVulkanWindow
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.frankenstein.VulkanProbe")
  public:
    int frames = 0;
    int phase = 0;
    int swapchains = 0;
    QVulkanWindowRenderer *createRenderer() override { return new ProbeRenderer(this); }
  public slots:
    int FrameCount() const { return frames; }
    int SwapchainCount() const { return swapchains; }
    int Width() const { return width(); }
    int Height() const { return height(); }
    int BufferWidth() const { return swapChainImageSize().width(); }
    int BufferHeight() const { return swapChainImageSize().height(); }
    QString Backend() const { return QGuiApplication::platformName(); }
    int ColorFormat() const { return colorFormat(); }
    // X11 only: the DRI3 version Xwayland offers this client. 1.4 adds the
    // syncobj path whose release points cannot outlive Xwayland.
    QString Dri3Version() const {
#if FRANKENSTEIN_PROBE_DRI3
        auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        if (!x11 || !x11->connection())
            return {};
        auto *reply = xcb_dri3_query_version_reply(
            x11->connection(), xcb_dri3_query_version(x11->connection(), 1, 4), nullptr);
        if (!reply)
            return {};
        const auto version = QString("%1.%2").arg(reply->major_version).arg(reply->minor_version);
        std::free(reply);
        return version;
#else
        return {};
#endif
    }
    QString Device() const {
        return physicalDeviceProperties() ? QString::fromUtf8(physicalDeviceProperties()->deviceName) : QString();
    }
    QString ApiVersion() const {
        if (!physicalDeviceProperties()) return {};
        const auto version = physicalDeviceProperties()->apiVersion;
        return QString("%1.%2.%3").arg(VK_VERSION_MAJOR(version)).arg(VK_VERSION_MINOR(version)).arg(VK_VERSION_PATCH(version));
    }
    void SetPhase(int value) { phase = value; requestUpdate(); }
    void SetFullScreen(bool value) { value ? showFullScreen() : showNormal(); }
    void Finish() { close(); }
};
void ProbeRenderer::initResources()
{
    m_functions = m_window->vulkanInstance()->deviceFunctions(m_window->device());
    qInfo().noquote() << "Vulkan device:" << m_window->Device() << "API" << m_window->ApiVersion()
                     << "QPA" << QGuiApplication::platformName() << "colour format" << m_window->colorFormat();
    qInfo()<<"Startup resources ready at"<<startupClock.elapsed()<<"ms";
}
void ProbeRenderer::initSwapChainResources() { ++m_window->swapchains; }
void ProbeRenderer::startNextFrame()
{
    const auto size = m_window->swapChainImageSize();
    VkClearValue clear[2]{};
    clear[0].color.float32[0] = 0.05f;
    clear[0].color.float32[1] = 0.1f;
    clear[0].color.float32[2] = 0.8f;
    clear[0].color.float32[3] = 1.0f;
    clear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = m_window->defaultRenderPass();
    pass.framebuffer = m_window->currentFramebuffer();
    pass.renderArea.extent = {uint32_t(size.width()), uint32_t(size.height())};
    pass.clearValueCount = 2;
    pass.pClearValues = clear;
    const auto buffer = m_window->currentCommandBuffer();
    m_functions->vkCmdBeginRenderPass(buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    VkClearAttachment left{};
    left.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    left.colorAttachment = 0;
    left.clearValue.color.float32[0] = m_window->phase ? 0.8f : 0.1f;
    left.clearValue.color.float32[1] = m_window->phase ? 0.1f : 0.8f;
    left.clearValue.color.float32[2] = 0.15f;
    left.clearValue.color.float32[3] = 1.0f;
    VkClearRect rectangle{};
    rectangle.rect.extent = {uint32_t(size.width() / 2), uint32_t(size.height())};
    rectangle.layerCount = 1;
    m_functions->vkCmdClearAttachments(buffer, 1, &left, 1, &rectangle);
    m_functions->vkCmdEndRenderPass(buffer);
    ++m_window->frames;
    if(m_window->frames==1)qInfo()<<"Startup first frame at"<<startupClock.elapsed()<<"ms";
    m_window->frameReady();
    m_window->requestUpdate();
}
int main(int argc, char **argv)
{
    startupClock.start();
    qInstallMessageHandler([](QtMsgType type,const QMessageLogContext &context,const QString &message) {
        const auto text=qFormatLogMessage(type,context,message).toUtf8();
        std::fprintf(stderr,"%s\n",text.constData());
        std::fflush(stderr);
    });
    QGuiApplication app(argc, argv);
    qInfo()<<"Startup QGuiApplication ready at"<<startupClock.elapsed()<<"ms";
    if (qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") != "1" ||
        (argc != 2 && !(argc == 3 && (QString::fromLocal8Bit(argv[2]) == "--sdr" ||
                                      QString::fromLocal8Bit(argv[2]).startsWith("--prefer=")))))
        return 2;
    // Diagnostic opt-in for this disposable fixture only, never desktop apps.
    if (qEnvironmentVariable("FRANKENSTEIN_VULKAN_DIAGNOSTICS") == "1")
        prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    const auto label = QString::fromLocal8Bit(argv[1]);
    app.setApplicationName("Vulkan Probe " + label);
    app.setDesktopFileName("org.frankenstein.VulkanProbe" + label);
    QVulkanInstance instance;
    instance.setApiVersion(QVersionNumber(1, 1, 0));
    if (!instance.create()) {
        qCritical() << "Vulkan instance creation failed:" << instance.errorCode();
        return 4;
    }
    qInfo()<<"Startup Vulkan instance ready at"<<startupClock.elapsed()<<"ms";
    VulkanProbe window;
    window.setVulkanInstance(&instance);
    const auto option = argc == 3 ? QString::fromLocal8Bit(argv[2]) : QString();
    if(option == "--sdr")
        window.setPreferredColorFormats({VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM});
    else if(option.startsWith("--prefer="))
        window.setPreferredColorFormats({VkFormat(option.mid(9).toInt())});
    window.setSampleCount(1);
    window.resize(360, 240);
    window.setTitle(app.applicationName());
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService("org.frankenstein.VulkanProbe" + label) ||
        !bus.registerObject("/Probe", &window, QDBusConnection::ExportAllSlots))
        return 3;
    QTimer::singleShot(60000, &window, &VulkanProbe::Finish);
    window.show();
    qInfo()<<"Startup show returned at"<<startupClock.elapsed()<<"ms";
    return app.exec();
}
#include "vulkan_probe.moc"
