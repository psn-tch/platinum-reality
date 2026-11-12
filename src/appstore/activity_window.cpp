#include "activity_window.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QTime>
#include <QVBoxLayout>

ActivityWindow::ActivityWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setWindowTitle("App Store Activity");
    setObjectName("appStoreActivity");
    resize(620, 380);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    auto *top = new QHBoxLayout;
    m_heading = new QLabel;
    m_heading->setObjectName("appStoreActivityHeading");
    m_heading->setTextFormat(Qt::PlainText);
    m_heading->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto font = m_heading->font();
    font.setBold(true);
    m_heading->setFont(font);
    m_progress = new QProgressBar;
    m_progress->setObjectName("appStoreActivityProgress");
    m_progress->setTextVisible(false);
    m_progress->setFixedWidth(160);
    m_progress->hide();
    top->addWidget(m_heading, 1);
    top->addWidget(m_progress);
    layout->addLayout(top);
    m_log = new QPlainTextEdit;
    m_log->setObjectName("appStoreActivityLog");
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_log, 1);
    auto *buttons = new QHBoxLayout;
    auto *copy = new QPushButton("Copy All");
    copy->setObjectName("appStoreActivityCopy");
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_log->toPlainText()); });
    auto *clear = new QPushButton("Clear");
    clear->setObjectName("appStoreActivityClear");
    connect(clear, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
    buttons->addWidget(copy);
    buttons->addWidget(clear);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    heading("Nothing is being installed or removed right now.");
}

void ActivityWindow::heading(const QString &text) { m_heading->setText(text); }

void ActivityWindow::begin(const QString &doing)
{
    m_doing = doing;
    heading(doing + "…");
    m_progress->setRange(0, 0);
    m_progress->show();
    if (!m_log->document()->isEmpty())
        m_log->appendPlainText(QString());
    m_log->appendPlainText(QString("— %1, %2 —").arg(doing, QTime::currentTime().toString("h:mm:ss AP")));
}

void ActivityWindow::line(const QString &text)
{
    // Follows the newest line, unless the log was scrolled back to read something.
    auto *bar = m_log->verticalScrollBar();
    const bool following = bar->value() >= bar->maximum() - 2;
    m_log->appendPlainText(text);
    if (following)
        bar->setValue(bar->maximum());
}

void ActivityWindow::progress(int step, int steps)
{
    if (step > 0 && steps > 0)
    {
        m_progress->setRange(0, steps);
        m_progress->setValue(step);
    }
}

void ActivityWindow::finish(bool ok, const QString &error, bool cancelled)
{
    m_progress->hide();
    const auto doing = m_doing.isEmpty() ? QString("The change") : m_doing;
    const auto result = ok ? QString("%1: finished.").arg(doing)
                      : cancelled ? QString("%1: cancelled.").arg(doing)
                                  : QString("%1 didn’t work.").arg(doing) + (error.isEmpty() ? QString() : " " + error);
    line(QString("— %1 —").arg(result));
    heading(result);
    m_doing.clear();
}

QString ActivityWindow::text() const { return m_log->toPlainText(); }
