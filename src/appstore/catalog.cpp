#include "catalog.hpp"
#include <QApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
#include <QPainter>
#include <QStyle>

Catalog Catalog::fromJson(const QJsonObject &document)
{
    Catalog catalog;
    catalog.yay = document.value("yay").toBool();
    catalog.aurUpdated = document.value("aurUpdated").toString();
    const auto packages = document.value("packages").toArray();
    catalog.packages.reserve(packages.size());
    for (const auto &value : packages)
    {
        const auto o = value.toObject();
        Package p;
        p.name = o.value("name").toString();
        if (p.name.isEmpty() || catalog.m_byName.contains(p.name))
            continue;
        p.version = o.value("version").toString();
        p.description = o.value("description").toString();
        p.repo = o.value("repo").toString();
        p.url = o.value("url").toString();
        p.installed = o.value("installed").toString();
        p.update = o.value("update").toString();
        p.size = o.value("size").toInteger();
        p.explicitlyInstalled = o.value("explicit").toBool();
        p.outOfDate = o.value("outOfDate").toBool();
        p.orphan = o.value("orphan").toBool();
        p.votes = o.value("votes").toInt();
        catalog.m_byName.insert(p.name, catalog.packages.size());
        catalog.packages.append(p);
    }
    for (const auto &value : document.value("apps").toArray())
    {
        const auto o = value.toObject();
        Application a{o.value("id").toString(),       o.value("title").toString(),  o.value("package").toString(),
                      o.value("summary").toString(),  o.value("about").toString(),  o.value("category").toString(),
                      o.value("icon").toString(),     o.value("launch").toString(), o.value("homepage").toString(),
                      o.value("stock").toString()};
        if (a.title.isEmpty() || !catalog.m_byName.contains(a.package))
            continue;
        if (!catalog.m_appByPackage.contains(a.package))
            catalog.m_appByPackage.insert(a.package, catalog.apps.size());
        catalog.apps.append(a);
    }
    return catalog;
}

QStringList catalogCategories()
{
    return {"Audio & Video", "Development", "Education", "Games", "Graphics", "Internet",
            "Office",        "Science",     "System",    "Utilities"};
}

QString classicSize(qint64 bytes)
{
    if (bytes <= 0)
        return "--";
    const QLocale english(QLocale::English);
    if (bytes < 1024 * 1024)
        return english.toString(qMax<qint64>(1, (bytes + 1023) / 1024)) + " K";
    if (bytes < 1024LL * 1024 * 1024)
        return english.toString(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return english.toString(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
}

QString sourceName(const Package &package)
{
    if (package.fromAur())
        return "AUR";
    if (package.repo == "local")
        return "This Computer";
    return "Arch (" + package.repo + ")";
}

CatalogModel::CatalogModel(QObject *parent) : QAbstractTableModel(parent) {}

void CatalogModel::setCatalog(Catalog catalog)
{
    beginResetModel();
    m_catalog = std::move(catalog);
    m_icons.clear();
    rebuild();
    endResetModel();
}

void CatalogModel::setView(View view)
{
    if (view == m_view)
        return;
    beginResetModel();
    m_view = view;
    rebuild();
    endResetModel();
}

void CatalogModel::rebuild()
{
    m_rows.clear();
    if (m_view == Applications)
    {
        m_rows.reserve(m_catalog.apps.size());
        for (int i = 0; i < m_catalog.apps.size(); ++i)
            m_rows.append({m_catalog.indexOf(m_catalog.apps[i].package), i});
        return;
    }
    for (int i = 0; i < m_catalog.packages.size(); ++i)
    {
        const auto &p = m_catalog.packages[i];
        const bool shown = m_view == AllPackages   ? p.official()
                           : m_view == AurPackages ? p.fromAur()
                           : m_view == Installed   ? p.isInstalled() && p.explicitlyInstalled
                                                   : p.isInstalled() && !p.update.isEmpty();
        if (shown)
            m_rows.append({i, m_view == AllPackages || m_view == AurPackages ? -1 : m_catalog.appFor(p.name)});
    }
}

const Package *CatalogModel::package(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return nullptr;
    const int i = m_rows[index.row()].package;
    return i >= 0 ? &m_catalog.packages[i] : nullptr;
}

const Application *CatalogModel::application(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return nullptr;
    const int i = m_rows[index.row()].app;
    return i >= 0 ? &m_catalog.apps[i] : nullptr;
}

namespace
{
// Original 32px drawings for software without its own icon: an application (a window with a
// title bar) and a package (a closed box), in the Platinum palette.
QIcon drawnIcon(bool application)
{
    QIcon icon;
    for (const int size : {16, 32})
    {
        QPixmap image(size, size);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.scale(size / 32.0, size / 32.0);
        p.setPen(QColor("#000000"));
        if (application)
        {
            p.setBrush(QColor("#ffffff"));
            p.drawRect(3, 5, 25, 22);
            p.fillRect(4, 6, 24, 5, QColor("#9999cc"));
            p.drawLine(3, 11, 28, 11);
            p.fillRect(7, 14, 17, 2, QColor("#666699"));
            p.fillRect(7, 18, 12, 2, QColor("#666699"));
            p.fillRect(7, 22, 15, 2, QColor("#666699"));
        }
        else
        {
            p.setBrush(QColor("#d9b47a"));
            p.drawPolygon(QPolygon({{4, 11}, {16, 6}, {28, 11}, {16, 16}}));
            p.setBrush(QColor("#c09050"));
            p.drawPolygon(QPolygon({{4, 11}, {16, 16}, {16, 28}, {4, 23}}));
            p.setBrush(QColor("#a87838"));
            p.drawPolygon(QPolygon({{16, 16}, {28, 11}, {28, 23}, {16, 28}}));
            p.setPen(QColor("#ffffff"));
            p.drawLine(10, 8, 22, 13);
        }
        icon.addPixmap(image);
    }
    return icon;
}
} // namespace

QIcon CatalogModel::icon(const Package &package, const Application *app) const
{
    const auto key = !app                                      ? QStringLiteral(":pkg")
                     : !app->icon.isEmpty() || !app->stock.isEmpty() ? app->icon + '|' + app->stock
                                                                     : QStringLiteral(":app");
    auto found = m_icons.find(key);
    if (found != m_icons.end())
        return *found;
    QIcon icon;
    if (app && !app->icon.isEmpty())
    {
        const QFileInfo file(app->icon);
        QPixmap pixmap;
        // AppStream's cached icons are small; anything else is not an icon.
        if (file.isFile() && file.size() <= 512 * 1024 && pixmap.load(file.filePath()))
            icon.addPixmap(pixmap);
    }
    if (icon.isNull() && app && !app->stock.isEmpty() && !app->stock.contains('/'))
        icon = QIcon::fromTheme(app->stock);
    if (icon.isNull())
    {
        static const QIcon applicationIcon = drawnIcon(true), packageIcon = drawnIcon(false);
        icon = app ? applicationIcon : packageIcon;
    }
    Q_UNUSED(package);
    m_icons.insert(key, icon);
    return icon;
}

int CatalogModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_rows.size(); }

int CatalogModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant CatalogModel::data(const QModelIndex &index, int role) const
{
    const auto *p = package(index);
    if (!p)
        return {};
    const auto *app = application(index);
    const auto title = app ? app->title : p->name;
    const auto status = !p->isInstalled() ? QString() : p->update.isEmpty() ? QStringLiteral("Installed")
                                                                             : QStringLiteral("Update");
    switch (role)
    {
    case Qt::DisplayRole:
        switch (index.column())
        {
        case Name: return title;
        case Status: return status;
        case Version: return p->isInstalled() && !p->update.isEmpty() ? p->update : p->version;
        case Size: return classicSize(p->size);
        case Source: return sourceName(*p);
        }
        break;
    case Qt::DecorationRole:
        if (index.column() == Name)
            return icon(*p, app);
        break;
    case Qt::ToolTipRole:
        if (index.column() == Name)
            return app && !app->summary.isEmpty() ? app->summary : p->description;
        break;
    case SortRole:
        switch (index.column())
        {
        case Name: return title.toLower();
        case Status: return status.isEmpty() ? 2 : status == "Update" ? 0 : 1;
        case Size: return p->size;
        default: return data(index, Qt::DisplayRole).toString().toLower();
        }
    case CategoryRole: return app ? app->category : QString();
    case SearchRole:
        return QStringList{title, p->name, app ? app->summary : QString(), p->description}.join('\n');
    case PackageRole: return p->name;
    }
    return {};
}

QVariant CatalogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    static const QStringList names{"Name", "Status", "Version", "Size", "Source"};
    return names.value(section);
}

void CatalogFilter::setCategory(const QString &category)
{
    beginFilterChange();
    m_category = category;
    endFilterChange(Direction::Rows);
}

void CatalogFilter::setSearch(const QString &text)
{
    const auto words = text.simplified().toLower().split(' ', Qt::SkipEmptyParts);
    if (words == m_words)
        return;
    beginFilterChange();
    m_words = words;
    endFilterChange(Direction::Rows);
}

bool CatalogFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto index = sourceModel()->index(row, 0, parent);
    if (!m_category.isEmpty() && index.data(CatalogModel::CategoryRole).toString() != m_category)
        return false;
    if (m_words.isEmpty())
        return true;
    const auto text = index.data(CatalogModel::SearchRole).toString();
    for (const auto &word : m_words)
        if (!text.contains(word, Qt::CaseInsensitive))
            return false;
    return true;
}
