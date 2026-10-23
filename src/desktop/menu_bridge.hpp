#pragma once
#include <QObject>
struct wl_global;
class QWaylandCompositor;
class QWaylandSurface;

class NativeMenuBridge final : public QObject
{
    Q_OBJECT
  public:
    explicit NativeMenuBridge(QWaylandCompositor *, QObject *parent = nullptr);
    ~NativeMenuBridge() override;
  signals:
    void endpointChanged(QWaylandSurface *, QString service, QString path);

  private:
    wl_global *m_global = nullptr;
};
