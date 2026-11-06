#pragma once
#include <QList>
#include <QString>
#include <QVariantMap>

struct ComputerApplication
{
    qulonglong id = 0;
    QString name;
    QList<qint64> processes;
};

// Read-only, bounded /proc snapshot. An injectable proc root supports fixtures.
// Missing/malformed measurements stay unavailable; no process tree is guessed.
QVariantMap computerInformation(const QList<ComputerApplication> &applications,
                                const QString &procRoot = "/proc");
