#include "screenshot_service.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickView>
#include <QStandardPaths>

struct ScreenshotData
{
    QImage image;
    quint64 revision = 0;
};
namespace
{
class ScreenshotProvider final : public QQuickImageProvider
{
  public:
    explicit ScreenshotProvider(std::shared_ptr<ScreenshotData> data)
        : QQuickImageProvider(QQuickImageProvider::Image), m_data(std::move(data))
    {
    }
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override
    {
        if (id != QString::number(m_data->revision))
            return {};
        if (size)
            *size = m_data->image.size();
        return requested.isValid()
                   ? m_data->image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                   : m_data->image;
    }

  private:
    std::shared_ptr<ScreenshotData> m_data;
};
} // namespace
ScreenshotService::ScreenshotService(QQuickView *view, QObject *parent)
    : QObject(parent), m_view(view), m_data(std::make_shared<ScreenshotData>())
{
    view->engine()->addImageProvider("platinum-screenshot", new ScreenshotProvider(m_data));
}
ScreenshotService::~ScreenshotService() = default;
QUrl ScreenshotService::preview() const
{
    return m_data->image.isNull()
               ? QUrl()
               : QUrl("image://platinum-screenshot/" + QString::number(m_data->revision));
}
QSize ScreenshotService::imageSize() const
{
    return m_data->image.size();
}
bool ScreenshotService::fail(QString message)
{
    m_error = std::move(message);
    emit changed();
    return false;
}
bool ScreenshotService::capture()
{
    if (!m_view || !m_view->isVisible())
        return fail("The desktop is not available to capture.");
    const auto dpr = m_view->devicePixelRatio();
    if (m_view->width() * dpr * m_view->height() * dpr > 64 * 1024 * 1024)
        return fail("This desktop is larger than the supported screenshot size.");
    const auto image = m_view->grabWindow();
    if (image.isNull())
        return fail("The desktop could not be captured.");
    m_data->image = image;
    m_data->image.setDevicePixelRatio(1);
    ++m_data->revision;
    m_selection = m_data->image.rect();
    auto pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty())
        pictures = QDir::homePath() + "/Pictures";
    m_suggestedPath = pictures + "/Screenshots/Platinum Reality " +
                      QDateTime::currentDateTime().toString("yyyy-MM-dd HH.mm.ss.zzz") + ".png";
    m_error.clear();
    emit changed();
    return true;
}
bool ScreenshotService::select(int x, int y, int width, int height)
{
    if (m_data->image.isNull() || x < 0 || y < 0 || width <= 0 || height <= 0 ||
        qint64(x) + width > m_data->image.width() || qint64(y) + height > m_data->image.height())
        return false;
    m_selection = QRect(x, y, width, height);
    m_error.clear();
    emit changed();
    return true;
}
bool ScreenshotService::save(QString path)
{
    if (m_data->image.isNull() || m_selection.isEmpty())
        return fail("Take a screenshot first.");
    if (path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
    if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null) ||
        QFileInfo(path).suffix().toLower() != "png")
        return fail("Enter a full filename ending in .png.");
    const QFileInfo destination(path);
    if (destination.exists() || destination.isSymLink())
        return fail("A file already has that name. Choose another filename.");
    if (!QDir().mkpath(destination.absolutePath()))
        return fail("The screenshot folder could not be created.");
    // Exclusive creation also prevents replacing a file created after the
    // earlier existence check. Write screenshot contents only after chmod.
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail("The screenshot could not be saved: " + output.errorString());
    if (!output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        !m_data->image.copy(m_selection).save(&output, "PNG") || !output.flush())
    {
        const auto message = output.errorString();
        output.close();
        output.remove();
        return fail("The screenshot could not be saved: " + message);
    }
    m_error.clear();
    emit changed();
    return true;
}
void ScreenshotService::discard()
{
    m_data->image = {};
    m_selection = {};
    m_error.clear();
    emit changed();
}
