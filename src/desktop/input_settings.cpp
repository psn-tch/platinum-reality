#include "input_settings.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace
{
struct Range
{
    QMetaType::Type type;
    double low, high;
    QVariant fallback;
};
// As scripts/standalone-session.py's INPUT_RANGES; the defaults are Sway's and libinput's, with
// tap to click on as the session's own Sway configuration sets it.
const QMap<QString, Range> &ranges()
{
    static const QMap<QString, Range> table{
        // The checked Keyboard Layouts (1 to 8 of layoutTable()), and the one in use, always checked.
        {"keyboard/layouts", {QMetaType::QStringList, 1, 8, QStringList{"us"}}},
        {"keyboard/layout", {QMetaType::QString, 0, 0, QString("us")}},
        {"keyboard/repeatDelay", {QMetaType::Int, 150, 1500, 600}},
        {"keyboard/repeatRate", {QMetaType::Int, 1, 60, 25}},
        {"pointer/speed", {QMetaType::Double, -1, 1, 0.0}},
        {"pointer/natural", {QMetaType::Bool, 0, 0, false}},
        {"pointer/leftHanded", {QMetaType::Bool, 0, 0, false}},
        {"touchpad/speed", {QMetaType::Double, -1, 1, 0.0}},
        {"touchpad/natural", {QMetaType::Bool, 0, 0, false}},
        {"touchpad/tap", {QMetaType::Bool, 0, 0, true}},
        {"touchpad/disableWhileTyping", {QMetaType::Bool, 0, 0, true}},
    };
    return table;
}
// The Keyboard Layouts list, named after Mac OS 9.2.2's where it had one, as xkb layout and
// variant ("ch(fr)": layout ch, variant fr). As scripts/standalone-session.py's KEYBOARD_LAYOUTS.
const QList<std::pair<QString, QString>> &layoutTable()
{
    static const QList<std::pair<QString, QString>> table{
        {"au", "Australian"}, {"at", "Austrian"}, {"be", "Belgian"}, {"br", "Brazilian"},
        {"gb", "British"}, {"bg", "Bulgarian"}, {"ca(multix)", "Canadian – CSA"},
        {"ca", "Canadian – French"}, {"hr", "Croatian"}, {"cz", "Czech"}, {"dk", "Danish"},
        {"nl", "Dutch"}, {"us(dvorak)", "Dvorak"}, {"fi", "Finnish"}, {"fr", "French"},
        {"de", "German"}, {"gr", "Greek"}, {"hu", "Hungarian"}, {"is", "Icelandic"},
        {"ie", "Irish"}, {"it", "Italian"}, {"jp", "Japanese"}, {"latam", "Latin American"},
        {"lt", "Lithuanian"}, {"no", "Norwegian"}, {"pl", "Polish"}, {"pt", "Portuguese"},
        {"ro", "Romanian"}, {"ru", "Russian"}, {"sk", "Slovak"}, {"si", "Slovenian"},
        {"es", "Spanish"}, {"se", "Swedish"}, {"ch(fr)", "Swiss French"}, {"ch", "Swiss German"},
        {"tr", "Turkish"}, {"us", "U.S."}, {"us(intl)", "U.S. – International"},
    };
    return table;
}
bool knownLayout(const QVariant &value)
{
    return value.typeId() == QMetaType::QString &&
           std::any_of(layoutTable().cbegin(), layoutTable().cend(),
                       [&](const auto &entry) { return entry.first == value.toString(); });
}
// The value as stored, or an invalid QVariant when it is not acceptable for the key.
QVariant accepted(const QString &key, const QVariant &value)
{
    const auto range = ranges().value(key);
    if (range.type == QMetaType::QString)
        return knownLayout(value) ? value : QVariant();
    if (range.type == QMetaType::QStringList)
    {
        // From QML or JSON a list arrives as a QVariantList; each entry must be a known layout, once.
        if (value.typeId() != QMetaType::QVariantList && value.typeId() != QMetaType::QStringList)
            return {};
        const auto items = value.toList();
        QStringList layouts;
        for (const auto &item : items)
        {
            if (!knownLayout(item) || layouts.contains(item.toString()))
                return {};
            layouts.append(item.toString());
        }
        return layouts.size() >= range.low && layouts.size() <= range.high ? QVariant(layouts) : QVariant();
    }
    if (range.type == QMetaType::Bool)
        return value.typeId() == QMetaType::Bool ? value : QVariant();
    if (value.typeId() == QMetaType::Bool || !value.canConvert<double>())
        return {};
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < range.low || number > range.high)
        return {};
    if (range.type == QMetaType::Int)
        return number == std::floor(number) ? QVariant(int(number)) : QVariant();
    return QVariant(std::round(number * 100) / 100);
}
} // namespace

InputSettings::InputSettings(QString profile, QObject *parent)
    : QObject(parent), m_profile(QDir(profile).absolutePath())
{
    for (auto it = ranges().cbegin(); it != ranges().cend(); ++it)
        m_values.insert(it.key(), it.value().fallback);
    QFile input(m_profile + "/input.json");
    if (QFileInfo(input.fileName()).isSymLink() || !input.open(QIODevice::ReadOnly) || input.size() > 4096)
        return;
    const auto object = QJsonDocument::fromJson(input.read(4097)).object();
    if (object.value("version").toInt() != 1)
        return;
    for (auto it = ranges().cbegin(); it != ranges().cend(); ++it)
    {
        const auto parts = it.key().split('/');
        const auto stored = object.value(parts[0]).toObject().value(parts[1]);
        if (stored.isUndefined())
            continue;
        if (const auto value = accepted(it.key(), stored.toVariant()); value.isValid())
        {
            m_values[it.key()] = value;
            m_chosen.append(it.key());
        }
    }
    // A layout that is no longer checked: the first checked one is used.
    if (!layouts().contains(layout()))
        m_values["keyboard/layout"] = layouts().first();
}
QVariantList InputSettings::layoutChoices() const
{
    QVariantList choices;
    for (const auto &[id, name] : layoutTable())
        choices.append(QVariantMap{{"id", id}, {"name", name}});
    return choices;
}
QString InputSettings::layoutName(const QString &id) const
{
    for (const auto &[known, name] : layoutTable())
        if (known == id)
            return name;
    return {};
}
QVariantMap InputSettings::section(const QString &name) const
{
    QVariantMap result;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
        if (it.key().startsWith(name + "/"))
            result.insert(it.key().mid(name.size() + 1), it.value());
    return result;
}
bool InputSettings::set(const QString &section, const QString &key, const QVariant &value)
{
    const auto name = section + "/" + key;
    const auto stored = ranges().contains(name) ? accepted(name, value) : QVariant();
    if (!stored.isValid())
    {
        emit errorOccurred("That input setting is outside its supported range.");
        return false;
    }
    // Only a checked layout can be the one in use.
    if (name == "keyboard/layout" && !layouts().contains(stored.toString()))
    {
        emit errorOccurred("That keyboard layout is not checked in the Keyboard control panel.");
        return false;
    }
    if (m_values.value(name) == stored && m_chosen.contains(name))
        return true;
    auto values = m_values;
    values[name] = stored;
    auto chosen = m_chosen;
    if (!chosen.contains(name))
        chosen.append(name);
    // Unchecking the layout in use moves to the first one still checked.
    if (name == "keyboard/layouts" && !stored.toStringList().contains(values.value("keyboard/layout").toString()))
    {
        values["keyboard/layout"] = stored.toStringList().first();
        if (!chosen.contains("keyboard/layout"))
            chosen.append("keyboard/layout");
    }
    return save(values, chosen);
}
bool InputSettings::reset(const QString &section)
{
    auto values = m_values;
    QStringList chosen;
    // Use Defaults is for Key Repeat: the checked Keyboard Layouts stay as they are.
    for (const auto &name : std::as_const(m_chosen))
        if (name.startsWith(section + "/") && !name.startsWith("keyboard/layout"))
            values[name] = ranges().value(name).fallback;
        else
            chosen.append(name);
    return chosen == m_chosen || save(values, chosen);
}
bool InputSettings::save(const QVariantMap &values, const QStringList &chosen)
{
    QJsonObject object{{"version", 1}};
    for (const auto &name : chosen)
    {
        const auto parts = name.split('/');
        auto group = object.value(parts[0]).toObject();
        group.insert(parts[1], QJsonValue::fromVariant(values.value(name)));
        object.insert(parts[0], group);
    }
    const auto bytes = QJsonDocument(object).toJson();
    if (!QDir().mkpath(m_profile) || QFileInfo(m_profile + "/input.json").isSymLink())
    {
        emit errorOccurred("Input settings could not be saved.");
        return false;
    }
    QSaveFile file(m_profile + "/input.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        emit errorOccurred("Input settings could not be saved: " + file.errorString());
        return false;
    }
    m_values = values;
    m_chosen = chosen;
    emit changed();
    emit saved();
    return true;
}
