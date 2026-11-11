#pragma once
#include "catalog.hpp"
#include <QMainWindow>

class FolderHeader;
class PackageClient;
class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTextBrowser;
class QTimer;
class QTreeView;

// The App Store (docs/app-store.md): Mac OS 9 had none, so this is an original window in the
// Platinum style, built like Sherlock 2: what to show and what to find at the top, a Finder list of
// the software, and the chosen item below it with its Install, Remove and Open buttons.
class AppStoreWindow final : public QMainWindow
{
    Q_OBJECT
    friend class AppStoreTests;

  public:
    explicit AppStoreWindow(PackageClient *client, QWidget *parent = nullptr);
    // Tests answer the questions a person would, in order: true is the question's action button.
    void setTestAnswers(QList<bool> answers) { m_testAnswers = std::move(answers); }
    QString lastQuestion() const { return m_lastQuestion; }
    QString lastAlert() const { return m_lastAlert; }

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    struct ShowItem
    {
        QString text;
        CatalogModel::View view;
        QString category;
    };
    void buildMenus();
    void catalogLoaded(const Catalog &catalog);
    void showChosen(int index);
    void refreshCount();
    void describe();
    void install();
    void installAppCatalog();
    void remove();
    void update();
    void open();
    void updateAll();
    void installYay();
    void refreshAur();
    bool start(const QStringList &arguments, const QString &doing);
    void changeProgress(const QString &text, int step, int steps);
    void changeFinished(bool ok, const QString &error, bool cancelled);
    bool ask(const QString &text, const QString &detail, const QString &action, bool caution);
    void alert(const QString &text, const QString &detail = {});
    const Package *chosenPackage() const;
    const Application *chosenApplication() const;
    QString desktopFile(const Application *app) const;
    void updateActions();

    PackageClient *m_client;
    CatalogModel *m_model;
    CatalogFilter *m_filter;
    QList<ShowItem> m_showItems;
    QComboBox *m_show;
    QLineEdit *m_find;
    QTimer *m_findDelay;
    FolderHeader *m_count;
    QTreeView *m_list;
    QLabel *m_icon, *m_title, *m_summary, *m_facts;
    QTextBrowser *m_about;
    QPushButton *m_primary, *m_open, *m_extra;
    QProgressBar *m_progress;
    QLabel *m_status;
    QAction *m_installAction, *m_removeAction, *m_openAction, *m_updateAllAction, *m_yayAction, *m_aurAction;
    QString m_keepSelected, m_doing;
    bool m_loaded = false, m_restartAdvised = false;
    QList<bool> m_testAnswers;
    QString m_lastQuestion, m_lastAlert;
};
