#pragma once
#include <QDir>
#include <QMimeData>
#include <QSet>
#include <QUrl>

// A small, explicit URL contract. Never turn filenames into commands, accept a
// mixed remote/local drop partially, or let a view remove sources after exec().
inline constexpr auto finderDragMime = "application/x-frankenstein-file-drag";
inline constexpr auto finderFileUrlsMime = "application/x-frankenstein-local-file-urls";
inline QStringList fileDragPaths(const QMimeData *mime)
{
    if (!mime || !mime->hasUrls())
        return {};
    // Qt's Wayland provider can redirect text/uri-list through an advertised
    // file-transfer portal, even on our private bus where no portal runs. Owned
    // Finder drags additionally offer these same URI bytes in a private format
    // so their transfer does not depend on that service. Decode one snapshot;
    // never retry a malformed owned payload through another transport.
    const bool owned = mime->hasFormat(finderDragMime) && mime->hasFormat(finderFileUrlsMime);
    const auto encoded = mime->data(owned ? finderFileUrlsMime : "text/uri-list");
    if (encoded.isEmpty() || encoded.size() > 256 * 1024 || encoded.contains('\0'))
        return {};
    QMimeData snapshot;
    snapshot.setData("text/uri-list", encoded);
    const auto urls = snapshot.urls();
    if (urls.isEmpty() || urls.size() > 128)
        return {};
    QStringList paths;
    QSet<QString> seen;
    for (const auto &url : urls)
    {
        const auto path = url.toLocalFile();
        if (!url.isLocalFile() || !QDir::isAbsolutePath(path) || path.contains(QChar::Null))
            return {};
        const auto clean = QDir::cleanPath(path);
        if (!seen.contains(clean))
        {
            seen.insert(clean);
            paths.push_back(clean);
        }
    }
    return paths;
}
