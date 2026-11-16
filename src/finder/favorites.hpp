#pragma once
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <utility>

// Folders of aliases, as 9.2.2 kept Favorites and Launcher Items. Here the aliases are symbolic
// links, as the Finder's Make Alias makes, so removing one (in the Finder) never touches the item.
namespace Aliases
{
// A link in the folder for each item not already there, named as the item ("name 2" when taken).
// Returns how many were added; at most maximum links are kept.
int add(const QString &folder, const QStringList &items, int maximum);
// The same, each link given a name ({item, name}); an empty name is the item's own.
int add(const QString &folder, const QList<std::pair<QString, QString>> &items, int maximum);
// [{name, link, target, folder, exists}], by name.
QVariantList list(const QString &folder);
} // namespace Aliases

// Favorites, as in 9.2.2 (File ▸ Add To Favorites; the Apple menu's Favorites), in the session
// profile's Favorites folder.
namespace Favorites
{
QString folder(const QString &profile);
constexpr int maximumCount = 64;
int add(const QString &profile, const QStringList &items);
QVariantList list(const QString &profile);
} // namespace Favorites

// The Launcher's items, as 9.2.2's Launcher Items folder (File ▸ Add To Launcher), in the session
// profile's Launcher Items folder.
namespace LauncherItems
{
QString folder(const QString &profile);
constexpr int maximumCount = 64;
int add(const QString &profile, const QStringList &items);
// An application, as a link to its .desktop file named as the application ("Firefox").
bool addApplication(const QString &profile, const QString &file, const QString &name);
} // namespace LauncherItems
