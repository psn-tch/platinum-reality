#include "app_store_window.hpp"
#include "activity_window.hpp"
#include "finder/list_view_parts.hpp"
#include "finder/platinum_style.hpp"
#include "package_client.hpp"
#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
const QString AppCatalogPackage = QStringLiteral("archlinux-appstream-data");

QString countText(int shown, int total, const QString &one, const QString &many)
{
    const QLocale english(QLocale::English);
    const auto noun = [&](int n) { return english.toString(n) + " " + (n == 1 ? one : many); };
    return shown == total ? noun(total) : english.toString(shown) + " of " + noun(total);
}

// Packages whose update only takes effect after a restart.
bool needsRestart(const QStringList &packages)
{
    for (const auto &name : packages)
        if (name.startsWith("linux") || name == "systemd" || name == "glibc" || name.startsWith("nvidia") ||
            name == "mesa" || name == "amd-ucode" || name == "intel-ucode" || name == "platinum-reality")
            return true;
    return false;
}
} // namespace

AppStoreWindow::AppStoreWindow(PackageClient *client, QWidget *parent)
    : QMainWindow(parent), m_client(client), m_model(new CatalogModel(this)), m_filter(new CatalogFilter(this))
{
    setWindowTitle("App Store");
    m_filter->setSourceModel(m_model);
    m_filter->setSortRole(CatalogModel::SortRole);
    m_filter->setSortCaseSensitivity(Qt::CaseInsensitive);

    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Show [pop-up] … Find [field]: what Sherlock's channels and its search field did.
    auto *top = new QWidget;
    auto *bar = new QHBoxLayout(top);
    bar->setContentsMargins(10, 8, 10, 8);
    bar->setSpacing(6);
    bar->addWidget(new QLabel("Show:"));
    m_show = new QComboBox;
    m_show->setObjectName("appStoreShow");
    m_show->setMinimumWidth(180);
    bar->addWidget(m_show);
    bar->addStretch(1);
    bar->addWidget(new QLabel("Find:"));
    m_find = new QLineEdit;
    m_find->setObjectName("appStoreFind");
    m_find->setClearButtonEnabled(true);
    m_find->setMinimumWidth(200);
    bar->addWidget(m_find);
    layout->addWidget(top);

    m_count = new FolderHeader(this);
    m_count->setText("Reading the software lists…");
    layout->addWidget(m_count);

    m_list = new QTreeView;
    m_list->setObjectName("appStoreList");
    // No disclosure triangles here: Name starts over the icon, at the rows' own text.
    m_list->setHeader(new FolderColumnHeader(m_list, 26));
    m_list->setItemDelegate(new FolderListDelegate(m_list));
    m_list->setModel(m_filter);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setIconSize({16, 16});
    m_list->setSortingEnabled(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->header()->setFixedHeight(qMax(21, fontMetrics().height() + 4));
    m_list->header()->setSectionsMovable(false);
    m_list->sortByColumn(CatalogModel::Name, Qt::AscendingOrder);
    connect(m_list->header(), &QHeaderView::sortIndicatorChanged, m_list->viewport(),
            qOverload<>(&QWidget::update));

    // The chosen item: its icon, name, what it is, and what can be done with it.
    auto *details = new QWidget;
    details->setObjectName("appStoreDetails");
    auto *detailsLayout = new QHBoxLayout(details);
    detailsLayout->setContentsMargins(12, 10, 12, 10);
    detailsLayout->setSpacing(12);
    m_icon = new QLabel;
    m_icon->setFixedSize(48, 48);
    m_icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    detailsLayout->addWidget(m_icon, 0, Qt::AlignTop);
    auto *text = new QVBoxLayout;
    text->setSpacing(3);
    m_title = new QLabel;
    m_title->setObjectName("appStoreTitle");
    auto titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1);
    m_title->setFont(titleFont);
    m_title->setTextFormat(Qt::PlainText);
    m_summary = new QLabel;
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    m_facts = new QLabel;
    m_facts->setObjectName("appStoreFacts");
    m_facts->setTextFormat(Qt::PlainText);
    m_facts->setWordWrap(true);
    m_about = new QTextBrowser;
    m_about->setObjectName("appStoreAbout");
    m_about->setOpenExternalLinks(true);
    m_about->setMinimumHeight(60);
    text->addWidget(m_title);
    text->addWidget(m_summary);
    text->addWidget(m_facts);
    text->addWidget(m_about, 1);
    detailsLayout->addLayout(text, 1);
    auto *buttons = new QVBoxLayout;
    buttons->setSpacing(8);
    m_primary = new QPushButton("Install");
    m_primary->setObjectName("appStorePrimary");
    m_open = new QPushButton("Open");
    m_open->setObjectName("appStoreOpen");
    m_extra = new QPushButton("Remove…");
    m_extra->setObjectName("appStoreExtra");
    for (auto *button : {m_primary, m_open, m_extra})
    {
        button->setMinimumWidth(96);
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    detailsLayout->addLayout(buttons);

    auto *split = new QSplitter(Qt::Vertical);
    split->setChildrenCollapsible(false);
    split->addWidget(m_list);
    split->addWidget(details);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);

    // The work under way, as in the Installer: a line of what is happening, and a bar.
    auto *working = new QWidget;
    auto *workingLayout = new QHBoxLayout(working);
    workingLayout->setContentsMargins(10, 4, 10, 6);
    m_status = new QLabel;
    m_status->setObjectName("appStoreStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setMinimumWidth(100);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_progress = new QProgressBar;
    m_progress->setObjectName("appStoreProgress");
    m_progress->setTextVisible(false);
    m_progress->setFixedWidth(180);
    // Activity…: every line of the work, as its programs write it.
    m_activity = new ActivityWindow(this);
    m_activityButton = new QPushButton("Activity…");
    m_activityButton->setObjectName("appStoreActivityButton");
    connect(m_activityButton, &QPushButton::clicked, this, &AppStoreWindow::showActivity);
    workingLayout->addWidget(m_status, 1);
    workingLayout->addWidget(m_progress);
    workingLayout->addWidget(m_activityButton);
    m_progress->hide();
    layout->addWidget(working);
    setCentralWidget(body);

    buildMenus();
    m_findDelay = new QTimer(this);
    m_findDelay->setSingleShot(true);
    m_findDelay->setInterval(200);
    connect(m_find, &QLineEdit::textChanged, m_findDelay, qOverload<>(&QTimer::start));
    connect(m_findDelay, &QTimer::timeout, this, [this] {
        m_filter->setSearch(m_find->text());
        refreshCount();
        if (!m_list->currentIndex().isValid())
            describe();
    });
    connect(m_show, &QComboBox::activated, this, &AppStoreWindow::showChosen);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, &AppStoreWindow::describe);
    connect(m_list, &QTreeView::doubleClicked, this, [this] {
        if (m_open->isEnabled())
            open();
    });
    connect(m_primary, &QPushButton::clicked, this, [this] {
        const auto *p = chosenPackage();
        if (!p && m_offerAur)
            m_model->catalog().aurUpdated.isEmpty() ? refreshAur() : showAurPackages();
        else if (!p && m_model->view() == CatalogModel::AurPackages)
            m_model->catalog().yay ? refreshAur() : installYay();
        else if (!p && m_model->view() == CatalogModel::Applications)
            installAppCatalog();
        else if (p && p->isInstalled() && !p->update.isEmpty())
            update();
        else
            install();
    });
    connect(m_open, &QPushButton::clicked, this, &AppStoreWindow::open);
    connect(m_extra, &QPushButton::clicked, this, &AppStoreWindow::remove);
    connect(m_client, &PackageClient::catalogLoaded, this, &AppStoreWindow::catalogLoaded);
    connect(m_client, &PackageClient::catalogFailed, this, [this](const QString &error) {
        m_count->setText("The software lists could not be read.");
        alert("The software lists could not be read.", error);
    });
    connect(m_client, &PackageClient::progress, this, &AppStoreWindow::changeProgress);
    connect(m_client, &PackageClient::output, m_activity, &ActivityWindow::line);
    connect(m_client, &PackageClient::finished, this, &AppStoreWindow::changeFinished);

    m_showItems.append({"All Applications", CatalogModel::Applications, {}});
    for (const auto &category : catalogCategories())
        m_showItems.append({category, CatalogModel::Applications, category});
    m_showItems.append({{}, CatalogModel::Applications, {}});
    m_showItems.append({"All Packages", CatalogModel::AllPackages, {}});
    m_showItems.append({"AUR Packages", CatalogModel::AurPackages, {}});
    m_showItems.append({{}, CatalogModel::Applications, {}});
    m_showItems.append({"Installed Software", CatalogModel::Installed, {}});
    m_showItems.append({"Updates", CatalogModel::Updates, {}});
    for (const auto &item : m_showItems)
        item.text.isEmpty() ? m_show->insertSeparator(m_show->count()) : m_show->addItem(item.text);

    m_list->setColumnWidth(CatalogModel::Name, 260);
    m_list->setColumnWidth(CatalogModel::Status, 74);
    m_list->setColumnWidth(CatalogModel::Version, 110);
    m_list->setColumnWidth(CatalogModel::Size, 70);
    resize(760, 600);
    describe();
    m_client->loadCatalog();
}

void AppStoreWindow::buildMenus()
{
    auto *file = menuBar()->addMenu("File");
    auto *close = file->addAction("Close Window");
    setClassicShortcuts(close, QKeySequence::Close);
    connect(close, &QAction::triggered, this, &QWidget::close);
    auto *quit = file->addAction("Quit App Store");
    setClassicShortcuts(quit, QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    auto *edit = menuBar()->addMenu("Edit");
    auto *find = edit->addAction("Find…");
    setClassicShortcuts(find, QKeySequence::Find);
    connect(find, &QAction::triggered, this, [this] {
        m_find->setFocus(Qt::ShortcutFocusReason);
        m_find->selectAll();
    });
    auto *software = menuBar()->addMenu("Software");
    m_installAction = software->addAction("Install", this, [this] { m_primary->click(); });
    m_removeAction = software->addAction("Remove…", this, &AppStoreWindow::remove);
    m_openAction = software->addAction("Open", this, &AppStoreWindow::open);
    setClassicShortcuts(m_openAction, QKeySequence(Qt::CTRL | Qt::Key_O));
    software->addSeparator();
    m_updateAllAction = software->addAction("Update All Software…", this, &AppStoreWindow::updateAll);
    software->addSeparator();
    m_yayAction = software->addAction("Install yay…", this, &AppStoreWindow::installYay);
    m_aurAction = software->addAction("Download the AUR List", this, &AppStoreWindow::refreshAur);
    software->addSeparator();
    m_activityAction = software->addAction("Show Activity", this, &AppStoreWindow::showActivity);
    setClassicShortcuts(m_activityAction, QKeySequence(Qt::CTRL | Qt::Key_L));
}

void AppStoreWindow::showActivity()
{
    m_activity->show();
    m_activity->raise();
    m_activity->activateWindow();
}

void AppStoreWindow::catalogLoaded(const Catalog &catalog)
{
    const auto *chosen = chosenPackage();
    const auto keep = !m_keepSelected.isEmpty() ? m_keepSelected : chosen ? chosen->name : QString();
    m_keepSelected.clear();
    m_model->setCatalog(catalog);
    m_loaded = true;
    refreshCount();
    if (!keep.isEmpty())
    {
        const auto found = m_filter->match(m_filter->index(0, 0), CatalogModel::PackageRole, keep, 1,
                                           Qt::MatchExactly);
        if (!found.isEmpty())
        {
            m_list->setCurrentIndex(found.first());
            m_list->scrollTo(found.first());
        }
    }
    describe();
}

void AppStoreWindow::showChosen(int index)
{
    if (index < 0 || index >= m_showItems.size() || m_showItems[index].text.isEmpty())
        return;
    const auto &item = m_showItems[index];
    m_model->setView(item.view);
    m_filter->setCategory(item.category);
    refreshCount();
    describe();
    // Big lists start at the top; the AUR list is only downloaded when someone asks for it.
    m_list->scrollToTop();
}

void AppStoreWindow::refreshCount()
{
    if (!m_loaded)
        return;
    const int shown = m_filter->rowCount(), total = [this] {
        int n = 0;
        const auto category = m_showItems.value(m_show->currentIndex()).category;
        for (int row = 0; row < m_model->rowCount(); ++row)
            n += category.isEmpty() || m_model->index(row, 0).data(CatalogModel::CategoryRole) == category;
        return n;
    }();
    const auto view = m_model->view();
    m_count->setText(view == CatalogModel::Applications ? countText(shown, total, "application", "applications")
                     : view == CatalogModel::Updates    ? countText(shown, total, "update", "updates")
                                                        : countText(shown, total, "package", "packages"));
}

const Package *AppStoreWindow::chosenPackage() const
{
    const auto index = m_list->currentIndex();
    return index.isValid() ? m_model->package(m_filter->mapToSource(index)) : nullptr;
}

const Application *AppStoreWindow::chosenApplication() const
{
    const auto index = m_list->currentIndex();
    return index.isValid() ? m_model->application(m_filter->mapToSource(index)) : nullptr;
}

QString AppStoreWindow::desktopFile(const Application *app) const
{
    if (!app || app->launch.isEmpty() || app->launch.contains('/'))
        return {};
    return QStandardPaths::locate(QStandardPaths::ApplicationsLocation, app->launch);
}

void AppStoreWindow::describe()
{
    const auto *p = chosenPackage();
    const auto *app = chosenApplication();
    const auto &catalog = m_model->catalog();
    m_extra->setText("Remove…");
    if (!p)
    {
        m_icon->clear();
        m_facts->clear();
        m_about->clear();
        m_open->hide();
        m_extra->hide();
        m_primary->hide();
        m_offerAur = false;
        const bool nothingFound = m_loaded && m_model->view() != CatalogModel::AurPackages && catalog.yay &&
                                  !m_filter->words().isEmpty() && m_filter->rowCount() == 0;
        const int aurMatches = nothingFound ? m_model->aurMatches(m_filter->words()) : 0;
        if (m_loaded && m_model->view() == CatalogModel::AurPackages && !catalog.yay)
        {
            m_title->setText("AUR Packages");
            m_summary->setText("The Arch User Repository has thousands more programs. The App Store installs "
                               "them with yay, which isn't installed yet.");
            m_about->setPlainText("Anyone can upload to the AUR, and nobody checks what is uploaded. Install "
                                  "only software you trust.");
            m_primary->setText("Install yay…");
            m_primary->show();
        }
        else if (m_loaded && m_model->view() == CatalogModel::AurPackages && m_model->rowCount() == 0)
        {
            m_title->setText("AUR Packages");
            m_summary->setText("The list of AUR packages hasn't been downloaded yet (about 10 MB).");
            m_primary->setText("Download");
            m_primary->show();
        }
        else if (nothingFound && (aurMatches > 0 || catalog.aurUpdated.isEmpty()))
        {
            // Searches look in the list showing; the AUR is a list of its own.
            m_offerAur = true;
            m_title->setText("Not in this list");
            const auto search = "“" + m_find->text().simplified() + "”";
            if (catalog.aurUpdated.isEmpty())
            {
                m_summary->setText("The AUR may have " + search + ". Its list of packages hasn't been "
                                   "downloaded yet (about 10 MB).");
                m_primary->setText("Download the AUR List");
            }
            else
            {
                m_summary->setText(QString("%1 AUR %2 %3.")
                                       .arg(aurMatches)
                                       .arg(aurMatches == 1 ? "package matches" : "packages match", search));
                m_primary->setText("Show AUR Packages");
            }
            m_primary->show();
        }
        else if (m_loaded && m_model->view() == CatalogModel::Applications && catalog.apps.isEmpty())
        {
            // Update 45 made archlinux-appstream-data optional so Software Update needn't stop for it.
            m_title->setText("Applications");
            m_summary->setText("Application names, pictures and categories come from Arch's application "
                               "catalog, which isn't installed. Every package is still under All Packages.");
            m_primary->setText("Install Catalog…");
            m_primary->setVisible(catalog.indexOf(AppCatalogPackage) >= 0);
        }
        else
        {
            m_title->setText(m_loaded ? "Choose something in the list." : "App Store");
            m_summary->setText(m_loaded ? "Its description appears here, with buttons to install, open or "
                                          "remove it."
                                        : "Reading the software lists…");
        }
        updateActions();
        return;
    }
    const QIcon icon = m_model->icon(*p, app);
    m_icon->setPixmap(icon.pixmap(QSize(48, 48), devicePixelRatioF()));
    m_title->setText(app ? app->title : p->name);
    m_summary->setText(app && !app->summary.isEmpty() ? app->summary : p->description);
    QStringList facts;
    facts << (app ? "Package " + p->name + " " + p->version : "Version " + p->version);
    if (p->size > 0)
        facts << classicSize(p->size);
    facts << sourceName(*p);
    if (p->isInstalled())
        facts << (p->update.isEmpty() ? "Installed" : "Installed: " + p->installed + ", update: " + p->update);
    if (p->fromAur())
    {
        facts << QLocale(QLocale::English).toString(p->votes) + (p->votes == 1 ? " vote" : " votes");
        if (p->outOfDate)
            facts << "Flagged out of date";
        if (p->orphan)
            facts << "No maintainer";
    }
    m_facts->setText(facts.join("  •  "));
    QString about = app && !app->about.isEmpty() ? app->about.toHtmlEscaped() : p->description.toHtmlEscaped();
    about.replace("\n", "<br>");
    const auto homepage = app && !app->homepage.isEmpty() ? app->homepage : p->url;
    if (homepage.startsWith("https://") || homepage.startsWith("http://"))
        about += QString("<p><a href=\"%1\">%2</a></p>").arg(homepage.toHtmlEscaped(), homepage.toHtmlEscaped());
    if (p->fromAur())
        about += QString("<p><a href=\"https://aur.archlinux.org/packages/%1\">This package on the AUR</a></p>")
                     .arg(QString(QUrl::toPercentEncoding(p->name)));
    m_about->setHtml(about);
    m_primary->setText(p->isInstalled() ? "Update" : "Install");
    m_primary->setVisible(!p->isInstalled() || !p->update.isEmpty());
    m_open->setVisible(p->isInstalled() && !desktopFile(app).isEmpty());
    m_extra->setVisible(p->isInstalled());
    updateActions();
}

void AppStoreWindow::updateActions()
{
    const bool idle = !m_client->busy();
    const auto *p = chosenPackage();
    for (auto *button : {m_primary, m_open, m_extra})
        button->setEnabled(idle);
    m_show->setEnabled(m_loaded);
    m_installAction->setEnabled(idle && p && (!p->isInstalled() || !p->update.isEmpty()));
    m_installAction->setText(p && p->isInstalled() ? "Update" : "Install");
    m_removeAction->setEnabled(idle && p && p->isInstalled());
    m_openAction->setEnabled(idle && m_open->isVisible());
    m_updateAllAction->setEnabled(idle && m_loaded);
    m_yayAction->setEnabled(idle && m_loaded && !m_model->catalog().yay);
    m_aurAction->setEnabled(idle && m_loaded && m_model->catalog().yay);
    m_aurAction->setText(m_model->catalog().aurUpdated.isEmpty() ? "Download the AUR List" : "Update the AUR List");
}

bool AppStoreWindow::ask(const QString &text, const QString &detail, const QString &action, bool caution)
{
    m_lastQuestion = text;
    if (!m_testAnswers.isEmpty())
        return m_testAnswers.takeFirst();
    QMessageBox box(caution ? QMessageBox::Warning : QMessageBox::Question, "App Store", text, QMessageBox::NoButton,
                    this);
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(detail);
    // In 9.2.2's order: the way back first, then the action. Anything that can't be undone, or
    // that runs other people's code, defaults to Cancel.
    auto *cancel = box.addButton("Cancel", QMessageBox::RejectRole);
    auto *yes = box.addButton(action, QMessageBox::AcceptRole);
    box.setDefaultButton(caution ? cancel : yes);
    box.setEscapeButton(cancel);
    keepReturnOnTheDefault(box);
    box.exec();
    return box.clickedButton() == yes;
}

void AppStoreWindow::alert(const QString &text, const QString &detail)
{
    m_lastAlert = detail.isEmpty() ? text : text + "\n" + detail;
    if (!m_testAnswers.isEmpty() || qEnvironmentVariableIsSet("PLATINUM_APP_STORE_TEST"))
        return;
    QMessageBox box(QMessageBox::Warning, "App Store", text, QMessageBox::Ok, this);
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(detail);
    keepReturnOnTheDefault(box);
    box.exec();
}

bool AppStoreWindow::start(const QStringList &arguments, const QString &doing)
{
    if (!m_client->run(arguments))
        return false;
    m_doing = doing;
    m_activity->begin(doing);
    m_status->setText(doing + "…");
    m_progress->setRange(0, 0);
    m_progress->show();
    updateActions();
    return true;
}

void AppStoreWindow::install()
{
    const auto *p = chosenPackage();
    if (!p || m_client->busy())
        return;
    const auto *app = chosenApplication();
    const auto name = app ? app->title : p->name;
    m_keepSelected = p->name;
    if (p->fromAur())
    {
        if (!ask("Install “" + name + "” from the AUR?",
                 "AUR packages are made by other Arch users. Anyone can upload one, and nobody checks them, so "
                 "install only software you trust. It is built on this computer, which can take a while.",
                 "Install", true))
            return;
        start({"install-aur", p->name}, "Installing " + name);
        return;
    }
    start({"install", p->name}, "Installing " + name);
}

void AppStoreWindow::installAppCatalog()
{
    if (m_client->busy())
        return;
    start({"install", AppCatalogPackage}, "Installing the application catalog");
}

void AppStoreWindow::update()
{
    const auto *p = chosenPackage();
    if (!p || m_client->busy())
        return;
    m_keepSelected = p->name;
    if (p->fromAur())
        start({"install-aur", p->name}, "Updating " + p->name);
    else
        // Arch updates everything together: one package on its own can break the others.
        updateAll();
}

void AppStoreWindow::remove()
{
    const auto *p = chosenPackage();
    if (!p || !p->isInstalled() || m_client->busy())
        return;
    const auto *app = chosenApplication();
    const auto name = app ? app->title : p->name;
    if (!ask("Remove “" + name + "”?",
             "It is removed from this computer, with anything it brought along that nothing else needs. "
             "Your own documents and settings are kept.",
             "Remove", true))
        return;
    m_keepSelected = p->name;
    start({"remove", p->name}, "Removing " + name);
}

void AppStoreWindow::open()
{
    const auto file = desktopFile(chosenApplication());
    if (file.isEmpty())
        return;
    // gio launches it the way the Applications folder does, in this session.
    if (!QProcess::startDetached("gio", {"launch", file}))
        alert("The application could not be opened.");
}

void AppStoreWindow::updateAll()
{
    if (m_client->busy())
        return;
    if (!ask("Update all software?",
             "Every program, and the system itself, is brought up to date from Arch Linux. This can take a while; "
             "keep the computer on until it finishes.",
             "Update", false))
        return;
    start({"update-all"}, "Updating all software");
}

void AppStoreWindow::installYay()
{
    if (m_client->busy() || m_model->catalog().yay)
        return;
    if (!ask("Install yay?",
             "yay installs software from the AUR, the Arch User Repository. The App Store installs git and "
             "base-devel, then builds yay from the AUR.",
             "Install", false))
        return;
    start({"install-yay"}, "Installing yay");
}

void AppStoreWindow::showAurPackages()
{
    for (int index = 0; index < m_showItems.size(); ++index)
        if (!m_showItems[index].text.isEmpty() && m_showItems[index].view == CatalogModel::AurPackages)
        {
            m_show->setCurrentIndex(m_show->findText(m_showItems[index].text));
            showChosen(m_show->currentIndex());
            return;
        }
}

void AppStoreWindow::refreshAur()
{
    if (m_client->busy() || !m_model->catalog().yay)
        return;
    start({"refresh-aur"}, "Downloading the AUR list");
}

void AppStoreWindow::changeProgress(const QString &text, int step, int steps)
{
    m_activity->progress(step, steps);
    if (step > 0 && steps > 0)
    {
        m_progress->setRange(0, steps);
        m_progress->setValue(step);
        m_status->setText(text);
    }
    else
        m_status->setText(m_doing + ": " + text);
}

void AppStoreWindow::changeFinished(bool ok, const QString &error, bool cancelled)
{
    m_progress->hide();
    m_status->clear();
    m_activity->finish(ok, error, cancelled);
    const auto doing = m_doing;
    m_doing.clear();
    const auto touched = m_client->touched();
    updateActions();
    if (ok)
    {
        // yay is new: its list of AUR packages comes next, as part of the same request.
        if (doing == "Installing yay")
        {
            m_client->loadCatalog();
            QTimer::singleShot(0, this, [this] { start({"refresh-aur"}, "Downloading the AUR list"); });
            return;
        }
        // The list is what was asked for: show it, keeping any search.
        if (doing == "Downloading the AUR list")
            showAurPackages();
        if (needsRestart(touched))
            alert("Restart the computer to finish updating.",
                  "Some of the system's own software was updated. It is used from the next restart.");
    }
    else if (!cancelled)
        alert(doing.isEmpty() ? "The change could not be made." : doing + " didn’t work.", error);
    m_client->loadCatalog();
}

void AppStoreWindow::closeEvent(QCloseEvent *event)
{
    if (m_client->busy())
    {
        // pacman must not be stopped halfway through.
        alert("The App Store is still working.", "Wait until it has finished, then close it.");
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
