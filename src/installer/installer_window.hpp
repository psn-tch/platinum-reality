#pragma once
#include <QJsonObject>
#include <QVariantList>
#include <QWidget>

#include <QElapsedTimer>

class EngineClient;
class QDialog;
class QKeyEvent;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTextBrowser;

// Install Platinum Reality, as faithful to the Mac OS 9.2.2 Installer as an original can be: the band
// naming the step, a page, and the step's buttons. Welcome lists the steps; then Select Destination
// (a Destination Disk pop-up, Options… for encryption), Important Information and the license (Save…,
// Print…, then Agree / Disagree), the account, and Install Software, whose Start erases the chosen disk
// only after a question whose default button is Cancel. It ends with 9.2.2's finished alert.
// All disk work is done by the install engine (EngineClient).
class InstallerWindow final : public QWidget
{
    Q_OBJECT
    friend class InstallerTests; // Exercises the real modal question, without preset answers.
  public:
    enum Page { Welcome, Destination, Information, License, Account, Install, Progress, Finished };
    Q_ENUM(Page)
    explicit InstallerWindow(EngineClient *engine, QString informationText, QString licenseText,
                             QWidget *parent = nullptr);
    Page page() const { return m_page; }
    // The settings the engine receives (no passwords).
    QJsonObject config() const;
    // Valid account fields, or the reason they are not.
    QString accountProblem() const;
    // Tests answer the questions a person would.
    void setConfirmationAnswers(bool agreeToLicense, bool eraseDisk)
    {
        m_testAnswers = {agreeToLicense, eraseDisk};
        m_hasTestAnswers = true;
    }
    // The text of the last question asked (license or erase), as people read it.
    QString lastQuestion() const { return m_lastQuestion; }

  signals:
    void pageChanged(InstallerWindow::Page page);
    void restartRequested();

  protected:
    void keyPressEvent(QKeyEvent *event) override;

  private:
    void goTo(Page page);
    void goBack();
    void goForward();
    bool ask(const QString &title, const QString &text, const QString &accept, const QString &reject, bool agree);
    void applyChoices(const QJsonObject &choices);
    void showDisks(const QVariantList &disks);
    QString eraseQuestion(const QJsonObject &plan) const;
    void startInstall();
    void finish(const QString &message);
    QString remainingText(double fraction);
    QString chosenDisk() const;
    QString diskName() const;
    bool diskUsable(int index) const;
    void describeDisk();
    void showOptions();
    void saveText();
    QWidget *welcomePage();
    QWidget *destinationPage();
    QWidget *textPage(QTextBrowser **browser, const QString &text);
    QWidget *accountPage();
    QWidget *installPage();
    QWidget *progressPage();
    QWidget *finishedPage();
    EngineClient *m_engine;
    Page m_page = Welcome;
    QStackedWidget *m_pages = nullptr;
    QLabel *m_title = nullptr, *m_destinationNote = nullptr, *m_installSummary = nullptr;
    QLabel *m_progressText = nullptr, *m_finishedText = nullptr, *m_progressHeading = nullptr, *m_remaining = nullptr,
           *m_finishedSummary = nullptr, *m_diskContents = nullptr, *m_diskSize = nullptr, *m_diskRequired = nullptr;
    QWidget *m_band = nullptr;
    QComboBox *m_disks = nullptr, *m_licenseLanguage = nullptr;
    QDialog *m_optionsDialog = nullptr, *m_finishedAlert = nullptr;
    QElapsedTimer m_installClock;
    QTextBrowser *m_information = nullptr, *m_license = nullptr;
    QLineEdit *m_fullName = nullptr, *m_userName = nullptr, *m_password = nullptr, *m_verify = nullptr,
              *m_computerName = nullptr;
    QComboBox *m_timeZone = nullptr, *m_keymap = nullptr, *m_language = nullptr;
    QCheckBox *m_encrypt = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_back = nullptr, *m_continue = nullptr, *m_restart = nullptr, *m_quit = nullptr, *m_options = nullptr,
                *m_save = nullptr, *m_print = nullptr;
    bool m_choicesAsked = false;
    bool m_agreed = false, m_userNameEdited = false, m_installOk = false, m_diskChanged = false;
    struct Answers
    {
        bool agree, erase;
    } m_testAnswers{};
    bool m_hasTestAnswers = false;
    QString m_lastQuestion;
};
