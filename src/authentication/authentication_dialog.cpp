#include "authentication_dialog.hpp"
#include <algorithm>
#include <QComboBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyleOptionFrame>
#include <QVBoxLayout>

// The field for the answer. After a wrong password 9.2.2 shows the box again with the old password
// selected, so typing replaces it (Keychain Access, captured 2026-11-14). Here the field is empty and
// only draws that many selected bullets: nothing is kept, and Return on them sends nothing.
class PasswordField final : public QLineEdit
{
  public:
    int bullets() const { return m_bullets; }
    void showBullets(int count)
    {
        clear();
        m_bullets = std::clamp(count, 0, 32);
        setProperty("shownBullets", m_bullets); // for tests: what is drawn, never what was typed
        // Read-only hides the caret, as a selection does.
        setReadOnly(m_bullets > 0);
        update();
    }

  protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (m_bullets && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
            return event->accept();
        if (m_bullets && replacesSelection(event))
            showBullets(0);
        QLineEdit::keyPressEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        showBullets(0);
        QLineEdit::mousePressEvent(event);
    }
    void paintEvent(QPaintEvent *event) override
    {
        QLineEdit::paintEvent(event);
        if (!m_bullets || !text().isEmpty())
            return;
        QStyleOptionFrame option;
        initStyleOption(&option);
        auto area = style()->subElementRect(QStyle::SE_LineEditContents, &option, this).marginsRemoved(textMargins());
        area.adjust(2, 0, -2, 0); // QLineEdit's own margin before the text
        const QString shown(m_bullets, QChar(style()->styleHint(QStyle::SH_LineEdit_PasswordCharacter, &option, this)));
        const QFontMetrics metrics(font());
        auto selection = QRect(area.left(), area.center().y() - metrics.height() / 2 + 1,
                               metrics.horizontalAdvance(shown), metrics.height()) &
                         area;
        QPainter painter(this);
        painter.fillRect(selection, palette().color(QPalette::Highlight));
        painter.setPen(palette().color(QPalette::HighlightedText));
        painter.drawText(selection, Qt::AlignLeft | Qt::AlignVCenter, shown);
    }

  private:
    static bool replacesSelection(QKeyEvent *event)
    {
        if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete || event->matches(QKeySequence::Paste) ||
            event->matches(QKeySequence::Cut))
            return true;
        const auto typed = event->text();
        return !typed.isEmpty() && typed.front().isPrint() &&
               !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
    }
    int m_bullets = 0;
};

namespace
{
// 9.2.2's etched line between the message and the fields: grey over white.
class EtchedLine final : public QWidget
{
  public:
    EtchedLine() { setFixedHeight(2); }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(0, 0, width(), 1, QColor("#999999"));
        painter.fillRect(0, 1, width(), 1, Qt::white);
    }
};

// The box's icon, never empty: the keychain from the person's own 9.2.2 Keychain Access when the Asset
// Importer brought it in (as 9.2.2's Unlock box shows it), else polkit's icon or the theme's password
// icon, else a padlock drawn here (original artwork).
QPixmap boxIcon(const QString &iconName)
{
    const auto keychain = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                          "/platinum-reality/assets/icons/keychain.png";
    if (QFileInfo(keychain).isFile())
        if (const QPixmap imported(keychain); !imported.isNull())
            return imported.scaled(32, 32);
    const auto themed = QIcon::fromTheme(iconName, QIcon::fromTheme("dialog-password"));
    if (!themed.isNull())
        return themed.pixmap(32, 32);
    QPixmap drawn(32, 32);
    drawn.fill(Qt::transparent);
    QPainter painter(&drawn);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#222222"), 3));
    painter.drawArc(QRect(9, 3, 14, 18), 0, 180 * 16);
    painter.drawLine(9, 12, 9, 15);
    painter.drawLine(23, 12, 23, 15);
    painter.setPen(QPen(QColor("#222222"), 1.5));
    painter.setBrush(QColor("#d8b84a"));
    painter.drawRoundedRect(QRectF(5.5, 14.5, 21, 15), 2, 2);
    painter.setBrush(QColor("#222222"));
    painter.drawEllipse(QPointF(16, 21), 2, 2);
    painter.drawRect(QRectF(15.25, 21, 1.5, 5));
    return drawn;
}

QLabel *boldLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    auto font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}
} // namespace

// Laid out like 9.2.2's Unlock Keychain box, with Create Keychain's bold line beside the icon for
// polkit's message: icon and message, an etched line, bold labels right-aligned to Name and Password,
// then Cancel and the default OK. References: ~/platinum-922-reference/polkit-2026-11-14 on potato
// (not in the repository: Apple's artwork).
AuthenticationDialog::AuthenticationDialog(const QString &message, const QString &iconName,
                                           const QList<QPair<QString, QString>> &identities, const QString &selected,
                                           QWidget *parent)
    : QDialog(parent), m_identities(identities), m_identity(selected)
{
    setObjectName("authenticationDialog");
    setWindowTitle("Password");
    auto *layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetFixedSize); // a modal box, not resized
    layout->setContentsMargins(26, 18, 14, 14);
    layout->setSpacing(0);
    auto *top = new QHBoxLayout;
    top->setSpacing(18);
    auto *icon = new QLabel;
    icon->setObjectName("authenticationIcon");
    icon->setPixmap(boxIcon(iconName));
    icon->setFixedSize(32, 32);
    top->addWidget(icon, 0, Qt::AlignVCenter);
    auto *text = boldLabel(message.isEmpty() ? QString("An application needs an administrator's password.") : message);
    text->setObjectName("authenticationMessage");
    text->setWordWrap(true);
    text->setFixedWidth(300);
    top->addWidget(text, 1, Qt::AlignVCenter);
    layout->addLayout(top);
    layout->addSpacing(12);
    layout->addWidget(new EtchedLine);
    layout->addSpacing(14);
    auto *fields = new QGridLayout;
    fields->setContentsMargins(36, 0, 6, 0);
    fields->setHorizontalSpacing(8);
    fields->setVerticalSpacing(10);
    auto *nameLabel = boldLabel("Name:");
    fields->addWidget(nameLabel, 0, 0, Qt::AlignRight | Qt::AlignVCenter);
    if (identities.size() > 1)
    {
        m_names = new QComboBox;
        m_names->setObjectName("authenticationName");
        for (const auto &[identity, name] : identities)
            m_names->addItem(name, identity);
        m_names->setCurrentIndex(std::max(0, m_names->findData(selected)));
        connect(m_names, &QComboBox::activated, this, [this] {
            m_identity = m_names->currentData().toString();
            m_answer->showBullets(0);
            m_asking = false; // The new person's own conversation asks again.
            setWaiting(true);
            showMessage({}, false);
            emit identityChosen(m_identity);
        });
        fields->addWidget(m_names, 0, 1);
    }
    else
    {
        m_name = new QLabel(identities.isEmpty() ? QString() : identities.first().second);
        m_name->setObjectName("authenticationName");
        m_name->setTextFormat(Qt::PlainText);
        fields->addWidget(m_name, 0, 1);
    }
    m_prompt = boldLabel("Password:");
    m_prompt->setObjectName("authenticationPrompt");
    fields->addWidget(m_prompt, 1, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_answer = new PasswordField;
    m_answer->setObjectName("authenticationPassword");
    m_answer->setEchoMode(QLineEdit::Password);
    m_answer->setFixedWidth(200);
    fields->addWidget(m_answer, 1, 1);
    m_status = new QLabel;
    m_status->setObjectName("authenticationStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setFixedWidth(200);
    m_status->hide();
    fields->addWidget(m_status, 2, 1);
    fields->setColumnStretch(1, 1);
    layout->addLayout(fields);
    layout->addSpacing(16);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(12);
    buttons->addStretch(1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("authenticationCancel");
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, &AuthenticationDialog::reject);
    buttons->addWidget(cancel);
    m_ok = new QPushButton("OK");
    m_ok->setObjectName("authenticationOK");
    m_ok->setDefault(true);
    connect(m_ok, &QPushButton::clicked, this, &AuthenticationDialog::send);
    connect(m_answer, &QLineEdit::returnPressed, this, &AuthenticationDialog::send);
    buttons->addWidget(m_ok);
    layout->addLayout(buttons);
    setWaiting(true);
}

void AuthenticationDialog::ask(const QString &prompt, bool echo)
{
    // PAM's "Password: " becomes the field's label; other questions keep PAM's words.
    auto label = prompt.trimmed();
    if (label.isEmpty() || label.compare("Password:", Qt::CaseInsensitive) == 0)
        label = "Password:";
    m_prompt->setText(label);
    m_answer->setEchoMode(echo ? QLineEdit::Normal : QLineEdit::Password);
    if (echo)
        m_answer->showBullets(0);
    m_asking = true;
    setWaiting(false);
    m_answer->setFocus();
}

void AuthenticationDialog::showMessage(const QString &text, bool error)
{
    m_status->setProperty("error", error);
    m_status->setText(text.trimmed());
    m_status->setVisible(!text.trimmed().isEmpty());
}

void AuthenticationDialog::rejectAnswer(const QString &why)
{
    // 9.2.2 says so in a stop alert over the box, then shows the box with the old password selected.
    m_answer->showBullets(m_sentLength);
    m_sentLength = 0;
    showMessage({}, false);
    const auto name = shownName();
    auto *alert = new QMessageBox(QMessageBox::Critical, windowTitle(),
                                  name.isEmpty() ? why
                                                 : QString("The password for “%1” failed authentication. "
                                                           "Please try again.")
                                                       .arg(name),
                                  QMessageBox::Ok, this);
    alert->setObjectName("authenticationFailed");
    alert->setAttribute(Qt::WA_DeleteOnClose);
    alert->setWindowModality(Qt::WindowModal);
    connect(alert, &QDialog::finished, m_answer, [this] { m_answer->setFocus(); });
    alert->open();
}

void AuthenticationDialog::setWaiting(bool waiting)
{
    m_answer->setEnabled(!waiting && m_asking);
    m_ok->setEnabled(!waiting && m_asking);
    if (m_names)
        m_names->setEnabled(!waiting || !m_asking);
}

QString AuthenticationDialog::selectedIdentity() const
{
    return m_identity;
}

QString AuthenticationDialog::shownName() const
{
    for (const auto &[identity, name] : m_identities)
        if (identity == m_identity)
            return name;
    return {};
}

void AuthenticationDialog::reject()
{
    m_answer->showBullets(0);
    emit canceled();
}

void AuthenticationDialog::send()
{
    // The selected bullets after a wrong password are only drawn: they are never an answer.
    if (!m_asking || !m_ok->isEnabled() || m_answer->bullets())
        return;
    m_asking = false;
    const auto response = m_answer->text();
    m_sentLength = int(response.size());
    // The field never holds the password after it is sent.
    m_answer->clear();
    setWaiting(true);
    emit answered(response);
}
