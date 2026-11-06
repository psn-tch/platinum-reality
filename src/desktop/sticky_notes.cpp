#include "sticky_notes.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <numeric>

namespace
{
constexpr int maximumNotes = 100;
constexpr int maximumText = 32768;
constexpr qint64 maximumFile = 4 * 1024 * 1024;
constexpr int minimumWidth = 120, minimumHeight = 80, maximumSide = 2000;
// Positions are desktop coordinates; a note further out than this was left on a screen that is gone.
constexpr int maximumPosition = 16384;
} // namespace

const QStringList &StickyNotes::colors()
{
    static const QStringList list{"yellow", "blue", "green", "pink", "purple", "gray"};
    return list;
}
const QStringList &StickyNotes::textSizes()
{
    static const QStringList list{"small", "medium", "large"};
    return list;
}

StickyNotes::StickyNotes(QString profile, QObject *parent)
    : QAbstractListModel(parent), m_profile(QDir(profile).absolutePath())
{
    m_saveDelay.setSingleShot(true);
    m_saveDelay.setInterval(600);
    connect(&m_saveDelay, &QTimer::timeout, this, &StickyNotes::saveNow);
    load();
}
StickyNotes::~StickyNotes()
{
    if (m_saveDelay.isActive())
        saveNow();
}
void StickyNotes::load()
{
    QFile input(m_profile + "/stickies.json");
    if (QFileInfo(input.fileName()).isSymLink() || !input.open(QIODevice::ReadOnly) || input.size() > maximumFile)
        return;
    const auto object = QJsonDocument::fromJson(input.read(maximumFile + 1)).object();
    if (object.value("version").toInt() != 1)
        return;
    m_hidden = object.value("hidden").toBool(false);
    for (const auto &value : object.value("notes").toArray())
    {
        const auto stored = value.toObject();
        Note note;
        note.id = stored.value("id").toInt();
        if (note.id <= 0 || rowOf(note.id) >= 0 || m_notes.size() >= maximumNotes)
            continue;
        note.text = stored.value("text").toString().left(maximumText);
        note.x = std::clamp(stored.value("x").toInt(), -maximumPosition, maximumPosition);
        note.y = std::clamp(stored.value("y").toInt(), -maximumPosition, maximumPosition);
        note.width = std::clamp(stored.value("width").toInt(220), minimumWidth, maximumSide);
        note.height = std::clamp(stored.value("height").toInt(160), minimumHeight, maximumSide);
        if (const auto color = stored.value("color").toString(); colors().contains(color))
            note.color = color;
        if (const auto size = stored.value("textSize").toString(); textSizes().contains(size))
            note.textSize = size;
        note.collapsed = stored.value("collapsed").toBool(false);
        note.floating = stored.value("floating").toBool(false);
        note.order = stored.value("order").toInt();
        note.created = QDateTime::fromString(stored.value("created").toString(), Qt::ISODate);
        m_nextId = std::max(m_nextId, note.id + 1);
        m_notes.append(note);
    }
}
bool StickyNotes::saveNow()
{
    m_saveDelay.stop();
    QJsonArray notes;
    for (const auto &note : std::as_const(m_notes))
        notes.append(QJsonObject{{"id", note.id}, {"text", note.text}, {"x", note.x}, {"y", note.y},
                                 {"width", note.width}, {"height", note.height}, {"color", note.color},
                                 {"textSize", note.textSize}, {"collapsed", note.collapsed},
                                 {"floating", note.floating}, {"order", note.order},
                                 {"created", note.created.toString(Qt::ISODate)}});
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"hidden", m_hidden}, {"notes", notes}}).toJson();
    QSaveFile file(m_profile + "/stickies.json");
    if (!QDir().mkpath(m_profile) || QFileInfo(file.fileName()).isSymLink() || !file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size() || !file.commit())
    {
        emit errorOccurred("Stickies could not save your notes.");
        return false;
    }
    return true;
}
void StickyNotes::scheduleSave()
{
    m_saveDelay.start();
}
int StickyNotes::rowOf(int id) const
{
    for (int row = 0; row < m_notes.size(); ++row)
        if (m_notes[row].id == id)
            return row;
    return -1;
}
void StickyNotes::changed(int row, const QList<int> &roles)
{
    const auto where = index(row);
    emit dataChanged(where, where, roles);
    scheduleSave();
}
int StickyNotes::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_notes.size());
}
QVariant StickyNotes::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_notes.size())
        return {};
    const auto &note = m_notes[index.row()];
    switch (role)
    {
    case IdRole: return note.id;
    case TextRole: return note.text;
    case XRole: return note.x;
    case YRole: return note.y;
    case WidthRole: return note.width;
    case HeightRole: return note.height;
    case ColorRole: return note.color;
    case TextSizeRole: return note.textSize;
    case CollapsedRole: return note.collapsed;
    case FloatingRole: return note.floating;
    case OrderRole: return note.order;
    case CreatedRole: return note.created;
    default: return {};
    }
}
QHash<int, QByteArray> StickyNotes::roleNames() const
{
    return {{IdRole, "noteId"}, {TextRole, "noteText"}, {XRole, "noteX"}, {YRole, "noteY"},
            {WidthRole, "noteWidth"}, {HeightRole, "noteHeight"}, {ColorRole, "noteColor"},
            {TextSizeRole, "noteTextSize"}, {CollapsedRole, "noteCollapsed"},
            {FloatingRole, "noteFloating"}, {OrderRole, "noteOrder"}, {CreatedRole, "noteCreated"}};
}
int StickyNotes::create(int x, int y, const QString &color)
{
    if (m_notes.size() >= maximumNotes)
    {
        emit errorOccurred("There are already 100 notes in Stickies. Delete one to make another.");
        return 0;
    }
    Note note;
    note.id = m_nextId++;
    note.x = std::clamp(x, -maximumPosition, maximumPosition);
    note.y = std::clamp(y, -maximumPosition, maximumPosition);
    note.color = colors().contains(color) ? color : QString("yellow");
    note.created = QDateTime::currentDateTime();
    beginInsertRows({}, int(m_notes.size()), int(m_notes.size()));
    m_notes.append(note);
    endInsertRows();
    raise(note.id);
    if (m_hidden)
        setHidden(false);
    emit countChanged();
    scheduleSave();
    return note.id;
}
void StickyNotes::setText(int id, const QString &text)
{
    const auto row = rowOf(id);
    if (row < 0 || m_notes[row].text == text)
        return;
    m_notes[row].text = text.left(maximumText);
    // No dataChanged: the note that is typing shows its own text, and a model change would move
    // its cursor. The text is saved a moment later.
    scheduleSave();
}
void StickyNotes::move(int id, int x, int y)
{
    const auto row = rowOf(id);
    if (row < 0)
        return;
    m_notes[row].x = std::clamp(x, -maximumPosition, maximumPosition);
    m_notes[row].y = std::clamp(y, -maximumPosition, maximumPosition);
    changed(row, {XRole, YRole});
}
void StickyNotes::resize(int id, int width, int height)
{
    const auto row = rowOf(id);
    if (row < 0)
        return;
    m_notes[row].width = std::clamp(width, minimumWidth, maximumSide);
    m_notes[row].height = std::clamp(height, minimumHeight, maximumSide);
    changed(row, {WidthRole, HeightRole});
}
bool StickyNotes::set(int id, const QString &key, const QVariant &value)
{
    const auto row = rowOf(id);
    if (row < 0)
        return false;
    auto &note = m_notes[row];
    if (key == "color" && colors().contains(value.toString()))
        note.color = value.toString(), changed(row, {ColorRole});
    else if (key == "textSize" && textSizes().contains(value.toString()))
        note.textSize = value.toString(), changed(row, {TextSizeRole});
    else if (key == "collapsed" && value.typeId() == QMetaType::Bool)
        note.collapsed = value.toBool(), changed(row, {CollapsedRole});
    else if (key == "floating" && value.typeId() == QMetaType::Bool)
        note.floating = value.toBool(), changed(row, {FloatingRole});
    else
        return false;
    return true;
}
void StickyNotes::raise(int id)
{
    const auto row = rowOf(id);
    if (row < 0)
        return;
    // Renumbered 1..n with this note last, so the numbers stay small (they become z values).
    QList<int> rows(m_notes.size());
    std::iota(rows.begin(), rows.end(), 0);
    std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) { return m_notes[a].order < m_notes[b].order; });
    rows.removeOne(row);
    rows.append(row);
    for (int rank = 0; rank < rows.size(); ++rank)
        m_notes[rows[rank]].order = rank + 1;
    if (!m_notes.isEmpty())
        emit dataChanged(index(0), index(int(m_notes.size()) - 1), {OrderRole});
    scheduleSave();
}
void StickyNotes::remove(int id)
{
    const auto row = rowOf(id);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_notes.removeAt(row);
    endRemoveRows();
    emit countChanged();
    scheduleSave();
}
void StickyNotes::setHidden(bool hidden)
{
    if (m_hidden == hidden)
        return;
    m_hidden = hidden;
    emit hiddenChanged();
    scheduleSave();
}
QString StickyNotes::text(int id) const
{
    const auto row = rowOf(id);
    return row < 0 ? QString() : m_notes[row].text;
}
