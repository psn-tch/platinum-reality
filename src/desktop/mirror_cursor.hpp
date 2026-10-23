#pragma once
#include <QCursor>
#include <QQuickItem>
#include <QRectF>
#include <memory>

class QQuickWindow;
class QWaylandCompositor;

// Optional cursor presentation on a fitted copy of a canonical desktop. It consumes no input
// and creates no pointer constraint. Bitmap/hidden client cursors are exact; shell cursor shapes
// currently use an original arrow fallback. Not a production Mirror controller.
class MirrorCursor final : public QQuickItem
{
    Q_OBJECT
  public:
    MirrorCursor(QWaylandCompositor *compositor, QQuickWindow *window, QQuickItem *parent = nullptr);
    ~MirrorCursor() override;
    void setMapping(QRectF source, QRectF picture);
    QCursor presentedCursor() const;
    bool cursorPresented() const;
    QRectF cursorSceneRect() const;

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    struct State;
    std::unique_ptr<State> m_state;
};
