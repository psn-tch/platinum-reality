#include "installer_window.hpp"
#include "engine_client.hpp"
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QJsonArray>
#include <QLocale>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimeZone>

namespace
{
const char *const titles[] = {"Welcome", "Select Destination", "Important Information", "Software License Agreement",
                              "Your Account", "Install Software", "Installing", "Installation Complete"};

// The title band: a soft blue-grey gradient with the step's name, as in the Mac OS 9 Installer.
class TitleBand final : public QWidget
{
  public:
    explicit TitleBand(QLabel *title, QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(52);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(64, 0, 16, 0);
        title->setStyleSheet("font-weight: bold; font-size: 13pt;");
        layout->addWidget(title);
        layout->addStretch();
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QLinearGradient band(0, 0, width(), 0);
        band.setColorAt(0, QColor("#c9cbe6"));
        band.setColorAt(1, QColor("#eeeef6"));
        painter.fillRect(rect(), band);
        painter.setPen(QColor("#888888"));
        painter.drawLine(0, height() - 1, width(), height() - 1);
        // An original smiling computer, not Apple's art.
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF screen(16, 10, 34, 28);
        painter.setPen(QPen(QColor("#333333"), 1.5));
        painter.setBrush(QColor("#dddddd"));
        painter.drawRoundedRect(screen, 4, 4);
        painter.setBrush(QColor("#7f93b2"));
        painter.drawRect(screen.adjusted(5, 4, -5, -8));
        painter.setPen(QPen(Qt::white, 1.5));
        painter.drawPoint(QPointF(27, 20));
        painter.drawPoint(QPointF(39, 20));
        painter.drawArc(QRectF(27, 19, 12, 8), 200 * 16, 140 * 16);
    }
};

QString sizeText(qint64 bytes)
{
    const double gb = bytes / 1e9;
    return gb >= 1000 ? QString::number(gb / 1000, 'f', 1) + " TB" : QString::number(gb, 'f', gb < 10 ? 1 : 0) + " GB";
}
}

InstallerWindow::InstallerWindow(EngineClient *engine, QString informationText, QString licenseText, QWidget *parent)
    : QWidget(parent), m_engine(engine)
{
    setWindowTitle("Install Platinum Reality");
    setFixedSize(600, 440); // One size, like the Mac OS 9 Installer window.
    m_title = new QLabel;
    m_title->setObjectName("installer-title");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new TitleBand(m_title));
    m_pages = new QStackedWidget;
    m_pages->addWidget(welcomePage());
    m_pages->addWidget(destinationPage());
    m_pages->addWidget(textPage(&m_information, informationText));
    m_pages->addWidget(textPage(&m_license, licenseText));
    m_pages->addWidget(accountPage());
    m_pages->addWidget(installPage());
    m_pages->addWidget(progressPage());
    m_pages->addWidget(finishedPage());
    auto *body = new QVBoxLayout;
    body->setContentsMargins(20, 14, 20, 14);
    body->setSpacing(12); // The Platinum style's own spacing is 0, so text pages would touch the buttons.
    body->addWidget(m_pages, 1);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_back = new QPushButton("Go Back");
    m_back->setObjectName("installer-back");
    m_continue = new QPushButton("Continue");
    m_continue->setObjectName("installer-continue");
    m_continue->setDefault(true);
    m_restart = new QPushButton("Restart");
    m_restart->setObjectName("installer-restart");
    for (auto *button : {m_back, m_continue, m_restart})
    {
        button->setMinimumWidth(96);
        buttons->addWidget(button);
    }
    body->addLayout(buttons);
    layout->addLayout(body, 1);
    connect(m_back, &QPushButton::clicked, this, &InstallerWindow::goBack);
    connect(m_continue, &QPushButton::clicked, this, &InstallerWindow::goForward);
    connect(m_restart, &QPushButton::clicked, this, &InstallerWindow::restartRequested);
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
            m_finishedText->setText(message);
            goTo(Finished);
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
    });
    connect(m_engine, &EngineClient::installFinished, this, [this](bool ok, const QString &message) {
        m_installOk = ok;
        const auto disk = config().value("disk").toString();
        m_finishedText->setText(
            ok ? "Platinum Reality was installed. Remove the installation disk, then restart your computer to use it."
            : "The installation could not be completed. " + message + "\n\n" +
                  (m_diskChanged
                       ? "The installer had already started erasing " + disk + ", so what was on it before is gone, "
                         "and it can't start up Platinum Reality yet. Your other disks were not changed. You can quit "
                         "and install again on the same disk."
                       : "Nothing was changed: " + disk + " was not erased. You can quit and try again."));
        goTo(Finished);
    });
    goTo(Welcome);
}

namespace
{
QString languageName(const QString &locale)
{
    const QLocale language(locale.section('.', 0, 0).section('@', 0, 0));
    if (language.language() == QLocale::C)
        return locale;
    return QLocale::languageToString(language.language()) + " (" + QLocale::territoryToString(language.territory()) +
           ") \u2014 " + locale;
}
} // namespace

QWidget *InstallerWindow::welcomePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *intro = new QLabel("Welcome to the Platinum Reality installation program. This program will guide you through "
                             "the five steps necessary to install Platinum Reality.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    const QStringList steps{"Select a destination disk", "Read important information",
                            "Respond to the software license agreement", "Set up your account", "Install software"};
    auto *grid = new QGridLayout;
    grid->setContentsMargins(24, 8, 0, 8);
    grid->setHorizontalSpacing(12);
    for (int i = 0; i < steps.size(); ++i)
    {
        auto *number = new QLabel(QString::number(i + 1) + ".");
        number->setStyleSheet("font-family: serif; font-style: italic; font-size: 20pt;");
        auto *text = new QLabel(steps[i]);
        text->setStyleSheet("font-weight: bold;");
        grid->addWidget(number, i, 0, Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(text, i, 1, Qt::AlignLeft | Qt::AlignVCenter);
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
    layout->setContentsMargins(0, 0, 0, 0);
    auto *intro = new QLabel("Choose the disk where you want to install Platinum Reality. Everything on that disk "
                             "will be erased.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_disks = new QListWidget;
    m_disks->setObjectName("installer-disks");
    m_disks->setIconSize(QSize(24, 24));
    layout->addWidget(m_disks, 1);
    m_destinationNote = new QLabel;
    m_destinationNote->setObjectName("installer-destination-note");
    m_destinationNote->setWordWrap(true);
    layout->addWidget(m_destinationNote);
    connect(m_disks, &QListWidget::currentRowChanged, this, [this] { goTo(m_page); });
    return page;
}

void InstallerWindow::keyPressEvent(QKeyEvent *event)
{
    // Return presses the button with the ring, whichever control has focus, as in Mac OS 9 (Qt does
    // this only in dialogs). Space still presses the focused button. Install still asks first.
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
    layout->addWidget(*browser);
    return page;
}

QWidget *InstallerWindow::accountPage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);
    m_fullName = new QLineEdit;
    m_userName = new QLineEdit;
    m_password = new QLineEdit;
    m_verify = new QLineEdit;
    m_computerName = new QLineEdit("platinum");
    for (auto *field : {m_password, m_verify})
        field->setEchoMode(QLineEdit::Password);
    m_fullName->setObjectName("installer-full-name");
    m_userName->setObjectName("installer-user-name");
    m_password->setObjectName("installer-password");
    m_verify->setObjectName("installer-verify");
    m_computerName->setObjectName("installer-computer-name");
    m_timeZone = new QComboBox;
    m_timeZone->setObjectName("installer-time-zone");
    for (const auto &zone : QTimeZone::availableTimeZoneIds())
        if (zone.contains('/') && !zone.startsWith("Etc/"))
            m_timeZone->addItem(QString::fromLatin1(zone));
    const auto local = QString::fromLatin1(QTimeZone::systemTimeZoneId());
    m_timeZone->setCurrentIndex(std::max(0, m_timeZone->findText(local.isEmpty() ? "America/Denver" : local)));
    m_keymap = new QComboBox;
    m_keymap->setObjectName("installer-keymap");
    m_keymap->addItems({"us", "uk", "de", "fr", "es", "it", "dvorak", "colemak"});
    m_language = new QComboBox;
    m_language->setObjectName("installer-language");
    m_language->addItem(languageName("en_US.UTF-8"), "en_US.UTF-8");
    form->addRow("Your name:", m_fullName);
    form->addRow("Short name:", m_userName);
    form->addRow("Password:", m_password);
    form->addRow("Verify:", m_verify);
    form->addRow("Computer name:", m_computerName);
    form->addRow("Language:", m_language);
    form->addRow("Time zone:", m_timeZone);
    form->addRow("Keyboard:", m_keymap);
    // The disk is locked with the account password: one password to remember, asked for at startup.
    m_encrypt = new QCheckBox("Encrypt the disk (your password unlocks it at startup)");
    m_encrypt->setObjectName("installer-encrypt");
    m_encrypt->setChecked(true);
    form->addRow("", m_encrypt);
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
    layout->setContentsMargins(0, 0, 0, 0);
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
    layout->setContentsMargins(0, 24, 0, 0);
    m_progressText = new QLabel("Preparing to install…");
    m_progressText->setObjectName("installer-progress-text");
    m_progress = new QProgressBar;
    m_progress->setObjectName("installer-progress");
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    layout->addWidget(m_progressText);
    layout->addWidget(m_progress);
    layout->addStretch();
    return page;
}

QWidget *InstallerWindow::finishedPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_finishedText = new QLabel;
    m_finishedText->setObjectName("installer-finished-text");
    m_finishedText->setWordWrap(true);
    layout->addWidget(m_finishedText);
    layout->addStretch();
    return page;
}

QString InstallerWindow::accountProblem() const
{
    // The install engine's own rules (distro/installer-engine/engine.py), so nothing it would refuse
    // gets as far as the Install button.
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
    const auto disk = m_disks->currentItem() ? m_disks->currentItem()->data(Qt::UserRole).toString() : QString();
    return {{"version", 1},
            {"disk", disk},
            {"hostname", m_computerName->text()},
            {"timezone", m_timeZone->currentText()},
            {"locale", m_language->currentData().toString()},
            {"keymap", m_keymap->currentText()},
            {"user", QJsonObject{{"name", m_userName->text()}, {"full_name", m_fullName->text().trimmed()}}},
            {"encrypt", m_encrypt->isChecked()},
            {"network", "iwd-networkd"}};
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
    fill(m_timeZone, names("timezones", QRegularExpression("^[A-Za-z0-9_+.-]+(/[A-Za-z0-9_+.-]+)*$")), "America/Denver", same);
    fill(m_keymap, names("keymaps", QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$")), "us", same);
}

void InstallerWindow::showDisks(const QVariantList &disks)
{
    m_disks->clear();
    for (const auto &value : disks)
    {
        const auto disk = value.toMap();
        const bool boot = disk.value("is_boot_medium").toBool();
        const auto model = disk.value("model").toString().trimmed();
        auto *item = new QListWidgetItem(
            QString("%1 — %2 (%3)%4")
                .arg(model.isEmpty() ? QString("Disk") : model, sizeText(disk.value("size_bytes").toLongLong()),
                     disk.value("path").toString(), boot ? "  — the installation disk" : ""));
        item->setData(Qt::UserRole, disk.value("path"));
        if (boot)
            item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
        m_disks->addItem(item);
    }
    m_destinationNote->setText(m_disks->count() ? QString() : QString("No disk was found to install on."));
    goTo(m_page);
}

void InstallerWindow::goTo(Page page)
{
    const bool changed = page != m_page;
    m_page = page;
    m_pages->setCurrentIndex(page);
    m_title->setText(titles[page]);
    m_back->setVisible(page < Progress);
    m_back->setEnabled(page > Welcome);
    m_continue->setVisible(page < Progress);
    m_continue->setText(page == Install ? "Install" : "Continue");
    m_restart->setVisible(page == Finished && m_installOk);
    bool ready = true;
    if (page == Destination)
        ready = m_disks->currentItem() && (m_disks->currentItem()->flags() & Qt::ItemIsEnabled);
    if (page == Account)
    {
        const auto problem = accountProblem();
        ready = problem.isEmpty();
    }
    if (page == Install)
    {
        const auto disk = m_disks->currentItem() ? m_disks->currentItem()->text() : QString();
        m_installSummary->setText("Platinum Reality will be installed on:\n\n    " + disk +
                                  "\n\nEverything on this disk will be erased. Your account “" + m_userName->text() +
                                  "” will be created, and the computer will be called “" + m_computerName->text() +
                                  "”." +
                                  (m_encrypt->isChecked() ? " The disk will be encrypted: your password unlocks it "
                                                            "each time the computer starts up."
                                                          : QString()) +
                                  "\n\nClick Install to begin.");
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
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton, this);
    // The KDE platform helper rebuilds custom buttons and loses our default.
    box.setOption(QMessageBox::Option::DontUseNativeDialog);
    box.setTextFormat(Qt::PlainText);
    auto *yes = box.addButton(accept, QMessageBox::AcceptRole);
    auto *no = box.addButton(reject, QMessageBox::RejectRole);
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
                                   "To continue installing, you must agree to the terms of the software license "
                                   "agreement. Click Agree to continue, or Disagree to go back.",
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
    auto text = "Erase “" + m_disks->currentItem()->text() +
                "” and install Platinum Reality? Everything on this disk will be lost.";
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
    goTo(Progress);
    QJsonObject secrets{{"password", m_password->text()}};
    if (m_encrypt->isChecked())
        secrets.insert("passphrase", m_password->text());
    m_engine->install(config(), secrets, config().value("disk").toString());
    m_password->clear();
    m_verify->clear();
}
