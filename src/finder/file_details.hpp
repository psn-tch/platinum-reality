#pragma once
#include <QString>
#include <QVariantMap>

// Mac OS 9 Info-window facts for one item, shared by the desktop and Finder Get Info windows
// (Finishing School FS-G01, docs/finishing-school-native.md). Read-only and bounded:
// {kindLabel, size, where, created, modified, original, capacity, available, missing}.
// - A folder shows how many items it holds (shallow, at most 10,000), never its own inode size.
// - An alias names its original, which is not its size or kind.
// - A disk shows capacity and space available.
// - Facts Linux lacks read "Unavailable".
// kind is "disk" or "trash" for those special items; anything else is read from the path.
QVariantMap fileDetails(const QString &path, const QString &kind = {});
