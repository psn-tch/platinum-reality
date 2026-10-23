#pragma once
#include "desktop_entry.hpp"

// Writes only presentation files within the explicitly supplied session profile.
// Existing user CSS is retained; managed assets may be refreshed on a later build.
bool prepareApplicationAppearance(const QString &profile, QString &error);
// With the user's normal application settings, GTK reads the user's own configuration, so the
// Platinum look comes from a private theme selected with GTK_THEME, never from the user's gtk.css.
// Returns the data directory to put first in XDG_DATA_DIRS, or an empty string on error.
QString preparePrivateGtkTheme(const QString &profile, QString &error);
inline constexpr auto privateGtkThemeName = "PlatinumReality";
bool prepareStyledApplication(frankenstein::applications::LaunchPlan &plan, const QString &profile,
                              QString &error);
// Presentation adaptation for the private browser profile, never an authority
// grant. The live Wayland peer's executable and explicit profile must both match.
bool ownedFirefoxProfileArguments(const QStringList &arguments, const QString &profile);
bool ownedFirefoxLaunch(const QString &program, const QStringList &arguments,
                        const QString &profile);
bool ownedFirefoxFrame(qint64 peerPid, const QString &profile);
// Any Firefox, the user's own profile included, gets the Platinum window frame (presentation
// only). Decided from the live Wayland peer's executable, or the program being launched.
bool firefoxClient(qint64 peerPid);
bool firefoxLaunch(const QString &program);
