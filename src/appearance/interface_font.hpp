#pragma once
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

// Use a locally installed face; never copy a proprietary font into the project.
inline QString classicInterfaceFontFamily()
{
    return QFontDatabase::families().contains("Charcoal", Qt::CaseInsensitive)
        ? QStringLiteral("Charcoal") : QStringLiteral("DejaVu Sans");
}
inline QFont classicInterfaceFont()
{
    return QFont(classicInterfaceFontFamily(), 9);
}
