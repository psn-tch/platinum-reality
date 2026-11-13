#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantList>

// The Control Strip's Camera module (docs/camera-switch.md; Lily Hewitt, 2026-11-12: "like nothing
// can access it if its toggled off"). The kernel does the blocking; the root helper platinum-webcam
// makes the change through pkexec: Off never asks, On asks for an administrator's password.
// Reading the state needs no administrator. The helper and pkexec are injectable for tests.
class CameraSwitch final : public QObject
{
    Q_OBJECT
    // The helper is installed.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool off READ off NOTIFY changed)
    // [{id, name, blocked}] for each USB camera.
    Q_PROPERTY(QVariantList cameras READ cameras NOTIFY changed)
    // Cameras this switch cannot block (not USB video), by name.
    Q_PROPERTY(QStringList others READ others NOTIFY changed)
    // The helper is switching (an administrator's password may be on screen).
    Q_PROPERTY(bool changing READ changing NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    // `elevate` runs the helper as root ({"pkexec"}); empty runs it directly (tests).
    CameraSwitch(QString helper, QStringList elevate = {"pkexec"}, QObject *parent = nullptr);
    ~CameraSwitch() override;
    bool available() const;
    bool off() const { return m_off; }
    QVariantList cameras() const { return m_cameras; }
    QStringList others() const { return m_others; }
    bool changing() const { return m_switch.state() != QProcess::NotRunning; }
    QString error() const { return m_error; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setOff(bool off);

  signals:
    void changed();

  private:
    void finished(QProcess *process, int code, QProcess::ExitStatus status);
    QString m_helper;
    QStringList m_elevate;
    QProcess m_status, m_switch;
    QVariantList m_cameras;
    QStringList m_others;
    QString m_error;
    bool m_off = false;
};
