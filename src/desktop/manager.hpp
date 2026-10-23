#pragma once
#include "interface_sounds.hpp"
#include "battery_status.hpp"
#include "policy/transition.hpp"
#include "window_presentation.hpp"
#include "display_layout.hpp"
#include "power_control.hpp"
#include <QAbstractListModel>
#include <QPointer>
#include <QProcess>
#include <QQuickView>
#include <QSet>
#include <QWaylandQuickCompositor>
#include <QWaylandXdgShell>
class MenuBroker;
class DesktopItems;
class ApplicationCatalog;
class AppearanceSettings;
class QWaylandSurface;
class QWaylandClient;
class X11Bridge;
class ApplicationAttention;
class WalletActivation;
class PointerConstraints;
class NotificationServer;
class StatusItems;
class SessionControl;
class IdleInhibition;
struct wl_protocol_logger;

class ManagedWindow final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qulonglong id READ id CONSTANT)
    Q_PROPERTY(int x READ x NOTIFY changed)
    Q_PROPERTY(int y READ y NOTIFY changed)
    Q_PROPERTY(int width READ width NOTIFY changed)
    Q_PROPERTY(int height READ height NOTIFY changed)
    Q_PROPERTY(int layer READ layer NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(bool shown READ shown NOTIFY changed)
    Q_PROPERTY(bool collapsed READ collapsed NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool canShade READ canShade NOTIFY changed)
    Q_PROPERTY(bool modal READ modal NOTIFY changed)
    Q_PROPERTY(bool canResize READ canResize NOTIFY changed)
    Q_PROPERTY(bool canZoom READ canZoom NOTIFY changed)
    Q_PROPERTY(bool hasCloseBox READ hasCloseBox NOTIFY changed)
    Q_PROPERTY(bool serverDecorated READ serverDecorated NOTIFY changed)
    // Where a self-decorated window's visible edge lies in its surface (the shadow it draws
    // around itself): the desktop places that edge, not the shadow, at the window position.
    Q_PROPERTY(QPoint contentOffset READ contentOffset NOTIFY changed)
    Q_PROPERTY(bool fullScreen READ isFullScreen NOTIFY changed)
    Q_PROPERTY(QRect fullScreenRect READ fullScreenRect NOTIFY changed)
    Q_PROPERTY(QObject *shell READ shell CONSTANT)
  public:
    explicit ManagedWindow(QObject *parent = nullptr) : QObject(parent) {}
    qulonglong id() const
    {
        return identity.value;
    }
    int x() const
    {
        return geometry.position.x;
    }
    int y() const
    {
        return geometry.position.y;
    }
    int width() const
    {
        return int(geometry.observed_size.width);
    }
    int height() const
    {
        return int(geometry.observed_size.height);
    }
    int layer() const
    {
        return z;
    }
    QString title() const
    {
        return top ? top->title() : QString();
    }
    bool shown() const
    {
        return visible;
    }
    bool collapsed() const
    {
        return shaded;
    }
    bool active() const
    {
        return selected;
    }
    bool canShade() const
    {
        return shadeAllowed;
    }
    bool modal() const
    {
        return top && top->isModal();
    }
    bool canResize() const
    {
        return top && windowPresentation(modal(), top->minSize(), top->maxSize()).resize;
    }
    bool canZoom() const
    {
        return top && windowPresentation(modal(), top->minSize(), top->maxSize()).zoom;
    }
    bool serverDecorated() const
    {
        return top && (classicBrowserFrame ||
                       top->decorationMode() == QWaylandXdgToplevel::ServerSideDecoration);
    }
    QPoint contentOffset() const
    {
        return xdg && !serverDecorated() ? xdg->windowGeometry().topLeft() : QPoint();
    }
    bool isFullScreen() const
    {
        return fullScreen;
    }
    QRect fullScreenRect() const { return fullScreenGeometry; }
    bool hasCloseBox() const
    {
        return serverDecorated() && !modal();
    }
    QObject *shell() const
    {
        return xdg;
    }
    frankenstein::policy::WindowId identity;
    frankenstein::policy::ContentGeometry geometry;
    QPointer<QWaylandXdgToplevel> top;
    QPointer<QWaylandXdgSurface> xdg;
    QWaylandClient *clientIdentity = nullptr; // comparison only, never dereferenced
    bool viaX11Bridge = false;
    bool visible = true, shaded = false, selected = false, shadeAllowed = false;
    bool classicBrowserFrame = false;
    int z = 0;
    QSize userSize;
    bool zoomed = false;
    // A client-requested full screen: it covers the desktop without chrome and
    // returns to restoreSize when the client leaves full screen.
    bool fullScreen = false;
    QRect fullScreenGeometry;
    bool restoreNeedsRescue = false;
    QSize restoreSize;
    bool configuredActive = false;
    bool shadeInMotion = false;
  signals:
    void changed();
};

class NativeManager final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString activeApplication READ activeApplication NOTIFY contextChanged)
    Q_PROPERTY(QVariantList applications READ applications NOTIFY contextChanged)
    Q_PROPERTY(QString workspace READ workspace CONSTANT)
    Q_PROPERTY(bool menuOpen READ menuOpen NOTIFY contextChanged)
    Q_PROPERTY(QObject *menuBroker READ menuBroker CONSTANT)
    Q_PROPERTY(qreal hostScale READ hostScale NOTIFY hostScaleChanged)
    Q_PROPERTY(QRect primaryDisplayRect READ primaryDisplayRect NOTIFY displayLayoutChanged)
    // Monitors control panel: {name, x, y, width, height, scale, transform, primary, internal}.
    Q_PROPERTY(QVariantList displayOutputs READ displayOutputs NOTIFY displayLayoutChanged)
    // A published topology (the extended display session) and the supervisor's control socket.
    Q_PROPERTY(bool displayArrangementAvailable READ displayArrangementAvailable NOTIFY displayLayoutChanged)
    Q_PROPERTY(bool displayRequestBusy READ displayRequestBusy NOTIFY displayRequestChanged)
    // Monitors control panel: every connected display shows the desktop, from the next login.
    // The session supervisor reads <profile>/displays.json; off by default.
    Q_PROPERTY(bool useAllDisplays READ useAllDisplays NOTIFY useAllDisplaysChanged)
    Q_PROPERTY(QObject *desktopItems READ desktopItems CONSTANT)
    Q_PROPERTY(bool desktopActive READ desktopActive NOTIFY contextChanged)
    Q_PROPERTY(bool shellModal READ shellModal NOTIFY contextChanged)
    Q_PROPERTY(QObject *applicationCatalog READ applicationCatalog CONSTANT)
    Q_PROPERTY(QObject *appearance READ appearance CONSTANT)
    Q_PROPERTY(QObject *screenshots READ screenshots CONSTANT)
    Q_PROPERTY(QObject *systemControls READ systemControls CONSTANT)
    Q_PROPERTY(QObject *audio READ audio CONSTANT)
    // BlueZ on the host's system bus (docs/bluetooth.md); unavailable without a compositor.
    Q_PROPERTY(QObject *bluetooth READ bluetooth CONSTANT)
    // Wi-Fi networks through iwd on the host's system bus (docs/wifi.md); as Bluetooth.
    Q_PROPERTY(QObject *wifi READ wifi CONSTANT)
    // Special ▸ Sleep, Restart and Shut Down: only in the whole session (power_control.hpp).
    Q_PROPERTY(bool powerAvailable READ powerAvailable CONSTANT)
    // Alert sounds (docs/alert-sounds.md); silent outside a real session.
    Q_PROPERTY(QObject *alertSounds READ alertSounds CONSTANT)
    Q_PROPERTY(QObject *interfaceSounds READ interfaceSounds CONSTANT)
    Q_PROPERTY(QObject *battery READ battery CONSTANT)
    // The user's own assets (icons/, sounds/), e.g. imported from their Mac OS 9; never bundled.
    Q_PROPERTY(QString personalAssets READ personalAssets CONSTANT)
    Q_PROPERTY(QObject *clipboard READ clipboard CONSTANT)
    // One of the desktop's own menus is open (set by Desktop.qml): a key closes it.
    Q_PROPERTY(bool shellMenuOpen MEMBER m_shellMenuOpen)
    Q_PROPERTY(bool nativeDragging READ nativeDragging NOTIFY nativeDragChanged)
    Q_PROPERTY(QPoint nativeDragPosition READ nativeDragPosition NOTIFY nativeDragChanged)
    Q_PROPERTY(bool nativeDragCopy READ nativeDragCopy NOTIFY nativeDragChanged)
    Q_PROPERTY(bool nativeDragOnDesktop READ nativeDragOnDesktop NOTIFY nativeDragChanged)
    Q_PROPERTY(QObject *nativeDragIcon READ nativeDragIcon NOTIFY nativeDragChanged)
    Q_PROPERTY(qulonglong desktopDropWindow READ desktopDropWindow NOTIFY desktopDropTargetChanged)
    Q_PROPERTY(QVariantList recentApplications READ recentApplications NOTIFY recentApplicationsChanged)
    Q_PROPERTY(bool pointerCaptured READ pointerCaptured NOTIFY pointerCaptureChanged)
    // Session-bus services for applications; null without a compositor (shell previews, tests).
    Q_PROPERTY(QObject *notifications READ notifications CONSTANT)
    Q_PROPERTY(QObject *statusItems READ statusItems CONSTANT)
    // An application asked to keep the session awake (video, games).
    Q_PROPERTY(bool idleInhibited READ idleInhibited NOTIFY idleInhibitionChanged)
  public:
    NativeManager(QWaylandQuickCompositor *compositor, QQuickView *view, QString workspace,
                  QString profile, QString stateFile, QStringList applicationDirectories = {},
                  QObject *parent = nullptr);
    ~NativeManager() override;
    bool receiveDesktopDrop(uint receiverPid, QStringList paths, QString token);
    qulonglong desktopDropWindow() const
    {
        return m_desktopDropWindow;
    }
    Q_INVOKABLE void updateDesktopDragTarget(int x, int y);
    Q_INVOKABLE void finishDesktopDrag(int x, int y, bool copy);
    bool nativeDragging() const
    {
        return m_nativeDragging;
    }
    QPoint nativeDragPosition() const
    {
        return m_nativeDragPosition;
    }
    bool nativeDragCopy() const
    {
        return m_nativeDragCopy;
    }
    bool nativeDragOnDesktop() const
    {
        return m_nativeDragOnDesktop;
    }
    QObject *nativeDragIcon() const;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString activeApplication() const;
    QVariantList applications() const;
    QString workspace() const
    {
        return m_workspace;
    }
    bool menuOpen() const
    {
        return bool(m_state.menu);
    }
    QObject *menuBroker() const;
    QObject *desktopItems() const;
    QObject *applicationCatalog() const;
    QObject *appearance() const;
    QObject *screenshots() const;
    QObject *systemControls() const { return m_systemControls; }
    QObject *audio() const { return m_audio; }
    QObject *bluetooth() const { return m_bluetooth; }
    QObject *wifi() const { return m_wifi; }
    QObject *alertSounds() const { return m_alertSounds; }
    QObject *interfaceSounds() const { return m_interfaceSounds; }
    QObject *battery() const { return m_battery; }
    QString personalAssets() const { return m_personalAssets; }
    QObject *clipboard() const { return m_clipboard; }
    bool readableDocument(const QString &path) const;
    Q_INVOKABLE void requestOpenDocument(QString path, bool chooseApplication = false);
    Q_INVOKABLE void launchApplication(QString id, QString document = {}, bool remember = false);
    bool desktopActive() const
    {
        return m_state.active.desktop;
    }
    Q_INVOKABLE bool activateDesktop();
    bool shellModal() const
    {
        return !m_shellModals.isEmpty();
    }
    Q_INVOKABLE qulonglong beginShellModal();
    Q_INVOKABLE void endShellModal(qulonglong token);
    qreal hostScale() const
    {
        return m_view->devicePixelRatio();
    }
    void setMenuEndpoint(QWaylandSurface *, QString service, QString path);
    Q_INVOKABLE void activate(qulonglong id, bool expand = false);
    Q_INVOKABLE void contextClickWindow(qulonglong id, QPointF scenePoint);
    Q_INVOKABLE void activateApplication(qulonglong id);
    Q_INVOKABLE void shade(qulonglong id);
    Q_INVOKABLE void setShadeInMotion(qulonglong id, bool running);
    Q_INVOKABLE void moveWindow(qulonglong id, int x, int y);
    Q_INVOKABLE void resizeWindow(qulonglong id, int width, int height);
    Q_INVOKABLE void zoom(qulonglong id);
    Q_INVOKABLE void closeWindow(qulonglong id);
    Q_INVOKABLE void hideApplication();
    Q_INVOKABLE void hideOtherApplications();
    Q_INVOKABLE void showAllApplications();
    Q_INVOKABLE void requestQuitActiveApplication();
    Q_INVOKABLE void beginMenu();
    Q_INVOKABLE void endMenu();
    Q_INVOKABLE void launchFinder(QString path = {});
    Q_INVOKABLE void showFinder();
    Q_INVOKABLE void launchTerminal();
    Q_INVOKABLE void launchPuzzle();
    Q_INVOKABLE void launchTrash();
    // A trusted .desktop launcher on the desktop (the user's own and executable).
    void launchDesktopFile(const QString &path);
    Q_INVOKABLE void quitSession();
    // Restart and Shut Down end the session as Log Out does, then ask logind.
    Q_INVOKABLE void sleepComputer();
    Q_INVOKABLE void restartComputer();
    Q_INVOKABLE void shutDownComputer();
    bool powerAvailable() const;
    Q_INVOKABLE QVariantMap snapshot() const;
    // Window menu: the active application's windows, by title (Finder's when the desktop is active).
    Q_INVOKABLE QVariantList windowMenu() const;
    Q_INVOKABLE QString aboutThisComputer() const;
    // Games and X11 programs can lock or confine the pointer (relative-pointer/pointer-constraints).
    Q_INVOKABLE bool pointerCaptureAvailable() const;
    bool pointerCaptured() const;
    // The release chord: free a locked or confined pointer until the next click in a window.
    Q_INVOKABLE void releasePointerCapture();
    QObject *notifications() const;
    QObject *statusItems() const;
    Q_INVOKABLE bool idleInhibitAvailable() const;
    bool idleInhibited() const;
    QVariantList recentApplications() const { return m_recentApplications; }
    void attachShell(QWaylandXdgShell *shell);
    void setX11Bridge(X11Bridge *bridge);
    void initializeWallet();
    // Only a validated host topology may be applied. This changes shell policy,
    // never physical outputs; the session controller owns hardware transactions.
    void setDisplayLayout(const DisplayLayout &layout);
    QRect primaryDisplayRect() const { return effectiveDisplayLayout().primaryRect(); }
    DisplayLayout effectiveDisplayLayout() const;
    QVariantList displayOutputs() const;
    bool displayArrangementAvailable() const;
    bool displayRequestBusy() const;
    // Asks the session supervisor to make a connected display the main one. The desktop moves
    // when the supervisor publishes the new topology, not when this returns.
    Q_INVOKABLE bool requestPrimaryDisplay(const QString &name);
    bool useAllDisplays() const { return m_useAllDisplays; }
    // Saved for the next login; the current session keeps its displays.
    Q_INVOKABLE bool setUseAllDisplays(bool all);
    // Launched applications use the user's own settings folders (~/.config and so on) and the
    // normal Firefox profile, instead of the private session profile. Off by default; the
    // installed launcher turns it on.
    void setNormalApplicationSettings(bool normal) { m_normalApplicationSettings = normal; }
    bool normalApplicationSettings() const { return m_normalApplicationSettings; }
    const frankenstein::policy::State &policyState() const
    {
        return m_state;
    }
  signals:
    void hostScaleChanged();
    void displayLayoutChanged();
    void displayRequestChanged();
    void useAllDisplaysChanged();
    void contextChanged();
    void focusWindow(qulonglong id);
    void focusDesktop();
    void shellMenuKeyPressed();
    void errorOccurred(QString message);
    void openWithRequested(QString document);
    void controlPanelRequested(QString action);
    void nativeDragChanged();
    void desktopDropTargetChanged();
    void recentApplicationsChanged();
    void pointerCaptureChanged();
    void idleInhibitionChanged();

  private:
    bool eventFilter(QObject *, QEvent *) override;
    void forgetClient(QWaylandClient *);
    void removeUnclaimedApplication(frankenstein::policy::ApplicationId);
    QString applicationDisplayName(const QString &key, const QString &title) const;
    void createWindow(QWaylandXdgToplevel *, QWaylandXdgSurface *);
    void retireWindow(ManagedWindow *);
    void dispatch(frankenstein::policy::Event);
    void synchronize();
    void setFullScreen(ManagedWindow *window, bool fullScreen, QWaylandOutput *requested = nullptr);
    bool oversizedFixedWindow(const ManagedWindow *window) const;
    QRect requestedScreen(QWaylandOutput *output) const;
    // The live output presenting that screen rectangle, else the default output.
    QWaylandOutput *outputForScreen(const QRect &screen) const;
    QRect windowScreen(const ManagedWindow *window) const;
    void refitFullScreens();
    void rescueRestoredWindow(ManagedWindow *window);
    QList<QWaylandXdgToplevel::State> configureStates(const ManagedWindow *window) const;
    ManagedWindow *find(qulonglong id) const;
    bool launch(QString program, QStringList arguments, QString directory = {},
                bool existingApplicationSettings = false);
    void drainDocumentRequests();
    void queueClientFlush();
    void startDropReceiver();
    bool routeNativeDrag(QPointF position, Qt::KeyboardModifiers modifiers);
    ManagedWindow *desktopDropWindowAt(QPointF point, QPointF *local = nullptr) const;
    QWaylandQuickCompositor *m_compositor;
    QQuickView *m_view;
    std::optional<DisplayLayout> m_displayLayout;
    SessionControl *m_sessionControl = nullptr;
    PowerControl *m_power = nullptr;
    std::optional<PowerControl::Action> m_pendingPower;
    void endSession();
    bool m_normalApplicationSettings = false;
    PointerConstraints *m_pointerConstraints = nullptr;
    NotificationServer *m_notifications = nullptr;
    StatusItems *m_statusItems = nullptr;
    IdleInhibition *m_idle = nullptr;
    QString m_workspace, m_profile, m_stateFile;
    bool m_useAllDisplays = false;
    frankenstein::policy::State m_state;
    QList<ManagedWindow *> m_windows;
    QMap<QString, frankenstein::policy::ApplicationId> m_applicationIds;
    QMap<qulonglong, QString> m_applicationNames;
    QMap<QWaylandClient *, QSet<qulonglong>> m_clientApplications;
    QMap<QWaylandSurface *, QPair<QString, QString>> m_menuEndpoints;
    MenuBroker *m_broker;
    DesktopItems *m_desktopItems;
    ApplicationCatalog *m_catalog;
    AppearanceSettings *m_appearance;
    QObject *m_screenshots;
    QObject *m_systemControls;
    QObject *m_audio;
    QObject *m_bluetooth;
    QObject *m_wifi;
    QObject *m_alertSounds;
    InterfaceSounds *m_interfaceSounds;
    BatteryStatus *m_battery;
    QString m_personalAssets;
    QObject *m_clipboard = nullptr;
    // Applications that asked to come forward without the right (Mac OS 9's ◆), until activated.
    QSet<quint64> m_wantsAttention;
    ApplicationAttention *m_attention = nullptr;
    ManagedWindow *windowForSurface(QWaylandSurface *surface) const;
    bool m_shellMenuOpen = false;
    int m_swallowedMenuKey = 0; // the closing key, whose release is swallowed too
    struct DocumentRequest
    {
        QString path;
        bool chooseApplication;
    };
    QList<DocumentRequest> m_pendingDocuments;
    bool m_documentsQueued = false;
    frankenstein::policy::ApplicationId m_desktopApplication;
    qulonglong m_nextApp = 0, m_nextWindow = 0, m_nextTracking = 0;
    bool m_quitting = false, m_allowExit = false;
    bool m_flushQueued = false;
    bool m_synchronizationDeferred = false;
    QSet<qulonglong> m_shellModals;
    qulonglong m_nextShellModal = 0;
    QProcess *m_dropProcess = nullptr;
    X11Bridge *m_x11 = nullptr; // Owner outlives manager and renderer retirement.
    WalletActivation *m_wallet = nullptr;
    bool m_walletRequested = false;
    void configureWalletEnvironment();
    void drainPendingLaunches();
    struct PendingLaunch {
        QString program;
        QStringList arguments;
        QString directory;
        bool existingSettings;
    };
    QList<PendingLaunch> m_pendingLaunches;
    // Tracks buffer attachment/commit lifecycle in production; bounded data-
    // device diagnostics are enabled only by FRANKENSTEIN_NATIVE_TEST.
    wl_protocol_logger *m_dropTrace = nullptr;
    int m_dropTraceMessages = 0;
    bool m_retiringDropReceiver = false;
    QPointer<QWaylandSurface> m_dropSurface;
    QPointer<QWaylandSurface> m_dragOrigin;
    QPoint m_nativeDragPosition, m_pendingDropPosition;
    bool m_nativeDragging = false, m_nativeDragCopy = false;
    bool m_nativeDragOnDesktop = false;
    bool m_pendingDesktopDrop = false, m_pendingDropCopy = false;
    qint64 m_pendingDropSourcePid = 0;
    qulonglong m_dropGeneration = 0;
    qulonglong m_desktopDropWindow = 0;
    QVariantList m_recentApplications; // Apple menu > Recent Applications, newest first
    void rememberRecentApplication(const QString &id);
};
