#pragma once
#include <QCameraDevice>
#include <QDir>
#include <QImage>
#include <QMainWindow>
#include <QPointer>

class QCamera;
class QComboBox;
class QImageCapture;
class QLabel;
class QMediaCaptureSession;
class QMediaDevices;
class QPushButton;
class QStackedWidget;
class QVideoWidget;

// Snapshot (docs/snapshot.md; Lily Hewitt, 2026-11-12: "a super simple photo booth. basically to take
// basic photos with webcam and test its working"). An original Platinum-style window: the camera's
// live picture, Take Picture, and the last picture taken. Pictures are saved as JPEG in
// ~/Pictures/Snapshot. With no camera, or the Control Strip's Camera switch Off, it says so, and the
// picture comes back by itself when a camera does.
class SnapshotWindow final : public QMainWindow
{
    Q_OBJECT
  public:
    explicit SnapshotWindow(QDir folder, QWidget *parent = nullptr);
    ~SnapshotWindow() override;
    // The folder pictures are saved in (made when the first is taken).
    QDir folder() const { return m_folder; }
    // The picture just taken, for tests and the thumbnail.
    QString lastPicture() const { return m_lastPicture; }
    // Save a captured frame as the next picture; returns its path ("" when it can't be written).
    QString savePicture(const QImage &image);
    void takePicture();
    static QString nameFor(const QDateTime &when);

  signals:
    void pictureSaved(const QString &path);

  private:
    void refreshCameras();
    void useCamera(const QCameraDevice &device);
    void showMessage(const QString &title, const QString &detail);
    void showThumbnail(const QString &path);
    void flash();

    QDir m_folder;
    QMediaDevices *m_devices;
    QMediaCaptureSession *m_session;
    QPointer<QCamera> m_camera;
    QImageCapture *m_capture;
    QStackedWidget *m_screen;
    QVideoWidget *m_preview;
    QLabel *m_message, *m_thumbnail, *m_status, *m_flash;
    QComboBox *m_choice;
    QPushButton *m_take, *m_show;
    QList<QCameraDevice> m_cameras;
    QString m_lastPicture;
};
