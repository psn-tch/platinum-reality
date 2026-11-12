#pragma once
#include "catalog.hpp"
#include <QMainWindow>

class ActivityWindow;
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
    void showAurPackages();
    bool start(const QStringList &arguments, const QString &doing);
    void changeProgress(const QString &text, int step, int steps);
    void changeFinished(bool ok, const QString &error, bool cancelled);
    bool ask(const QString &text, const QString &detail, const QString &action, bool caution);
    void alert(const QString &text, const QString &detail = {});
    const Package *chosenPackage() const;
    const Application *chosenApplication() const;
    QString desktopFile(const Application *app) const;
    void updateActions();
    void showActivity();

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
    ActivityWindow *m_activity;
    QPushButton *m_activityButton;
    QAction *m_installAction, *m_removeAction, *m_openAction, *m_updateAllAction, *m_yayAction, *m_aurAction,
        *m_activityAction;
    QString m_keepSelected, m_doing;
    // A search found nothing here but the AUR may have it (Lily Hewitt, 2026-11-12: she kept
    // downloading the AUR list because searches never looked in it).
    bool m_loaded = false, m_restartAdvised = false, m_offerAur = false;
    QList<bool> m_testAnswers;
    QString m_lastQuestion, m_lastAlert;
};
