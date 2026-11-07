#include "installer_window.hpp"
#include "engine_client.hpp"
#include <QAbstractButton>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyleOptionComboBox>
#include <QTextBrowser>
#include <QTimeZone>
#include <QTimer>

// The look follows the Mac OS 9.2.2 Installer ("Install Macintosh Software"), captured in psn_tch's
// 9.2.2 VM on 2026-11-06: a 45-pixel band fading from #dddddd to #ccccff with the step's icons, name
// and a ? button; an #eeeeee page; a #dddddd footer with the step's buttons at the left and Go Back
// and the default button at the right. Every drawing here is original: none of Apple's art.
namespace
{
const char *const titles[] = {"Welcome", "Select Destination", "Important Information", "Software License Agreement",
                              "Your Account", "Install Software", "Install Software", "Install Software"};
const QColor bandColor("#ccccff"), pageColor("#eeeeee"), footerColor("#dddddd"), etchColor("#777777");

// The install engine's own minimum (distro/installer-engine/engine.py MIN_DISK_BYTES).
constexpr qint64 minimumDiskBytes = 16LL * 1024 * 1024 * 1024;

QString sizeText(qint64 bytes)
{
    const double gb = bytes / 1e9;
    return gb >= 1000 ? QString::number(gb / 1000, 'f', 1) + " TB" : QString::number(gb, 'f', gb < 10 ? 1 : 0) + " GB";
}

// The installer's own mark: a smiling computer.
void drawMark(QPainter &p, const QRectF &r)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF body(r.x() + r.width() * 0.1, r.y() + r.height() * 0.04, r.width() * 0.8, r.height() * 0.74);
    p.setPen(QPen(QColor("#222222"), 1.2));
    p.setBrush(QColor("#dddddd"));
    p.drawRoundedRect(body, 3, 3);
    const QRectF screen = body.adjusted(body.width() * 0.16, body.height() * 0.13, -body.width() * 0.16, -body.height() * 0.34);
    p.setBrush(QColor("#6666cc"));
    p.drawRect(screen);
    p.setPen(QPen(Qt::white, 1.4));
    const auto c = screen.center();
    p.drawPoint(QPointF(c.x() - screen.width() * 0.2, c.y() - screen.height() * 0.15));
    p.drawPoint(QPointF(c.x() + screen.width() * 0.2, c.y() - screen.height() * 0.15));
    p.drawArc(QRectF(c.x() - screen.width() * 0.22, c.y() - screen.height() * 0.1, screen.width() * 0.44,
                     screen.height() * 0.4), 200 * 16, 140 * 16);
    p.setPen(QPen(QColor("#222222"), 1.2));
    p.setBrush(QColor("#bbbbbb"));
    p.drawRect(QRectF(body.x() + body.width() * 0.08, body.bottom() + 1, body.width() * 0.84, r.height() * 0.14));
    p.restore();
}

// The step's own icon, beside the mark: a disk, a document, or a document being signed.
void drawStepIcon(QPainter &p, const QRectF &r, int step)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#222222"), 1.1));
    if (step == InstallerWindow::Destination)
    {
        const QRectF drive(r.x() + 2, r.y() + r.height() * 0.45, r.width() - 4, r.height() * 0.32);
        p.setBrush(QColor("#eeeeee"));
        p.drawRoundedRect(drive, 2, 2);
        p.setBrush(QColor("#33aa33"));
        p.drawRect(QRectF(drive.x() + 4, drive.bottom() - 5, 3, 2));
        // The arrow down onto the disk: where it will go.
        p.setPen(QPen(QColor("#333399"), 2));
        p.drawLine(QPointF(r.center().x(), r.y() + 2), QPointF(r.center().x(), drive.y() - 3));
        p.drawLine(QPointF(r.center().x() - 4, drive.y() - 7), QPointF(r.center().x(), drive.y() - 3));
        p.drawLine(QPointF(r.center().x() + 4, drive.y() - 7), QPointF(r.center().x(), drive.y() - 3));
    }
    else
    {
        const QRectF sheet(r.x() + r.width() * 0.18, r.y() + 1, r.width() * 0.62, r.height() - 2);
        QPainterPath outline;
        outline.moveTo(sheet.topLeft());
        outline.lineTo(sheet.right() - 6, sheet.top());
        outline.lineTo(sheet.right(), sheet.top() + 6);
        outline.lineTo(sheet.bottomRight());
        outline.lineTo(sheet.bottomLeft());
        outline.closeSubpath();
        p.setBrush(Qt::white);
        p.drawPath(outline);
        p.drawLine(QPointF(sheet.right() - 6, sheet.top()), QPointF(sheet.right() - 6, sheet.top() + 6));
        p.drawLine(QPointF(sheet.right() - 6, sheet.top() + 6), QPointF(sheet.right(), sheet.top() + 6));
        p.setPen(QPen(QColor("#666666"), 1));
        for (int line = 0; line < 5; ++line)
            p.drawLine(QPointF(sheet.left() + 3, sheet.top() + 9 + line * 4), QPointF(sheet.right() - 3, sheet.top() + 9 + line * 4));
        if (step == InstallerWindow::License)
        {
            p.setPen(QPen(QColor("#222222"), 1));
            p.setBrush(QColor("#993333"));
            QPolygonF pen{{r.right() - 2, r.y() + 4}, {r.right(), r.y() + 6}, {r.x() + r.width() * 0.45, r.bottom() - 4},
                          {r.x() + r.width() * 0.42, r.bottom() - 2}};
            p.drawPolygon(pen);
        }
    }
    p.restore();
}

// The ? at the band's right: Help for this step.
class HelpButton final : public QAbstractButton
{
  public:
    explicit HelpButton(QWidget *parent = nullptr) : QAbstractButton(parent)
    {
        setFixedSize(22, 20);
        setFocusPolicy(Qt::NoFocus);
        setToolTip("Help");
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect r = rect().adjusted(0, 0, -1, -1);
        p.fillRect(r, isDown() ? QColor("#888888") : QColor("#dddddd"));
        p.setPen(Qt::black);
        p.drawRect(r);
        p.setPen(isDown() ? QColor("#555555") : Qt::white);
        p.drawLine(1, 1, r.width() - 1, 1);
        p.drawLine(1, 1, 1, r.height() - 1);
        p.setPen(QColor("#888888"));
        p.drawLine(2, r.height() - 1, r.width() - 1, r.height() - 1);
        p.drawLine(r.width() - 1, 2, r.width() - 1, r.height() - 1);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF circle(5, 3.5, 12, 12);
        p.setPen(QPen(Qt::black, 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(circle);
        auto font = this->font();
        font.setBold(true);
        font.setPixelSize(10);
        p.setFont(font);
        p.drawText(circle, Qt::AlignCenter, "?");
    }
};

// The band across the top.
class TitleBand final : public QWidget
{
  public:
    TitleBand(QLabel *title, QWidget *extra, HelpButton *help, QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(47);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(86, 2, 12, 3);
        auto font = title->font();
        font.setBold(true);
        title->setFont(font);
        layout->addWidget(title);
        layout->addStretch();
        layout->addWidget(extra);
        layout->addSpacing(8);
        layout->addWidget(help);
    }
    int step = InstallerWindow::Welcome;

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QLinearGradient fade(0, 0, width() * 0.45, 0);
        fade.setColorAt(0, footerColor);
        fade.setColorAt(1, bandColor);
        p.fillRect(rect(), fade);
        p.setPen(Qt::white);
        p.drawLine(0, 0, width(), 0);
        p.setPen(QColor("#9999cc"));
        p.drawLine(0, height() - 2, width(), height() - 2);
        p.setPen(QColor("#666666"));
        p.drawLine(0, height() - 1, width(), height() - 1);
        drawMark(p, QRectF(12, 7, 32, 32));
        if (step == InstallerWindow::Destination || step == InstallerWindow::Information || step == InstallerWindow::License)
            drawStepIcon(p, QRectF(46, 7, 32, 32), step);
    }
};

// A Platinum pop-up menu: a bevelled box with the choice and a ⇕ box at the right.
class PopupMenu final : public QComboBox
{
  public:
    using QComboBox::QComboBox;
    QSize sizeHint() const override { return {QComboBox::sizeHint().width() + 8, 21}; }
    QSize minimumSizeHint() const override { return {60, 21}; }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -1.5, -1.5);
        p.setPen(QPen(isEnabled() ? Qt::black : QColor("#888888"), 1));
        p.setBrush(footerColor);
        p.drawRoundedRect(r, 3, 3);
        p.setRenderHint(QPainter::Antialiasing, false);
        const int arrows = height() + 1, split = width() - arrows - 2;
        p.setPen(Qt::white);
        p.drawLine(2, 1, split - 2, 1);
        p.drawLine(1, 2, 1, height() - 4);
        p.drawLine(split + 2, 1, width() - 4, 1);
        p.setPen(QColor("#aaaaaa"));
        p.drawLine(2, height() - 3, width() - 4, height() - 3);
        p.drawLine(split, 2, split, height() - 4);
        p.setPen(QColor("#777777"));
        p.drawLine(width() - 3, 3, width() - 3, height() - 4);
        p.setPen(Qt::white);
        p.drawLine(split + 1, 2, split + 1, height() - 4);
        // ⇕
        const QPointF c(split + (width() - split) / 2.0, height() / 2.0 - 0.5);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(isEnabled() ? Qt::black : QColor("#888888"));
        p.drawPolygon(QPolygonF{{c.x() - 3.5, c.y() - 1.5}, {c.x() + 3.5, c.y() - 1.5}, {c.x(), c.y() - 5.5}});
        p.drawPolygon(QPolygonF{{c.x() - 3.5, c.y() + 1.5}, {c.x() + 3.5, c.y() + 1.5}, {c.x(), c.y() + 5.5}});
        p.setPen(isEnabled() ? Qt::black : QColor("#888888"));
        p.drawText(QRect(9, 0, split - 14, height() - 1), Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(currentText(), Qt::ElideRight, split - 14));
    }
};

// A titled group: an etched line round its contents, broken by the bold title.
class Group final : public QWidget
{
  public:
    explicit Group(const QString &title, QWidget *parent = nullptr) : QWidget(parent), m_title(title)
    {
        auto font = this->font();
        font.setBold(true);
        m_bold = font;
        setContentsMargins(12, QFontMetrics(font).height() + 6, 12, 10);
    }
    // A title that is a control of its own ("Destination Disk:" and its pop-up) sits on the line.
    void setHeader(QWidget *header)
    {
        m_header = header;
        header->setParent(this);
        setContentsMargins(12, header->sizeHint().height() + 6, 12, 10);
    }

  protected:
    void resizeEvent(QResizeEvent *) override
    {
        if (m_header)
            m_header->setGeometry(8, 0, std::min(width() - 16, m_header->sizeHint().width()), m_header->sizeHint().height());
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int line = m_header ? m_header->sizeHint().height() / 2 : QFontMetrics(m_bold).height() / 2;
        const int gapEnd = m_header ? 8 + m_header->width() + 4 : 12 + QFontMetrics(m_bold).horizontalAdvance(m_title) + 4;
        const QRect r(0, line, width() - 2, height() - line - 2);
        p.setPen(etchColor);
        p.drawLine(r.left(), r.top(), 4, r.top());
        p.drawLine(gapEnd, r.top(), r.right(), r.top());
        p.drawLine(r.left(), r.top(), r.left(), r.bottom());
        p.drawLine(r.left(), r.bottom(), r.right(), r.bottom());
        p.drawLine(r.right(), r.top(), r.right(), r.bottom());
        p.setPen(Qt::white);
        p.drawLine(r.left() + 1, r.top() + 1, 4, r.top() + 1);
        p.drawLine(gapEnd, r.top() + 1, r.right() - 1, r.top() + 1);
        p.drawLine(r.left() + 1, r.top() + 1, r.left() + 1, r.bottom() - 1);
        p.drawLine(r.left(), r.bottom() + 1, r.right() + 1, r.bottom() + 1);
        p.drawLine(r.right() + 1, r.top(), r.right() + 1, r.bottom() + 1);
        if (!m_header)
        {
            p.setFont(m_bold);
            p.setPen(Qt::black);
            p.drawText(QPoint(8, QFontMetrics(m_bold).ascent()), m_title);
        }
    }

  private:
    QString m_title;
    QFont m_bold;
    QWidget *m_header = nullptr;
};

// The 9.2.2 progress bar: a black frame, a blue tube for the part done, grey for the rest.
class ProgressTube final : public QProgressBar
{
  public:
    using QProgressBar::QProgressBar;
    QSize sizeHint() const override { return {200, 14}; }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect frame(0, 0, width() - 1, 12);
        p.fillRect(frame.adjusted(1, 1, 0, 0), QColor("#bbbbbb"));
        p.setPen(QColor("#888888"));
        p.drawLine(1, 1, frame.right() - 1, 1);
        const double fraction = maximum() > minimum() ? double(value() - minimum()) / (maximum() - minimum()) : 0;
        const int done = qRound((frame.width() - 1) * std::clamp(fraction, 0.0, 1.0));
        static const char *const tube[] = {"#333399", "#6666cc", "#9999ff", "#ccccff", "#eeeeee",
                                           "#ccccff", "#9999ff", "#6666cc", "#333399", "#000055"};
        for (int row = 0; row < 10; ++row)
        {
            p.setPen(QColor(tube[row]));
            if (done > 0)
                p.drawLine(1, row + 1, done, row + 1);
        }
        p.setPen(QColor("#dddddd"));
        p.drawLine(done + 1, 11, frame.right() - 1, 11);
        p.setPen(Qt::black);
        p.drawRect(frame);
        p.setPen(Qt::white);
        p.drawLine(1, 13, frame.right() + 1, 13);
        p.drawLine(frame.right() + 1, 1, frame.right() + 1, 13);
    }
};

// A sunken box: the Destination page's message, and the progress box.
class Inset final : public QWidget
{
  public:
    explicit Inset(QColor fill, QWidget *parent = nullptr) : QWidget(parent), m_fill(fill) {}

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), m_fill);
        p.setPen(QColor("#aaaaaa"));
        p.drawLine(0, 0, width() - 1, 0);
        p.drawLine(0, 0, 0, height() - 1);
        p.setPen(Qt::white);
        p.drawLine(1, height() - 1, width() - 1, height() - 1);
        p.drawLine(width() - 1, 1, width() - 1, height() - 1);
    }

  private:
    QColor m_fill;
};

// The page background, with the faint mark behind the Welcome text.
class PageArea final : public QStackedWidget
{
  public:
    bool watermark = true;

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), pageColor);
        if (!watermark)
            return;
        // Only the outline, very light, as 9.2.2's large face sat behind its Welcome text.
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#d6d6f0"), 3));
        p.setBrush(Qt::NoBrush);
        const QRectF body(width() - 230, 18, 190, 160), screen = body.adjusted(30, 22, -30, -54);
        p.drawRoundedRect(body, 10, 10);
        p.drawRect(screen);
        p.drawArc(QRectF(screen.center().x() - 30, screen.center().y() - 6, 60, 36), 200 * 16, 140 * 16);
        p.drawRect(QRectF(body.x() + 14, body.bottom() + 6, body.width() - 28, 26));
    }
};

class Footer final : public QWidget
{
  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), footerColor);
        p.setPen(etchColor);
        p.drawLine(0, 0, width(), 0);
        p.setPen(Qt::white);
        p.drawLine(0, 1, width(), 1);
    }
};

QLabel *wrapped(const QString &text)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

QFont smaller(QFont font)
{
    font.setPointSizeF(font.pointSizeF() * 0.85);
    return font;
}

QFont documentFont()
{
    QFont font("Times");
    font.setStyleHint(QFont::Serif);
    font.setPointSize(11);
    return font;
}

const char *const help[] = {
    "This program installs Platinum Reality in four steps, and asks you about your account on the way. "
    "Nothing is changed on any disk until you click Start on the last step.",
    "Choose the disk to install on from the Destination Disk pop-up menu. The whole disk is erased: "
    "everything on it is lost. Options lets you choose whether the disk is encrypted.",
    "Read this information before you install. Save keeps a copy of it.",
    "Read the license. When you click Continue, you are asked whether you agree to it.",
    "Your name, short name and password are for the account you log in with. The password also unlocks "
    "an encrypted disk each time the computer starts up.",
    "Click Start to erase the destination disk and install Platinum Reality. You are asked once more before "
    "anything is erased.",
    "Platinum Reality is being installed. This takes a few minutes; don't turn off the computer.",
    "The installation has finished. Restart the computer to use Platinum Reality."};

QString languageName(const QString &locale)
{
    const QLocale language(locale.section('.', 0, 0).section('@', 0, 0));
    if (language.language() == QLocale::C)
        return locale;
    return QLocale::languageToString(language.language()) + " (" + QLocale::territoryToString(language.territory()) +
           ") \u2014 " + locale;
}
} // namespace

InstallerWindow::InstallerWindow(EngineClient *engine, QString informationText, QString licenseText, QWidget *parent)
    : QWidget(parent), m_engine(engine)
{
    setWindowTitle("Install Platinum Reality Software");
    setFixedSize(540, 372); // One size, like the Mac OS 9 Installer window.
    m_title = new QLabel;
    m_title->setObjectName("installer-title");
    m_licenseLanguage = new PopupMenu;
    m_licenseLanguage->setObjectName("installer-license-language");
    m_licenseLanguage->addItem("English");
    m_licenseLanguage->setFocusPolicy(Qt::NoFocus);
    auto *helpButton = new HelpButton;
    helpButton->setObjectName("installer-help");
    auto *bandWidget = new TitleBand(m_title, m_licenseLanguage, helpButton);
    m_band = bandWidget;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bandWidget);
    auto *area = new PageArea;
    m_pages = area;
    m_pages->addWidget(welcomePage());
    m_pages->addWidget(destinationPage());
    m_pages->addWidget(textPage(&m_information, informationText));
    m_pages->addWidget(textPage(&m_license, licenseText));
    m_pages->addWidget(accountPage());
    m_pages->addWidget(installPage());
    m_pages->addWidget(progressPage());
    m_pages->addWidget(finishedPage());
    layout->addWidget(m_pages, 1);
    auto *bar = new Footer;
    bar->setFixedHeight(42);
    auto *buttons = new QHBoxLayout(bar);
    buttons->setContentsMargins(12, 9, 12, 7);
    buttons->setSpacing(12);
    const auto button = [&](const QString &text, const char *name, int width) {
        auto *b = new QPushButton(text);
        b->setObjectName(name);
        b->setMinimumWidth(width);
        b->setAutoDefault(false);
        return b;
    };
    m_options = button("Options…", "installer-options", 90);
    m_save = button("Save…", "installer-save", 90);
    m_print = button("Print…", "installer-print", 90);
    m_print->setEnabled(false); // As in 9.2.2 here: nothing to print to.
    m_back = button("Go Back", "installer-back", 90);
    m_continue = button("Continue", "installer-continue", 92);
    m_continue->setDefault(true);
    for (auto *b : {m_options, m_save, m_print})
        buttons->addWidget(b);
    buttons->addStretch();
    for (auto *b : {m_back, m_continue})
        buttons->addWidget(b);
    layout->addWidget(bar);
    connect(m_back, &QPushButton::clicked, this, &InstallerWindow::goBack);
    connect(m_continue, &QPushButton::clicked, this, &InstallerWindow::goForward);
    connect(m_options, &QPushButton::clicked, this, &InstallerWindow::showOptions);
    connect(m_save, &QPushButton::clicked, this, &InstallerWindow::saveText);
    connect(helpButton, &QAbstractButton::clicked, this, [this] {
        QMessageBox box(QMessageBox::Information, "Help", help[m_page], QMessageBox::Ok, this);
        box.setOption(QMessageBox::Option::DontUseNativeDialog);
        box.exec();
    });
    connect(m_engine, &EngineClient::disksListed, this, [this](const QVariantList &disks) {
        showDisks(disks);
        // Then, once, what the live image can install; the engine runs one command at a time.
        if (!m_choicesAsked)
        {
            m_choicesAsked = true;
            QTimer::singleShot(0, m_engine, &EngineClient::listChoices);
        }
    });
    connect(m_engine, &EngineClient::choicesListed, this, &InstallerWindow::applyChoices);
    connect(m_engine, &EngineClient::failed, this, [this](const QString &message) {
        if (m_page == Destination)
            m_destinationNote->setText(message);
        else if (m_page == Install || m_page == Progress)
        {
            m_installOk = false;
            finish(message);
        }
    });
    connect(m_engine, &EngineClient::planned, this, [this](const QJsonObject &plan) {
        if (m_page != Install)
            return;
        // Only now, with the engine's plan, does the question say what is on the disk.
        if (ask("Erase Disk", eraseQuestion(plan), "Erase and Install", "Cancel", false))
            startInstall();
        else
            goTo(Install);
    });
    connect(m_engine, &EngineClient::progress, this, [this](const QString &step, double fraction, const QString &message) {
        // The engine erases from "partition" on; before that, nothing on any disk has changed.
        if (step != "validate" && step != "check")
            m_diskChanged = true;
        m_progress->setValue(qRound(fraction * 1000));
        if (!message.isEmpty())
            m_progressText->setText(message);
        m_remaining->setText(remainingText(fraction));
    });
    connect(m_engine, &EngineClient::installFinished, this, [this](bool ok, const QString &message) {
        m_installOk = ok;
        finish(message);
    });
    goTo(Welcome);
}

QWidget *InstallerWindow::welcomePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 10);
    layout->setSpacing(6);
    layout->addWidget(wrapped("Welcome to the Platinum Reality installation program. This program will guide you "
                              "through the five steps necessary to install Platinum Reality."));
    const QStringList steps{"Select a destination disk", "Read important information",
                            "Respond to the software license agreement", "Set up your account", "Install software"};
    auto *grid = new QGridLayout;
    grid->setContentsMargins(18, 4, 0, 4);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(0);
    for (int i = 0; i < steps.size(); ++i)
    {
        auto *number = new QLabel(QString::number(i + 1) + ".");
        QFont numeral("Times");
        numeral.setStyleHint(QFont::Serif);
        numeral.setItalic(true);
        numeral.setPixelSize(26);
        number->setFont(numeral);
        grid->addWidget(number, i, 0, Qt::AlignRight | Qt::AlignBottom);
        grid->addWidget(new QLabel(steps[i]), i, 1, Qt::AlignLeft | Qt::AlignBottom);
    }
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);
    layout->addWidget(new QLabel("Click Continue to begin the first step."));
    layout->addStretch();
    return page;
}

QWidget *InstallerWindow::destinationPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);
    auto *intro = new QLabel("Choose a disk to install the software on, then click Select.");
    auto font = intro->font();
    font.setBold(true);
    intro->setFont(font);
    layout->addWidget(intro);
    auto *group = new Group(QString());
    auto *header = new QWidget;
    auto *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(4, 0, 4, 0);
    auto *label = new QLabel("Destination Disk:");
    label->setFont(font);
    m_disks = new PopupMenu;
    m_disks->setObjectName("installer-disks");
    m_disks->setMinimumWidth(330);
    headerRow->addWidget(label);
    headerRow->addWidget(m_disks);
    group->setHeader(header);
    auto *rows = new QGridLayout(group);
    rows->setHorizontalSpacing(12);
    rows->setVerticalSpacing(2);
    const auto row = [&](int at, const QString &name, QLabel **value) {
        auto *left = new QLabel(name);
        left->setFont(smaller(this->font()));
        *value = new QLabel;
        (*value)->setFont(smaller(this->font()));
        rows->addWidget(left, at, 0);
        rows->addWidget(*value, at, 1);
    };
    row(0, "Currently on this disk:", &m_diskContents);
    row(1, "Available disk space:", &m_diskSize);
    row(2, "Approximate disk space required for installation:", &m_diskRequired);
    rows->setColumnStretch(2, 1); // The values in a column after the names, as 9.2.2 set them.
    m_diskRequired->setText("16 GB");
    auto *message = new Inset(pageColor);
    auto *messageLayout = new QVBoxLayout(message);
    messageLayout->setContentsMargins(6, 5, 6, 5);
    m_destinationNote = new QLabel;
    m_destinationNote->setObjectName("installer-destination-note");
    m_destinationNote->setWordWrap(true);
    m_destinationNote->setFont(smaller(this->font()));
    m_destinationNote->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    messageLayout->addWidget(m_destinationNote);
    messageLayout->addStretch();
    rows->addWidget(message, 3, 0, 1, 3);
    message->setFixedHeight(50);
    layout->addWidget(group);
    layout->addStretch();
    // Destination Options: whether the disk is encrypted (9.2.2 asked about a clean installation here).
    m_optionsDialog = new QDialog(this);
    m_optionsDialog->setWindowTitle("Options");
    auto *options = new QVBoxLayout(m_optionsDialog);
    options->setContentsMargins(14, 12, 14, 12);
    m_encrypt = new QCheckBox("Encrypt Disk");
    m_encrypt->setObjectName("installer-encrypt");
    m_encrypt->setChecked(true);
    auto bold = m_encrypt->font();
    bold.setBold(true);
    m_encrypt->setFont(bold);
    options->addWidget(m_encrypt);
    auto *explain = wrapped("Locks everything on the disk with your password. The computer asks for it each "
                            "time it starts up, and without it nobody can read the disk.");
    explain->setFont(smaller(this->font()));
    explain->setContentsMargins(22, 0, 0, 8);
    explain->setMinimumWidth(340);
    options->addWidget(explain);
    auto *choose = new QHBoxLayout;
    choose->addStretch();
    auto *cancel = new QPushButton("Cancel");
    auto *ok = new QPushButton("OK");
    ok->setDefault(true);
    for (auto *b : {cancel, ok})
    {
        b->setMinimumWidth(68);
        choose->addWidget(b);
    }
    options->addLayout(choose);
    connect(ok, &QPushButton::clicked, m_optionsDialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, m_optionsDialog, &QDialog::reject);
    connect(m_disks, &QComboBox::currentIndexChanged, this, [this] { describeDisk(); goTo(m_page); });
    return page;
}

void InstallerWindow::keyPressEvent(QKeyEvent *event)
{
    // Return presses the button with the ring, whichever control has focus, as in Mac OS 9 (Qt does
    // this only in dialogs). Space still presses the focused button. Start still asks first.
    const auto plain = event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier;
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && plain && m_continue->isVisible() &&
        m_continue->isEnabled())
    {
        m_continue->click();
        return;
    }
    QWidget::keyPressEvent(event);
}

QWidget *InstallerWindow::textPage(QTextBrowser **browser, const QString &text)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    *browser = new QTextBrowser;
    (*browser)->setPlainText(text);
    (*browser)->setOpenLinks(false);
    (*browser)->setFont(documentFont());
    (*browser)->setFrameShape(QFrame::NoFrame);
    (*browser)->document()->setDocumentMargin(8);
    layout->addWidget(*browser);
    return page;
}

QWidget *InstallerWindow::accountPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 8, 14, 6);
    layout->setSpacing(6);
    m_fullName = new QLineEdit;
    m_userName = new QLineEdit;
    m_password = new QLineEdit;
    m_verify = new QLineEdit;
    m_computerName = new QLineEdit("platinum");
    for (auto *field : {m_password, m_verify})
        field->setEchoMode(QLineEdit::Password);
    for (auto *field : {m_fullName, m_userName, m_password, m_verify, m_computerName})
        field->setFixedHeight(20);
    m_fullName->setObjectName("installer-full-name");
    m_userName->setObjectName("installer-user-name");
    m_password->setObjectName("installer-password");
    m_verify->setObjectName("installer-verify");
    m_computerName->setObjectName("installer-computer-name");
    m_timeZone = new PopupMenu;
    m_timeZone->setObjectName("installer-time-zone");
    // UTC first: the live disk's own clock, and the start when the computer's zone isn't known. Starting
    // on the list's first zone (Africa/Abidjan) set strangers' clocks wrong (update-37 image, 2026-11-07).
    m_timeZone->addItem("UTC");
    for (const auto &zone : QTimeZone::availableTimeZoneIds())
        if (zone.contains('/') && !zone.startsWith("Etc/"))
            m_timeZone->addItem(QString::fromLatin1(zone));
    const auto local = QString::fromLatin1(QTimeZone::systemTimeZoneId());
    m_timeZone->setCurrentIndex(std::max(0, m_timeZone->findText(local)));
    m_keymap = new PopupMenu;
    m_keymap->setObjectName("installer-keymap");
    m_keymap->addItems({"us", "uk", "de", "fr", "es", "it", "dvorak", "colemak"});
    m_language = new PopupMenu;
    m_language->setObjectName("installer-language");
    m_language->addItem(languageName("en_US.UTF-8"), "en_US.UTF-8");
    const auto form = [](Group *group) {
        auto *f = new QFormLayout(group);
        f->setHorizontalSpacing(10);
        f->setVerticalSpacing(4);
        f->setLabelAlignment(Qt::AlignRight);
        return f;
    };
    auto *account = new Group("Your Account");
    auto *accountForm = form(account);
    accountForm->addRow("Your name:", m_fullName);
    accountForm->addRow("Short name:", m_userName);
    accountForm->addRow("Password:", m_password);
    accountForm->addRow("Verify:", m_verify);
    auto *computer = new Group("This Computer");
    auto *computerForm = form(computer);
    computerForm->addRow("Computer name:", m_computerName);
    computerForm->addRow("Language:", m_language);
    computerForm->addRow("Time zone:", m_timeZone);
    computerForm->addRow("Keyboard:", m_keymap);
    layout->addWidget(account);
    layout->addWidget(computer);
    layout->addStretch();
    // The short name follows the full name until it is typed in itself.
    connect(m_fullName, &QLineEdit::textEdited, this, [this](const QString &name) {
        if (!m_userNameEdited)
            m_userName->setText(name.toLower().remove(QRegularExpression("[^a-z0-9]")).left(24));
        goTo(m_page);
    });
    connect(m_userName, &QLineEdit::textEdited, this, [this] { m_userNameEdited = true; goTo(m_page); });
    for (auto *field : {m_password, m_verify, m_computerName})
        connect(field, &QLineEdit::textChanged, this, [this] { goTo(m_page); });
    return page;
}

QWidget *InstallerWindow::installPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 10);
    m_installSummary = new QLabel;
    m_installSummary->setObjectName("installer-summary");
    m_installSummary->setWordWrap(true);
    layout->addWidget(m_installSummary);
    layout->addStretch();
    return page;
}

QWidget *InstallerWindow::progressPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 10);
    layout->setSpacing(14);
    m_progressHeading = new QLabel;
    m_progressHeading->setWordWrap(true);
    layout->addWidget(m_progressHeading);
    auto *box = new Inset(QColor("#dddddd"));
    auto *inner = new QGridLayout(box);
    inner->setContentsMargins(12, 10, 12, 8);
    inner->setHorizontalSpacing(12);
    inner->setVerticalSpacing(5);
    m_progressText = new QLabel("Preparing to install…");
    m_progressText->setObjectName("installer-progress-text");
    m_progress = new ProgressTube;
    m_progress->setObjectName("installer-progress");
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    auto *stop = new QPushButton("Stop");
    stop->setEnabled(false); // The engine can't stop halfway safely once the disk is being erased.
    stop->setMinimumWidth(70);
    m_remaining = new QLabel("Estimated Time Remaining: Calculating…");
    m_remaining->setObjectName("installer-remaining");
    m_remaining->setFont(smaller(font()));
    inner->addWidget(m_progressText, 0, 0, 1, 2);
    inner->addWidget(m_progress, 1, 0);
    inner->addWidget(stop, 1, 1);
    inner->addWidget(m_remaining, 2, 0, 1, 2);
    inner->setColumnStretch(0, 1);
    layout->addWidget(box);
    layout->addStretch();
    return page;
}

QWidget *InstallerWindow::finishedPage()
{
    // 9.2.2 closed its window and said so in an alert; the alert is a child of this window, so the
    // page behind it is the finished Install Software page.
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 10);
    m_finishedSummary = new QLabel;
    m_finishedSummary->setWordWrap(true);
    layout->addWidget(m_finishedSummary);
    layout->addStretch();
    m_finishedAlert = new QDialog(this);
    m_finishedAlert->setWindowTitle("Install Platinum Reality");
    m_finishedAlert->setObjectName("installer-finished");
    auto *alert = new QGridLayout(m_finishedAlert);
    alert->setContentsMargins(16, 16, 16, 12);
    alert->setHorizontalSpacing(16);
    auto *icon = new QLabel;
    icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));
    alert->addWidget(icon, 0, 0, Qt::AlignTop);
    m_finishedText = new QLabel;
    m_finishedText->setObjectName("installer-finished-text");
    m_finishedText->setWordWrap(true);
    m_finishedText->setMinimumWidth(320);
    alert->addWidget(m_finishedText, 0, 1);
    auto *choose = new QHBoxLayout;
    choose->addStretch();
    m_quit = new QPushButton("Quit");
    m_quit->setObjectName("installer-quit");
    m_quit->setMinimumWidth(80);
    m_quit->setAutoDefault(false);
    m_restart = new QPushButton("Restart");
    m_restart->setObjectName("installer-restart");
    m_restart->setMinimumWidth(80);
    m_restart->setAutoDefault(false);
    connect(m_restart, &QPushButton::clicked, this, &InstallerWindow::restartRequested);
    choose->addWidget(m_quit);
    choose->addWidget(m_restart);
    alert->addLayout(choose, 1, 0, 1, 2);
    connect(m_quit, &QPushButton::clicked, m_finishedAlert, &QDialog::accept);
    connect(m_quit, &QPushButton::clicked, this, &QWidget::close);
    return page;
}

void InstallerWindow::finish(const QString &message)
{
    const auto disk = diskName();
    m_finishedText->setText(
        m_installOk ? "The installation process has finished.\n\nRemove the installation disk, then click Restart to "
                      "use your new software. Click Quit to leave the installer."
                    : "The installation could not be completed. " + message + "\n\n" +
                          (m_diskChanged
                               ? "The installer had already started erasing “" + disk + "”, so what was on it before "
                                 "is gone, and it can't start up Platinum Reality yet. Your other disks were not "
                                 "changed. You can quit and install again on the same disk."
                               : "Nothing was changed: “" + disk + "” was not erased. You can quit and try again."));
    m_finishedSummary->setText(m_installOk ? "Platinum Reality was installed on “" + disk + "”."
                                           : "Platinum Reality was not installed.");
    m_restart->setVisible(m_installOk);
    m_restart->setDefault(m_installOk);
    m_quit->setDefault(!m_installOk);
    goTo(Finished);
    m_finishedAlert->open();
    (m_installOk ? m_restart : m_quit)->setFocus(Qt::OtherFocusReason);
}

QString InstallerWindow::remainingText(double fraction)
{
    if (!m_installClock.isValid())
        m_installClock.start();
    const double seconds = m_installClock.elapsed() / 1000.0;
    if (fraction < 0.05 || seconds < 5)
        return "Estimated Time Remaining: Calculating…";
    const double left = seconds * (1 - fraction) / fraction;
    const int minutes = qRound(left / 60);
    return minutes < 1 ? "Estimated Time Remaining: Less than a minute"
                       : minutes == 1 ? "Estimated Time Remaining: About 1 minute"
                                      : QString("Estimated Time Remaining: About %1 minutes").arg(minutes);
}

QString InstallerWindow::accountProblem() const
{
    // The install engine's own rules (distro/installer-engine/engine.py), so nothing it would refuse
    // gets as far as the Start button.
    static const QRegularExpression user("^[a-z_][a-z0-9_-]{0,30}$"),
        host("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"), unusable("[\\x00-\\x1f:\\x7f]"),
        lineBreak("[\\x00\\r\\n]");
    const auto name = m_fullName->text().trimmed(), shortName = m_userName->text(), password = m_password->text();
    if (name.isEmpty())
        return "Type your name.";
    if (name.size() > 128 || name.contains(unusable))
        return "Your name can be up to 128 characters, without a colon.";
    if (!user.match(shortName).hasMatch())
        return "The short name uses lowercase letters, numbers, - and _ (up to 31), and starts with a letter.";
    if (QStringList{"root", "liveuser", "sddm", "nobody"}.contains(shortName))
        return "That short name belongs to the system. Choose another.";
    if (password.size() < 8)
        return "The password needs at least 8 characters.";
    if (password.size() > 1024 || password.contains(lineBreak))
        return "The password can be up to 1024 characters, on one line.";
    if (password != m_verify->text())
        return "The two passwords are not the same.";
    if (!host.match(m_computerName->text()).hasMatch())
        return "The computer name uses lowercase letters, numbers and hyphens, and can't end with a hyphen.";
    return {};
}

QJsonObject InstallerWindow::config() const
{
    return {{"version", 1},
            {"disk", chosenDisk()},
            {"hostname", m_computerName->text()},
            {"timezone", m_timeZone->currentText()},
            {"locale", m_language->currentData().toString()},
            {"keymap", m_keymap->currentText()},
            {"user", QJsonObject{{"name", m_userName->text()}, {"full_name", m_fullName->text().trimmed()}}},
            {"encrypt", m_encrypt->isChecked()},
            {"network", "iwd-networkd"}};
}

QString InstallerWindow::chosenDisk() const
{
    return diskUsable(m_disks->currentIndex()) ? m_disks->currentData().toMap().value("path").toString() : QString();
}

QString InstallerWindow::diskName() const
{
    return m_disks->currentIndex() >= 0 ? m_disks->currentText() : QString();
}

bool InstallerWindow::diskUsable(int index) const
{
    auto *model = qobject_cast<QStandardItemModel *>(m_disks->model());
    return index >= 0 && model && model->item(index) && model->item(index)->isEnabled();
}

void InstallerWindow::applyChoices(const QJsonObject &choices)
{
    // Only well-formed names, at most 2000 of each; an engine that cannot say keeps the built-in lists.
    const auto names = [&](const char *key, const QRegularExpression &shape) {
        QStringList result;
        for (const auto &value : choices.value(key).toArray())
            if (value.isString() && shape.match(value.toString()).hasMatch() && result.size() < 2000)
                result << value.toString();
        return result;
    };
    const auto fill = [](QComboBox *box, const QStringList &items, const QString &fallback, auto label) {
        if (items.isEmpty())
            return;
        const auto keep = box->currentData().isValid() ? box->currentData().toString() : box->currentText();
        box->clear();
        for (const auto &item : items)
            box->addItem(label(item), item);
        const auto wanted = items.contains(keep) ? keep : fallback;
        box->setCurrentIndex(std::max(0, box->findData(wanted)));
    };
    const auto same = [](const QString &item) { return item; };
    fill(m_language, names("locales", QRegularExpression("^[A-Za-z][A-Za-z0-9_@.-]{0,63}$")), "en_US.UTF-8", languageName);
    fill(m_timeZone, names("timezones", QRegularExpression("^[A-Za-z0-9_+.-]+(/[A-Za-z0-9_+.-]+)*$")), "UTC", same);
    fill(m_keymap, names("keymaps", QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$")), "us", same);
}

void InstallerWindow::showDisks(const QVariantList &disks)
{
    QSignalBlocker quiet(m_disks);
    m_disks->clear();
    auto *model = qobject_cast<QStandardItemModel *>(m_disks->model());
    int first = -1;
    for (const auto &value : disks)
    {
        const auto disk = value.toMap();
        const bool boot = disk.value("is_boot_medium").toBool();
        // An empty CD drive or card reader (0 bytes) or a small stick can't take the installation.
        const bool small = disk.value("size_bytes").toLongLong() < minimumDiskBytes;
        const auto model_ = disk.value("model").toString().trimmed();
        // The path before the size: it says which disk is erased, so it must never be cut off.
        m_disks->addItem(QString("%1 (%3) — %2%4")
                             .arg(model_.isEmpty() ? QString("Disk") : model_, sizeText(disk.value("size_bytes").toLongLong()),
                                  disk.value("path").toString(),
                                  boot ? "  — the installation disk" : small ? "  — too small" : ""),
                         disk);
        // What can't be chosen is listed dimmed, as a Mac menu shows it.
        if ((boot || small) && model)
            model->item(m_disks->count() - 1)->setEnabled(false);
        else if (first < 0)
            first = m_disks->count() - 1;
    }
    // As in 9.2.2, a disk is chosen to begin with: the first one that can be installed on.
    m_disks->setCurrentIndex(first);
    describeDisk();
    if (!m_disks->count())
        m_destinationNote->setText("No disk was found to install on.");
    goTo(m_page);
}

void InstallerWindow::describeDisk()
{
    const auto disk = m_disks->currentData().toMap();
    if (disk.isEmpty())
    {
        m_diskContents->clear();
        m_diskSize->clear();
        m_destinationNote->setText(m_disks->count() ? "There is no disk here that Platinum Reality can be installed on: "
                                                      "it needs one of at least 16 GB, other than the installation disk."
                                                    : QString());
        return;
    }
    const auto partitions = disk.value("partitions").toList();
    m_diskContents->setText(partitions.isEmpty() ? QString("Nothing")
                            : partitions.size() == 1 ? QString("1 partition")
                                                     : QString::number(partitions.size()) + " partitions");
    const auto bytes = disk.value("size_bytes").toLongLong();
    m_diskSize->setText(sizeText(bytes));
    m_destinationNote->setText(bytes < minimumDiskBytes
                                   ? "This disk is too small. Platinum Reality needs at least 16 GB."
                                   : "This disk has enough space for Platinum Reality. Everything on it will be "
                                     "erased when you click Start.");
}

void InstallerWindow::showOptions()
{
    if (m_page != Destination)
        return;
    const bool before = m_encrypt->isChecked();
    if (m_optionsDialog->exec() != QDialog::Accepted)
        m_encrypt->setChecked(before);
}

void InstallerWindow::saveText()
{
    auto *browser = m_page == License ? m_license : m_information;
    const auto path = QFileDialog::getSaveFileName(this, "Save", QDir::homePath() + "/" +
                                                   (m_page == License ? "License.txt" : "Important Information.txt"),
                                                   QString(), nullptr, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty())
        return;
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(browser->toPlainText().toUtf8()) < 0)
        QMessageBox::warning(this, "Save", "The text could not be saved there.");
}

void InstallerWindow::goTo(Page page)
{
    const bool changed = page != m_page;
    m_page = page;
    m_pages->setCurrentIndex(page);
    static_cast<PageArea *>(m_pages)->watermark = page == Welcome;
    m_pages->update();
    m_title->setText(titles[page]);
    static_cast<TitleBand *>(m_band)->step = page;
    m_band->update();
    m_licenseLanguage->setVisible(page == License);
    m_options->setVisible(page == Destination);
    m_save->setVisible(page == Information || page == License);
    m_print->setVisible(page == Information || page == License);
    // While installing and after, the buttons stay where they were, dimmed, as in 9.2.2.
    const bool installing = page >= Progress;
    m_back->setVisible(true);
    m_back->setEnabled(page > Welcome && !installing);
    m_continue->setText(page == Destination ? "Select" : page >= Install ? "Start" : "Continue");
    bool ready = !installing;
    if (page == Destination)
        ready = !chosenDisk().isEmpty() && !m_destinationNote->text().contains("too small");
    if (page == Account)
        ready = accountProblem().isEmpty();
    if (page == Install || installing)
    {
        const auto disk = diskName();
        m_installSummary->setText("Click Start to install Platinum Reality on “" + disk + ".”\n\nEverything on this "
                                  "disk will be erased. Your account “" + m_userName->text() +
                                  "” will be created, and the computer will be called “" + m_computerName->text() +
                                  "”." +
                                  (m_encrypt->isChecked() ? " The disk will be encrypted: your password unlocks it "
                                                            "each time the computer starts up."
                                                          : QString()));
        m_progressHeading->setText("Click Start to install Platinum Reality on “" + disk + ".”");
    }
    m_continue->setEnabled(ready && !m_engine->busy());
    if (changed && page == Destination && m_disks->count() == 0)
        m_engine->listDisks();
    if (changed)
        emit pageChanged(page);
}

void InstallerWindow::goBack()
{
    if (m_page > Welcome && m_page < Progress)
        goTo(Page(m_page - 1));
}

bool InstallerWindow::ask(const QString &title, const QString &text, const QString &accept, const QString &reject,
                          bool agree)
{
    m_lastQuestion = text;
    if (m_hasTestAnswers)
        return agree ? m_testAnswers.agree : m_testAnswers.erase;
    QMessageBox box(agree ? QMessageBox::Information : QMessageBox::Warning, title, text, QMessageBox::NoButton, this);
    // The KDE platform helper rebuilds custom buttons and loses our default.
    box.setOption(QMessageBox::Option::DontUseNativeDialog);
    box.setTextFormat(Qt::PlainText);
    // In 9.2.2's order: the way back first, then the way on.
    auto *no = box.addButton(reject, QMessageBox::RejectRole);
    auto *yes = box.addButton(accept, QMessageBox::AcceptRole);
    // Dialog push buttons are auto-default in Qt: focusing Erase can otherwise
    // replace the deliberate Cancel default. Return follows the explicit ring;
    // Space/click still activates the focused choice.
    yes->setAutoDefault(false);
    no->setAutoDefault(false);
    // Agreeing to a license is the normal path; erasing a disk defaults to Cancel.
    box.setDefaultButton(agree ? yes : no);
    box.setEscapeButton(no);
    (agree ? yes : no)->setFocus(Qt::OtherFocusReason);
    box.exec();
    return box.clickedButton() == yes;
}

void InstallerWindow::goForward()
{
    switch (m_page)
    {
    case License:
        m_agreed = m_agreed || ask("Software License Agreement",
                                   "To continue with installing the software you must agree to the terms of the "
                                   "software license agreement.\n\nClick Agree to continue, or click Disagree to "
                                   "cancel the installation.",
                                   "Agree", "Disagree", true);
        if (!m_agreed)
            return;
        goTo(Account);
        return;
    case Install:
        // Planning reads the disk and changes nothing; the erase question follows with its result.
        m_engine->plan(config());
        m_continue->setEnabled(false);
        return;
    case Progress:
    case Finished:
        return;
    default:
        goTo(Page(m_page + 1));
    }
}

QString InstallerWindow::eraseQuestion(const QJsonObject &plan) const
{
    // Plain text: partition labels come from the disk and are shown as written.
    auto text = "Erase “" + diskName() + "” and install Platinum Reality? Everything on this disk will be lost.";
    const auto partitions = plan.value("existing_partitions").toArray();
    if (!partitions.isEmpty())
    {
        text += "\n\nThis disk has " + QString::number(partitions.size()) +
                (partitions.size() == 1 ? " partition" : " partitions") + " now:";
        for (const auto &value : partitions)
        {
            const auto partition = value.toObject();
            const auto label = partition.value("label").toString();
            const auto type = partition.value("fstype").toString();
            text += "\n  " + partition.value("path").toString() + "  " +
                    sizeText(partition.value("size_bytes").toInteger()) + (type.isEmpty() ? QString() : "  " + type) +
                    (label.isEmpty() ? QString() : "  “" + label + "”");
        }
    }
    const auto warnings = plan.value("warnings").toArray();
    for (const auto &warning : warnings)
        if (!warning.toString().isEmpty())
            text += "\n\n" + warning.toString();
    return text;
}

void InstallerWindow::startInstall()
{
    m_diskChanged = false;
    m_installClock.invalidate();
    m_progressText->setText("Installing Platinum Reality onto “" + diskName() + "”");
    goTo(Progress);
    QJsonObject secrets{{"password", m_password->text()}};
    if (m_encrypt->isChecked())
        secrets.insert("passphrase", m_password->text());
    m_engine->install(config(), secrets, config().value("disk").toString());
    m_password->clear();
    m_verify->clear();
}
