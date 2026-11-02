#pragma once
#include "display_layout.hpp"
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// What the Monitors panel shows beside the layout, from a supervisor that manages the displays
// (Monitors choices decide which are on): monitor names, connected displays that are off, and
// whether a change is waiting for "Keep". An unmanaged session has none of these.
struct DisplayDetails
{
    bool managed = false;
    QVariantMap labels;      // connector -> monitor name
    QVariantList available;  // {name, label, lidClosed} for connected displays that are off
    QVariantMap modes;       // connector -> {modes: [{width, height, refresh}], mode: {...}} (refresh in mHz)
    bool pending = false;
    QVariantList mirrors;    // {name, label}: displays that are on, showing the main display
};

struct DisplaySnapshot
{
    QString session;
    quint64 generation;
    DisplayLayout layout;
    DisplayDetails details;
};

std::optional<DisplaySnapshot> parseDisplaySnapshot(const QByteArray &bytes, QString *error = nullptr);

// Reads an owned private session file and writes a paired acknowledgement.
// Never opens Sway IPC or changes physical outputs. Atomic replacement is
// watched through the session directory.
class DisplayTopologyFeed final : public QObject
{
    Q_OBJECT
  public:
    explicit DisplayTopologyFeed(QString path, QObject *parent = nullptr);
    bool refresh();
    bool acknowledge(quint64 generation);
    const std::optional<DisplaySnapshot> &snapshot() const { return m_snapshot; }
    QString error() const { return m_error; }
  signals:
    void changed();
    void errorOccurred(QString message);
  private:
    bool fail(const QString &message);
    QString m_path, m_error;
    QByteArray m_bytes;
    std::optional<DisplaySnapshot> m_snapshot;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
