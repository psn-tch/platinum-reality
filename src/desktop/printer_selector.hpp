#pragma once
#include <QObject>
#include <functional>
#include <QStringList>
#include <QVariantList>

class QProcess;

// The Control Strip's Printer Selector (docs/fidelity/control-strip.md): the CUPS printers this
// user can print to and their default, read with lpstat; choosing one sets this user's default
// with lpoptions -d (~/.cups/lpoptions, no administrator needed). The programs are injectable
// for tests. Everything runs asynchronously; a missing CUPS just lists no printers.
class PrinterSelector : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList printers READ printers NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit PrinterSelector(QString lpstat = "lpstat", QString lpoptions = "lpoptions", QObject *parent = nullptr);
    QVariantList printers() const;
    bool busy() const { return m_running != nullptr; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool setDefault(const QString &name);
signals:
    void changed();
private:
    void run(const QString &program, const QStringList &arguments, std::function<void(int, const QByteArray &)> done);
    QString m_lpstat, m_lpoptions;
    QStringList m_names;
    QString m_default;
    QProcess *m_running = nullptr;
};
