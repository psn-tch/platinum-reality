#pragma once
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QUrl>
#include <memory>
class QQuickView;
struct ScreenshotData;

class ScreenshotService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl preview READ preview NOTIFY changed)
    Q_PROPERTY(QSize imageSize READ imageSize NOTIFY changed)
    Q_PROPERTY(QRect selection READ selection NOTIFY changed)
    Q_PROPERTY(QString suggestedPath READ suggestedPath NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    explicit ScreenshotService(QQuickView *view, QObject *parent = nullptr);
    ~ScreenshotService() override;
    QUrl preview() const;
    QSize imageSize() const;
    QRect selection() const
    {
        return m_selection;
    }
    QString suggestedPath() const
    {
        return m_suggestedPath;
    }
    QString error() const
    {
        return m_error;
    }
    Q_INVOKABLE bool capture();
    Q_INVOKABLE bool select(int x, int y, int width, int height);
    Q_INVOKABLE bool save(QString path);
    Q_INVOKABLE void discard();
  signals:
    void changed();

  private:
    bool fail(QString message);
    QPointer<QQuickView> m_view;
    std::shared_ptr<ScreenshotData> m_data;
    QRect m_selection;
    QString m_suggestedPath, m_error;
};
