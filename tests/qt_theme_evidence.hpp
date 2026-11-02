#pragma once
#include <QFile>

// The Arch integration checks must fail if requesting "kde" silently falls
// back. Inspect the running process, not just the presence of a plugin file.
inline bool kdePlatformThemeLoaded()
{
    QFile maps("/proc/self/maps");
    return maps.open(QIODevice::ReadOnly) &&
           maps.readAll().contains("/platformthemes/KDEPlasmaPlatformTheme6.so");
}
