#pragma once
#include <QList>
#include <QString>

// File labels, as Mac OS 9's Label menu: none or one of seven colours, kept with the file as
// the extended attribute user.platinum.label ("1" to "7"). On a filesystem without extended
// attributes (some USB sticks, network shares) the label is kept in the profile's labels.json
// instead, by absolute path. The colours are Mac OS 9's defaults; the names are the colours'.
namespace FileLabels
{
struct Label
{
    int index;
    QString name;
    QString color; // #rrggbb
};
// Labels 1 to 7, in menu order.
const QList<Label> &labels();
// The name ("" for none) and colour ("" for none) of a label number.
QString name(int label);
QString color(int label);
// 0 for none or unreadable.
int label(const QString &path, const QString &profile);
// 0 removes the label. False when the file cannot be labelled.
bool setLabel(const QString &path, int label, const QString &profile);
} // namespace FileLabels
