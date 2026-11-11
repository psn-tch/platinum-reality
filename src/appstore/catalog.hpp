#pragma once
#include <QAbstractTableModel>
#include <QHash>
#include <QIcon>
#include <QJsonObject>
#include <QSortFilterProxyModel>
#include <QVector>

// What the App Store lists (docs/app-store.md), as read by `platinum-packages catalog`.
struct Package
{
    QString name, version, description, repo, url, installed, update;
    qint64 size = 0;
    bool explicitlyInstalled = false, outOfDate = false, orphan = false;
    int votes = 0;
    bool isInstalled() const { return !installed.isEmpty(); }
    bool fromAur() const { return repo == "aur"; }
    bool official() const { return repo != "aur" && repo != "local"; }
};

struct Application
{
    QString id, title, package, summary, about, category, icon, launch, homepage;
    // The icon theme's name for it, when its cached picture can't be read (no JPEG XL support).
    QString stock;
};

struct Catalog
{
    QVector<Package> packages;
    QVector<Application> apps;
    bool yay = false;
    QString aurUpdated;
    static Catalog fromJson(const QJsonObject &document);
    int indexOf(const QString &name) const { return m_byName.value(name, -1); }
    // The first application a package provides, or -1.
    int appFor(const QString &name) const { return m_appByPackage.value(name, -1); }

  private:
    QHash<QString, int> m_byName, m_appByPackage;
};

// The categories of the Show pop-up menu, in its order.
QStringList catalogCategories();
// Sizes as Mac OS 9 wrote them: 12 K, 3.4 MB, 1.2 GB.
QString classicSize(qint64 bytes);
// The repository as people read it: Arch (extra), AUR, This Computer.
QString sourceName(const Package &package);

class CatalogModel final : public QAbstractTableModel
{
    Q_OBJECT
  public:
    enum Column { Name, Status, Version, Size, Source, ColumnCount };
    enum View { Applications, AllPackages, AurPackages, Installed, Updates };
    enum Role { SortRole = Qt::UserRole + 1, CategoryRole, SearchRole, PackageRole };
    explicit CatalogModel(QObject *parent = nullptr);
    void setCatalog(Catalog catalog);
    const Catalog &catalog() const { return m_catalog; }
    void setView(View view);
    View view() const { return m_view; }
    // The package (and application, or nullptr) a row shows.
    const Package *package(const QModelIndex &index) const;
    const Application *application(const QModelIndex &index) const;
    QIcon icon(const Package &package, const Application *app) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

  private:
    struct Row
    {
        int package = -1, app = -1;
    };
    void rebuild();
    Catalog m_catalog;
    View m_view = Applications;
    QVector<Row> m_rows;
    mutable QHash<QString, QIcon> m_icons;
};

// The Show pop-up's category and the Find field, over the model.
class CatalogFilter final : public QSortFilterProxyModel
{
    Q_OBJECT
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setCategory(const QString &category);
    void setSearch(const QString &text);
    QString search() const { return m_words.join(' '); }

  protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

  private:
    QString m_category;
    QStringList m_words;
};
