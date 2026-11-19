#pragma once
#include <QString>
#include <QVariantList>

// Mac OS 9's Recent Documents: an ordered history backed by real aliases in the profile.
// Deleting an alias forgets that entry; documents themselves are never modified or removed.
namespace RecentDocuments
{
constexpr int maximumCount = 99;
QString folder(const QString &profile);
QVariantList list(const QString &profile);
bool remember(const QString &profile, const QString &document, int maximum);
bool clear(const QString &profile);
}
