#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QClipboard>
#include <QDBusConnection>
#include <QMainWindow>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScreen>
#include <QWindow>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QStyle>
#include <memory>

// Disposable, owned native application. Its inspection endpoint belongs only
// on the private qualification bus, never on a desktop or production service.
class ProbeText final : public QPlainTextEdit
{
  public:
    int presses = 0;
    int keyPresses = 0, keyReleases = 0;
    Qt::KeyboardModifiers lastKeyModifiers;
    QPoint last;

  protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        ++keyPresses;
        lastKeyModifiers = event->modifiers();
        QPlainTextEdit::keyPressEvent(event);
    }
    void keyReleaseEvent(QKeyEvent *event) override
    {
        ++keyReleases;
        QPlainTextEdit::keyReleaseEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        ++presses;
        last = event->position().toPoint();
        QPlainTextEdit::mousePressEvent(event);
    }
};
class NativeProbe final : public QMainWindow
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.frankenstein.NativeProbe")
  public:
    NativeProbe()
    {
        resize(360, 240);
        if (qApp->arguments().contains("--frameless")) setWindowFlag(Qt::FramelessWindowHint);
        setWindowTitle(qApp->applicationName());
        text = new ProbeText;
        setCentralWidget(text);
        auto *file = menuBar()->addMenu("&File");
        connect(file->addAction("&Increment Probe"), &QAction::triggered, this,
                [this] { ++counter; });
        connect(file->addAction("&Close"), &QAction::triggered, this, &QWidget::close);
    }
  public slots:
    QString StyleName() const { return qApp->style()->objectName(); }
    // Read-only view of the output this client was told about.
    int ScreenWidth() const { return screen()->geometry().width(); }
    int ScreenHeight() const { return screen()->geometry().height(); }
    double ScreenPixelRatio() const { return screen()->devicePixelRatio(); }
    QString ScreenName() const { return screen()->name(); }
    QString Screens() const {
        QJsonArray result;
        for(auto *screen : QGuiApplication::screens()) {
            const auto rect=screen->geometry();
            result.append(QJsonObject{{"name",screen->name()},{"x",rect.x()},{"y",rect.y()},
                                      {"width",rect.width()},{"height",rect.height()},
                                      {"dpr",screen->devicePixelRatio()}});
        }
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    bool FullScreenOn(QString name) {
        for(auto *screen : QGuiApplication::screens()) if(screen->name()==name) {
            showNormal();
            windowHandle()->setScreen(screen);
            showFullScreen();
            return true;
        }
        return false;
    }
    void SetFullScreen(bool value) { value ? showFullScreen() : showNormal(); }
    // A minimize button: under X11, Qt sends WM_CHANGE_STATE IconicState (as Steam does).
    void Minimize() { showMinimized(); }
    // As X11 programs (SDL games) pick a monitor: move the window onto it, then go full screen.
    void MoveTo(int x, int y) { move(x, y); }
    bool IsFullScreen() const { return windowState() & Qt::WindowFullScreen; }
    int Width() const { return width(); }
    int Height() const { return height(); }
    bool LocalMenuVisible() const { return menuBar()->isVisible(); }
    void ShowFileMenu() { menuBar()->actions().first()->menu()->popup(mapToGlobal(QPoint(10,25))); }
    // A menu opened at a window-local point, e.g. near a monitor's edge.
    void ShowFileMenuAt(int x, int y) { menuBar()->actions().first()->menu()->popup(mapToGlobal(QPoint(x,y))); }
    void HideFileMenu() { menuBar()->actions().first()->menu()->hide(); }
    bool FileMenuVisible() const { return menuBar()->actions().first()->menu()->isVisible(); }
    void SetClipboard(QString value) { QApplication::clipboard()->setText(value); }
    // The primary selection: what middle-click pastes.
    void SetSelection(QString value) { QApplication::clipboard()->setText(value, QClipboard::Selection); }
    QString Selection() const { return QApplication::clipboard()->text(QClipboard::Selection); }
    // Ask to come forward (xdg-activation through Qt's xdg-shell) and ring the bell (xdg-system-bell).
    void RequestActivate() { activateWindow(); }
    // As when another application passes a token (Qt reads XDG_ACTIVATION_TOKEN on request).
    void RequestActivateWithToken(QString token) { qputenv("XDG_ACTIVATION_TOKEN", token.toLatin1()); activateWindow(); }
    void Beep() { QApplication::beep(); }
    QString Clipboard() const { return QApplication::clipboard()->text(); }
    QStringList Arguments() const { return qApp->arguments().mid(1); }
    int Counter() const
    {
        return counter;
    }
    int CloseRequests() const { return closeRequests; }
    QString Text() const
    {
        return text->toPlainText();
    }
    QString InputState() const
    {
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {"textFocus", text->hasFocus()}, {"activeWindow", isActiveWindow()},
            {"keyPresses", text->keyPresses}, {"keyReleases", text->keyReleases},
            {"lastKeyModifiers", int(text->lastKeyModifiers)}}).toJson(QJsonDocument::Compact));
    }
    int MousePresses() const
    {
        return text->presses;
    }
    int LastX() const
    {
        return text->last.x();
    }
    int LastY() const
    {
        return text->last.y();
    }
    void VetoClose(bool value)
    {
        veto = value;
    }
    void HideWithoutQuitting()
    {
        qApp->setQuitOnLastWindowClosed(false);
        hide();
    }
    void ShowAgain()
    {
        qApp->setQuitOnLastWindowClosed(true);
        show();
    }
    void SetTitle(QString title) { setWindowTitle(title); }
    void Finish()
    {
        veto = false;
        close();
        qApp->quit(); // Also finishes an owned probe with no visible windows.
    }

  private:
    void closeEvent(QCloseEvent *event) override
    {
        ++closeRequests;
        if (veto)
            event->ignore();
        else
            event->accept();
    }
    ProbeText *text;
    int counter = 0;
    int closeRequests = 0;
    bool veto = false;
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") != "1")
        return 2;
    const auto label = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString("A");
    app.setApplicationName("Native Probe " + label);
    app.setDesktopFileName("org.frankenstein.NativeProbe" + label);
    for (const auto &argument : app.arguments())
        if (argument.startsWith("--app-id=")) {
            app.setApplicationName(argument.mid(9));
            app.setDesktopFileName(argument.mid(9));
        }
    NativeProbe probe;
    probe.setWindowTitle("Native Probe " + label);
    probe.winId();
    for(const auto &argument : app.arguments()) if(argument.startsWith("--screen="))
        for(auto *screen : app.screens()) if(screen->name()==argument.mid(9))
            probe.windowHandle()->setScreen(screen);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService("org.frankenstein.NativeProbe" + label) ||
        !bus.registerObject("/Probe", &probe, QDBusConnection::ExportAllSlots))
        return 3;
    // Qualification must not leave fixture programs running after a failure.
    QTimer::singleShot(60000, &probe, &NativeProbe::Finish);
    // --fixed=WxH: a client that fixes its size (minimum = maximum), as games do.
    for(const auto &argument : app.arguments()) if(argument.startsWith("--fixed=")) {
        const auto size=argument.mid(8).split('x');
        if(size.size()==2) probe.setFixedSize(size[0].toInt(),size[1].toInt());
    }
    if(app.arguments().contains("--start-fullscreen")) probe.showFullScreen();
    else probe.show();
    // --leave-fullscreen-after=ms: the client then asks to leave full screen (Unity does).
    for(const auto &argument : app.arguments()) if(argument.startsWith("--leave-fullscreen-after="))
        QTimer::singleShot(argument.mid(25).toInt(), &probe, [&probe]{ probe.showNormal(); });
    std::unique_ptr<NativeProbe> second;
    if (app.arguments().contains("--two-windows"))
    {
        second = std::make_unique<NativeProbe>();
        second->setWindowTitle(app.applicationName() + " second");
        second->show();
    }
    return app.exec();
}
#include "native_probe.moc"
