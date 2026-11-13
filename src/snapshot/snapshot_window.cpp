#include "snapshot_window.hpp"
#include "finder/platinum_style.hpp"
#include <QAction>
#include <QCamera>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageCapture>
#include <QImageWriter>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMenuBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

SnapshotWindow::SnapshotWindow(QDir folder, QWidget *parent)
    : QMainWindow(parent), m_folder(std::move(folder)), m_devices(new QMediaDevices(this)),
      m_session(new QMediaCaptureSession(this)), m_capture(new QImageCapture(this))
{
    setWindowTitle("Snapshot");
    auto *body = new QWidget;
    setCentralWidget(body);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    // The picture: the camera's, or why there isn't one, in a sunken well.
    auto *well = new QFrame;
    well->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    well->setLineWidth(2);
    auto *wellLayout = new QVBoxLayout(well);
    wellLayout->setContentsMargins(2, 2, 2, 2);
    m_screen = new QStackedWidget;
    m_screen->setFixedSize(480, 360);
    m_preview = new QVideoWidget;
    m_preview->setObjectName("snapshot-preview");
    m_message = new QLabel;
    m_message->setObjectName("snapshot-message");
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_message->setTextFormat(Qt::RichText);
    m_message->setAutoFillBackground(true);
    m_message->setStyleSheet("background: #444; color: white; padding: 24px;");
    m_screen->addWidget(m_preview);
    m_screen->addWidget(m_message);
    wellLayout->addWidget(m_screen);
    layout->addWidget(well, 0, Qt::AlignHCenter);
    // A white flash over the picture as it is taken.
    m_flash = new QLabel(m_screen);
    m_flash->setAutoFillBackground(true);
    m_flash->setStyleSheet("background: white;");
    m_flash->hide();

    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    m_thumbnail = new QLabel;
    m_thumbnail->setObjectName("snapshot-thumbnail");
    m_thumbnail->setFixedSize(64, 48);
    m_thumbnail->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    m_thumbnail->setAlignment(Qt::AlignCenter);
    row->addWidget(m_thumbnail);
    auto *column = new QVBoxLayout;
    m_choice = new QComboBox;
    m_choice->setObjectName("snapshot-camera");
    m_status = new QLabel;
    m_status->setObjectName("snapshot-status");
    column->addWidget(m_choice);
    column->addWidget(m_status);
    row->addLayout(column, 1);
    m_show = new QPushButton("Show Pictures");
    m_show->setObjectName("snapshot-show");
    m_take = new QPushButton("Take Picture");
    m_take->setObjectName("snapshot-take");
    m_take->setDefault(true);
    m_take->setMinimumWidth(120);
    row->addWidget(m_show);
    row->addWidget(m_take);
    layout->addLayout(row);

    auto *file = menuBar()->addMenu("File");
    auto *take = file->addAction("Take Picture", this, &SnapshotWindow::takePicture);
    setClassicShortcuts(take, QKeySequence(Qt::CTRL | Qt::Key_T));
    file->addAction("Show Pictures", this, [this] {
        m_folder.mkpath(".");
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder.absolutePath()));
    });
    file->addSeparator();
    auto *quit = file->addAction("Quit Snapshot");
    setClassicShortcuts(quit, QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    m_session->setVideoOutput(m_preview);
    m_session->setImageCapture(m_capture);
    connect(m_take, &QPushButton::clicked, this, &SnapshotWindow::takePicture);
    connect(m_show, &QPushButton::clicked, this, [this] {
        m_folder.mkpath(".");
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder.absolutePath()));
    });
    connect(m_choice, &QComboBox::activated, this, [this](int index) {
        if (index >= 0 && index < m_cameras.size())
            useCamera(m_cameras[index]);
    });
    connect(m_capture, &QImageCapture::readyForCaptureChanged, this,
            [this](bool ready) { m_take->setEnabled(ready); });
    connect(m_capture, &QImageCapture::imageCaptured, this, [this](int, const QImage &image) {
        const auto path = savePicture(image);
        m_status->setText(path.isEmpty() ? "The picture could not be saved." : "Saved “" + QFileInfo(path).fileName() + "”.");
    });
    connect(m_capture, &QImageCapture::errorOccurred, this,
            [this](int, QImageCapture::Error, const QString &error) { m_status->setText("No picture: " + error); });
    // Cameras come and go: plugged in, unplugged, or switched off in the Control Strip.
    connect(m_devices, &QMediaDevices::videoInputsChanged, this, &SnapshotWindow::refreshCameras);
    refreshCameras();
    adjustSize();
    setFixedSize(sizeHint());
}

SnapshotWindow::~SnapshotWindow()
{
    if (m_camera)
        m_camera->stop();
}

QString SnapshotWindow::nameFor(const QDateTime &when)
{
    return "Snapshot " + when.toString("yyyy-MM-dd 'at' HH.mm.ss") + ".jpg";
}

QString SnapshotWindow::savePicture(const QImage &image)
{
    if (image.isNull() || !m_folder.mkpath("."))
        return {};
    auto path = m_folder.filePath(nameFor(QDateTime::currentDateTime()));
    // Two in the same second: "… 2.jpg", never over an earlier picture.
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = m_folder.filePath(nameFor(QDateTime::currentDateTime()).chopped(4) + " " + QString::number(n) + ".jpg");
    QImageWriter writer(path, "jpeg");
    writer.setQuality(92);
    if (!writer.write(image))
        return {};
    m_lastPicture = path;
    showThumbnail(path);
    emit pictureSaved(path);
    return path;
}

void SnapshotWindow::takePicture()
{
    if (!m_camera || !m_capture->isReadyForCapture())
        return;
    flash();
    m_capture->capture();
}

void SnapshotWindow::flash()
{
    m_flash->setGeometry(m_screen->rect());
    m_flash->raise();
    m_flash->show();
    QTimer::singleShot(120, m_flash, &QWidget::hide);
}

void SnapshotWindow::showThumbnail(const QString &path)
{
    const QImage picture(path);
    m_thumbnail->setPixmap(QPixmap::fromImage(picture.scaled(m_thumbnail->size() - QSize(4, 4), Qt::KeepAspectRatio,
                                                             Qt::SmoothTransformation)));
    m_thumbnail->setToolTip(QFileInfo(path).fileName());
}

void SnapshotWindow::showMessage(const QString &title, const QString &detail)
{
    m_message->setText("<p style='font-size:15px'><b>" + title.toHtmlEscaped() + "</b></p><p>" + detail.toHtmlEscaped() + "</p>");
    m_screen->setCurrentWidget(m_message);
}

void SnapshotWindow::refreshCameras()
{
    const auto previous = m_camera ? m_camera->cameraDevice().id() : QByteArray();
    m_cameras = QMediaDevices::videoInputs();
    m_choice->clear();
    for (const auto &device : m_cameras)
        m_choice->addItem(device.description());
    m_choice->setVisible(m_cameras.size() > 1);
    if (m_cameras.isEmpty())
    {
        if (m_camera)
        {
            m_camera->stop();
            delete m_camera;
        }
        m_take->setEnabled(false);
        m_status->setText("No camera.");
        showMessage("No camera is available.",
                    "Connect a camera, or if the Control Strip's Camera switch is Off, turn it On. "
                    "The picture appears here when a camera does.");
        return;
    }
    // Keep the camera in use if it is still there; otherwise the system's default, else the first.
    int index = 0;
    for (int i = 0; i < m_cameras.size(); ++i)
        if (m_cameras[i].id() == previous || (previous.isEmpty() && m_cameras[i].isDefault()))
            index = i;
    m_choice->setCurrentIndex(index);
    if (!m_camera || m_camera->cameraDevice().id() != m_cameras[index].id())
        useCamera(m_cameras[index]);
}

void SnapshotWindow::useCamera(const QCameraDevice &device)
{
    if (m_camera)
    {
        m_camera->stop();
        delete m_camera;
    }
    m_camera = new QCamera(device, this);
    connect(m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &text) {
        if (error == QCamera::NoError)
            return;
        m_take->setEnabled(false);
        m_status->setText("The camera stopped.");
        showMessage("The camera isn't working.", text.isEmpty() ? "It may be in use by another program." : text);
    });
    m_session->setCamera(m_camera);
    m_screen->setCurrentWidget(m_preview);
    m_status->setText(device.description());
    m_take->setEnabled(false); // Until the camera is ready for a picture.
    m_camera->start();
}
