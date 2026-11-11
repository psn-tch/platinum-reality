#include "appearance/interface_font.hpp"
#include "appstore/app_store_window.hpp"
#include "appstore/package_client.hpp"
#include "finder/list_view_parts.hpp"
#include "finder/platinum_style.hpp"
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTreeView>
#include <QtTest>

// The App Store window against a stand-in helper, tests/fixtures/app-store/platinum-packages (the real
// one's tests are platinum_packages_tests.py): it keeps a small catalog in a file, changes it as installs
// and removals happen, and logs each request.
namespace
{
QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(APP_STORE_FIXTURES "/") + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class AppStoreTests final : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir m_folder;
    QString path(const QString &name) const { return m_folder.filePath(name); }
    QStringList log() const
    {
        QFile file(path("log"));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts)
                                              : QStringList();
    }
    static void write(const QString &file, const QByteArray &data)
    {
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(data);
    }
    void snap(QWidget *window, const QString &name)
    {
        const auto evidence = qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR");
        if (!evidence.isEmpty())
            window->grab().save(evidence + "/app-store-" + name + ".png");
    }
    // Chooses the row whose Name is `title`, as a click would.
    static bool choose(AppStoreWindow &window, const QString &title)
    {
        auto *model = window.m_list->model();
        for (int row = 0; row < model->rowCount(); ++row)
            if (model->index(row, 0).data().toString() == title)
            {
                window.m_list->setCurrentIndex(model->index(row, 0));
                return true;
            }
        return false;
    }
    static void show(AppStoreWindow &window, const QString &text)
    {
        const int index = window.m_show->findText(text);
        QVERIFY2(index >= 0, qPrintable(text));
        window.m_show->setCurrentIndex(index);
        emit window.m_show->activated(index);
    }

  private slots:
    void initTestCase()
    {
        qApp->setStyle(new PlatinumStyle);
        qApp->setFont(classicInterfaceFont());
        qputenv("PLATINUM_APP_STORE_TEST", "1");
        QVERIFY(m_folder.isValid());
    }

    void init()
    {
        write(path("platinum-packages"), fixture("platinum-packages"));
        QFile::setPermissions(path("platinum-packages"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        write(path("catalog.json"), fixture("catalog.json"));
        QFile::remove(path("log"));
        QFile::remove(path("fail"));
        QFile::remove(path("pause"));
    }

    void classicSizes()
    {
        QCOMPARE(classicSize(0), QString("--"));
        QCOMPARE(classicSize(10), QString("1 K"));
        QCOMPARE(classicSize(12 * 1024), QString("12 K"));
        QCOMPARE(classicSize(3604480), QString("3.4 MB"));
        QCOMPARE(classicSize(1288490189LL), QString("1.2 GB"));
    }

    void showsApplicationsCategoriesAndFinds()
    {
        PackageClient client(path("platinum-packages"));
        AppStoreWindow window(&client);
        window.show();
        QTRY_COMPARE(window.m_count->text(), QString("2 applications"));
        QCOMPARE(window.m_list->model()->rowCount(), 2);
        // Finder's own list parts draw the list.
        QVERIFY(dynamic_cast<FolderColumnHeader *>(window.m_list->header()));
        QVERIFY(dynamic_cast<FolderListDelegate *>(window.m_list->itemDelegate()));
        QCOMPARE(window.m_list->model()->index(0, 0).data().toString(), QString("Chess"));
        snap(&window, "1-applications");

        show(window, "Graphics");
        QCOMPARE(window.m_list->model()->rowCount(), 1);
        QCOMPARE(window.m_count->text(), QString("1 application"));
        show(window, "All Applications");
        window.m_find->setText("draw");
        QTRY_COMPARE(window.m_list->model()->rowCount(), 1);
        QCOMPARE(window.m_count->text(), QString("1 of 2 applications"));
        window.m_find->clear();
        QTRY_COMPARE(window.m_list->model()->rowCount(), 2);

        show(window, "All Packages");
        QCOMPARE(window.m_count->text(), QString("4 packages"));
        show(window, "Installed Software");
        QCOMPARE(window.m_list->model()->rowCount(), 1);
        show(window, "Updates");
        QCOMPARE(window.m_count->text(), QString("1 update"));
        QCOMPARE(window.m_list->model()->index(0, CatalogModel::Version).data().toString(), QString("6.1-1"));
    }

    void installsTheChosenApplicationAndKeepsItChosen()
    {
        PackageClient client(path("platinum-packages"));
        AppStoreWindow window(&client);
        window.show();
        QTRY_VERIFY(choose(window, "Paint"));
        QCOMPARE(window.m_title->text(), QString("Paint"));
        QCOMPARE(window.m_summary->text(), QString("Draw pictures"));
        QVERIFY(window.m_facts->text().contains("Package paint 2.0-1"));
        QVERIFY(window.m_facts->text().contains("4.8 MB"));
        QVERIFY(window.m_facts->text().contains("Arch (extra)"));
        QVERIFY(window.m_about->toHtml().contains("https://paint.example"));
        QCOMPARE(window.m_primary->text(), QString("Install"));
        QVERIFY(window.m_primary->isVisible());
        QVERIFY(!window.m_extra->isVisible());
        snap(&window, "2-chosen");

        write(path("pause"), "");
        window.m_primary->click();
        QTRY_VERIFY(window.m_progress->isVisible());
        QVERIFY(!window.m_primary->isEnabled());
        QVERIFY(window.m_status->text().startsWith("Installing Paint"));
        snap(&window, "3-installing");
        QFile::remove(path("pause"));
        QTRY_VERIFY(!window.m_progress->isVisible());
        QTRY_COMPARE(window.m_list->model()->index(window.m_list->currentIndex().row(), CatalogModel::Status)
                         .data()
                         .toString(),
                     QString("Installed"));
        QCOMPARE(window.m_title->text(), QString("Paint"));
        QVERIFY(!window.m_primary->isVisible());
        QVERIFY(window.m_extra->isVisible());
        QCOMPARE(log().mid(0, 2), QStringList({"catalog", "install paint"}));
        QCOMPARE(window.lastAlert(), QString());
    }

    void removingAsksFirstAndFailuresAreExplained()
    {
        auto state = fixture("catalog.json").replace(R"("installed": "", "url")", R"("installed": "2.0-1", "explicit": true, "url")");
        write(path("catalog.json"), state);
        PackageClient client(path("platinum-packages"));
        AppStoreWindow window(&client);
        window.show();
        QTRY_VERIFY(choose(window, "Paint"));
        QVERIFY(window.m_extra->isVisible());
        window.setTestAnswers({false});
        window.m_extra->click();
        QCOMPARE(window.lastQuestion(), QString("Remove “Paint”?"));
        QCOMPARE(log(), QStringList({"catalog"}));

        write(path("fail"), "“chess” needs “paint”, so it can’t be removed.");
        window.setTestAnswers({true});
        window.m_extra->click();
        QTRY_VERIFY(window.lastAlert().contains("so it can’t be removed"));
        QVERIFY(window.lastAlert().startsWith("Removing Paint didn’t work."));
        QTRY_COMPARE(log().last(), QString("catalog"));  // Read again after the failed change.

        // A dismissed password box is not an error.
        write(path("fail"), "cancel");
        window.setTestAnswers({true});
        window.m_extra->click();
        QTRY_VERIFY(!client.busy() && log().size() >= 5);
        QVERIFY(!window.lastAlert().contains("cancel"));
    }

    void aurNeedsYayAndAWarningBeforeInstalling()
    {
        PackageClient client(path("platinum-packages"));
        AppStoreWindow window(&client);
        window.show();
        QTRY_VERIFY(window.m_count->text().contains("applications"));
        show(window, "AUR Packages");
        QCOMPARE(window.m_list->model()->rowCount(), 0);
        QCOMPARE(window.m_primary->text(), QString("Install yay…"));
        QVERIFY(window.m_primary->isVisible());
        snap(&window, "4-aur-needs-yay");
        window.setTestAnswers({true});
        window.m_primary->click();
        QCOMPARE(window.lastQuestion(), QString("Install yay?"));
        // yay, then its list straight away.
        QTRY_VERIFY(log().contains("refresh-aur"));
        QTRY_COMPARE(window.m_list->model()->rowCount(), 1);
        QVERIFY(log().indexOf("install-yay") < log().indexOf("refresh-aur"));

        QVERIFY(choose(window, "aurthing"));
        QVERIFY(window.m_facts->text().contains("AUR"));
        QVERIFY(window.m_facts->text().contains("7 votes"));
        QVERIFY(window.m_facts->text().contains("No maintainer"));
        window.setTestAnswers({false});
        window.m_primary->click();
        QCOMPARE(window.lastQuestion(), QString("Install “aurthing” from the AUR?"));
        QVERIFY(!log().contains("install-aur aurthing"));
        window.setTestAnswers({true});
        window.m_primary->click();
        QTRY_VERIFY(log().contains("install-aur aurthing"));
        QTRY_VERIFY(!client.busy());
        snap(&window, "5-aur-installed");
    }

    void updatesGoThroughUpdateAllAndAskForARestart()
    {
        PackageClient client(path("platinum-packages"));
        AppStoreWindow window(&client);
        window.show();
        QTRY_VERIFY(window.m_count->text().contains("applications"));
        show(window, "Updates");
        QVERIFY(choose(window, "linux"));
        QCOMPARE(window.m_primary->text(), QString("Update"));
        window.setTestAnswers({true});
        window.m_primary->click();
        QCOMPARE(window.lastQuestion(), QString("Update all software?"));
        QTRY_VERIFY(log().contains("update-all"));
        QTRY_COMPARE(window.lastAlert().section('\n', 0, 0), QString("Restart the computer to finish updating."));
        QTRY_COMPARE(window.m_count->text(), QString("0 updates"));
    }

    void withoutTheAppCatalogItOffersToInstallIt()
    {
        // archlinux-appstream-data is optional (update 45), so Applications can be empty.
        auto catalog = QJsonDocument::fromJson(fixture("catalog.json")).object();
        auto packages = catalog["packages"].toArray();
        packages.append(QJsonObject{{"name", "archlinux-appstream-data"}, {"version", "20261101-1"},
                                    {"description", "Arch Linux application database for AppStream-based software centers"},
                                    {"repo", "extra"}, {"size", 21000000}, {"installed", ""}});
        catalog["packages"] = packages;
        catalog["apps"] = QJsonArray();
        write(path("catalog.json"), QJsonDocument(catalog).toJson());
        PackageClient client(path("platinum-packages"));
        AppStoreWindow window(&client);
        window.show();
        QTRY_VERIFY(window.m_count->text().contains("applications"));
        QCOMPARE(window.m_title->text(), QString("Applications"));
        QVERIFY(window.m_summary->text().contains("isn't installed"));
        QCOMPARE(window.m_primary->text(), QString("Install Catalog…"));
        QVERIFY(window.m_primary->isVisible());
        snap(&window, "6-no-app-catalog");
        window.m_primary->click();
        QTRY_VERIFY(log().contains("install archlinux-appstream-data"));
        QTRY_VERIFY(!client.busy());
        QCOMPARE(window.lastAlert(), QString());
        // Every package is still listed.
        show(window, "All Packages");
        QVERIFY(choose(window, "archlinux-appstream-data"));
    }

    void cannotBeClosedWhileWorking()
    {
        PackageClient client(path("platinum-packages"));
        AppStoreWindow window(&client);
        window.show();
        QTRY_VERIFY(choose(window, "Chess"));
        write(path("pause"), "");
        window.m_primary->click();
        QTRY_VERIFY(client.busy());
        QVERIFY(!window.close());
        QCOMPARE(window.lastAlert().section('\n', 0, 0), QString("The App Store is still working."));
        QFile::remove(path("pause"));
        QTRY_VERIFY(!client.busy());
        QVERIFY(window.close());
    }

    // Opt-in (APP_STORE_REAL_HELPER=scripts/platinum-packages): this computer's real catalog, read only.
    void realCatalog()
    {
        const auto helper = qEnvironmentVariable("APP_STORE_REAL_HELPER");
        if (helper.isEmpty())
            QSKIP("APP_STORE_REAL_HELPER is not set");
        PackageClient client(helper);
        AppStoreWindow window(&client);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(window.m_count->text().endsWith("applications"), 30000);
        QVERIFY(window.m_list->model()->rowCount() > 100);
        window.m_find->setText("image manipulation");
        QTRY_VERIFY(window.m_count->text().contains(" of "));
        QVERIFY(choose(window, "GNU Image Manipulation Program"));
        QTest::qWait(100);
        snap(&window, "real-gimp");
        window.m_find->clear();
        show(window, "Games");
        QTRY_VERIFY(window.m_list->model()->rowCount() > 10);
        window.m_list->setCurrentIndex(window.m_list->model()->index(3, 0));
        QTest::qWait(300);
        snap(&window, "real-games");
        show(window, "All Packages");
        QVERIFY(window.m_list->model()->rowCount() > 10000);
        snap(&window, "real-packages");
    }

    void aMissingHelperIsReported()
    {
        PackageClient client(path("missing-helper"));
        AppStoreWindow window(&client);
        QTRY_VERIFY(window.lastAlert().contains("helper is missing"));
    }
};

QTEST_MAIN(AppStoreTests)
#include "app_store_tests.moc"
