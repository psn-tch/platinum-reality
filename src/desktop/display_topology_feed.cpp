#include "display_topology_feed.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <unistd.h>

std::optional<DisplaySnapshot> parseDisplaySnapshot(const QByteArray &bytes, QString *error)
{
    if(error) error->clear();
    const auto reject=[&](const QString &message)->std::optional<DisplaySnapshot>{
        if(error) *error=message;
        return std::nullopt;
    };
    if(bytes.isEmpty() || bytes.size()>65536) return reject("Display snapshot size is invalid.");
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(bytes,&parseError);
    if(parseError.error!=QJsonParseError::NoError || !document.isObject()) return reject("Invalid display snapshot JSON.");
    const auto root=document.object();
    if(root.value("version")!=QJsonValue(1)) return reject("Unsupported display snapshot version.");
    const auto session=root.value("session").toString();
    static const QRegularExpression sessionId("^[0-9a-f]{32}$");
    if(!sessionId.match(session).hasMatch()) return reject("Invalid display session identity.");
    const auto generation=root.value("generation");
    const double serial=generation.toDouble(-1);
    if(!generation.isDouble() || serial<1 || serial>9007199254740991.0 || std::floor(serial)!=serial)
        return reject("Invalid display snapshot generation.");
    const auto modeName=root.value("mode").toString();
    if(modeName!="extend" && modeName!="mirror") return reject("Invalid display mode.");
    if(!root.value("primary").isString() || !root.value("outputs").isArray())
        return reject("Missing primary display or output list.");
    QVector<DisplayOutput> outputs;
    for(const auto &value:root.value("outputs").toArray()) {
        if(!value.isObject()) return reject("Invalid display output record.");
        const auto output=value.toObject();
        int coordinates[4];
        const QStringList keys{"x","y","width","height"};
        for(int i=0;i<4;++i) {
            const auto number=output.value(keys[i]);
            const double n=number.toDouble(1000000);
            if(!number.isDouble() || std::floor(n)!=n || n < -16384 || n>16384)
                return reject("Invalid logical display coordinate.");
            coordinates[i]=int(n);
        }
        if(!output.value("name").isString() || !output.value("transform").isString() || !output.value("scale").isDouble())
            return reject("Missing display identity, scale or rotation.");
        outputs.append({output.value("name").toString(),
                        QRect(coordinates[0],coordinates[1],coordinates[2],coordinates[3]),
                        output.value("scale").toDouble(),output.value("transform").toString()});
    }
    QSize canvas;
    if(root.contains("canvas")) {
        const auto c=root.value("canvas").toObject();
        const double w=c.value("width").toDouble(-1),h=c.value("height").toDouble(-1);
        if(!root.value("canvas").isObject() || w<80 || h<80 || w>16384 || h>16384 || std::floor(w)!=w || std::floor(h)!=h)
            return reject("Invalid display canvas.");
        canvas=QSize(int(w),int(h));
    }
    QString geometryError;
    auto layout=DisplayLayout::create(outputs,root.value("primary").toString(),
                modeName=="extend"?DisplayLayout::Mode::Extend:DisplayLayout::Mode::Mirror,&geometryError,canvas);
    if(!layout) return reject(geometryError);
    DisplayDetails details;
    static const QRegularExpression connector("^[A-Za-z0-9_.-]{1,64}$");
    const auto label=[](const QJsonValue &value){
        return value.isString() && !value.toString().isEmpty() && value.toString().size()<=64;
    };
    if(root.contains("labels")) {
        if(!root.value("labels").isObject()) return reject("Invalid display names.");
        const auto labels=root.value("labels").toObject();
        for(auto it=labels.begin();it!=labels.end();++it) {
            if(!connector.match(it.key()).hasMatch() || !label(it.value())) return reject("Invalid display name.");
            details.labels.insert(it.key(),it.value().toString());
        }
        details.managed=true;
    }
    if(root.contains("available")) {
        const auto available=root.value("available").toArray();
        if(!root.value("available").isArray() || available.size()>16) return reject("Invalid list of displays that are off.");
        for(const auto &value:available) {
            const auto output=value.toObject();
            const auto name=output.value("name").toString();
            if(!value.isObject() || !connector.match(name).hasMatch() || !label(output.value("label")) ||
               !output.value("lid_closed").isBool() ||
               std::any_of(outputs.cbegin(),outputs.cend(),[&](const DisplayOutput &o){return o.name==name;}))
                return reject("Invalid display that is off.");
            details.available.append(QVariantMap{{"name",name},{"label",output.value("label").toString()},
                                                 {"lidClosed",output.value("lid_closed").toBool()}});
        }
    }
    if(root.contains("modes")) {
        if(!root.value("modes").isObject()) return reject("Invalid display modes.");
        const auto all=root.value("modes").toObject();
        const auto mode=[](const QJsonValue &value)->QVariant{
            const auto m=value.toObject();
            const double w=m.value("width").toDouble(-1),h=m.value("height").toDouble(-1),r=m.value("refresh").toDouble(-1);
            if(!value.isObject() || w<80 || w>16384 || h<80 || h>16384 || r<0 || r>1000000 ||
               std::floor(w)!=w || std::floor(h)!=h || std::floor(r)!=r)
                return {};
            return QVariantMap{{"width",int(w)},{"height",int(h)},{"refresh",int(r)}};
        };
        for(auto it=all.begin();it!=all.end();++it) {
            const auto entry=it.value().toObject();
            const auto list=entry.value("modes").toArray();
            if(!connector.match(it.key()).hasMatch() || !it.value().isObject() || !entry.value("modes").isArray() ||
               list.size()>64 || std::none_of(outputs.cbegin(),outputs.cend(),[&](const DisplayOutput &o){return o.name==it.key();}))
                return reject("Invalid display modes.");
            QVariantList modes;
            for(const auto &value:list) {
                const auto parsed=mode(value);
                if(!parsed.isValid()) return reject("Invalid display mode.");
                modes.append(parsed);
            }
            const auto current=entry.contains("mode")?mode(entry.value("mode")):QVariant();
            if(entry.contains("mode") && !current.isValid()) return reject("Invalid current display mode.");
            details.modes.insert(it.key(),QVariantMap{{"modes",modes},{"mode",current}});
        }
    }
    if(root.contains("mirrors")) {
        const auto mirrors=root.value("mirrors").toArray();
        if(!root.value("mirrors").isArray() || mirrors.size()>16 || !canvas.isValid())
            return reject("Invalid mirrored displays.");
        for(const auto &value:mirrors) {
            const auto mirror=value.toObject();
            const auto name=mirror.value("name").toString();
            if(!value.isObject() || !connector.match(name).hasMatch() || !label(mirror.value("label")) ||
               std::any_of(outputs.cbegin(),outputs.cend(),[&](const DisplayOutput &o){return o.name==name;}))
                return reject("Invalid mirrored display.");
            details.mirrors.append(QVariantMap{{"name",name},{"label",mirror.value("label").toString()}});
        }
    }
    if(root.contains("pending")) {
        if(!root.value("pending").isBool()) return reject("Invalid pending display change.");
        details.pending=root.value("pending").toBool();
    }
    return DisplaySnapshot{session,quint64(serial),*layout,details};
}

DisplayTopologyFeed::DisplayTopologyFeed(QString path,QObject *parent):QObject(parent),m_path(std::move(path))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(25);
    connect(&m_debounce,&QTimer::timeout,this,&DisplayTopologyFeed::refresh);
    const auto schedule=[this]{m_debounce.start();};
    connect(&m_watcher,&QFileSystemWatcher::directoryChanged,this,schedule);
    connect(&m_watcher,&QFileSystemWatcher::fileChanged,this,schedule);
    const QFileInfo parentInfo(QFileInfo(m_path).absolutePath());
    const auto publicBits=QFileDevice::ReadGroup|QFileDevice::WriteGroup|QFileDevice::ExeGroup|
                          QFileDevice::ReadOther|QFileDevice::WriteOther|QFileDevice::ExeOther;
    if(QFileInfo(m_path).isAbsolute() && parentInfo.isDir() && !parentInfo.isSymLink() &&
       parentInfo.ownerId()==geteuid() && !(parentInfo.permissions()&publicBits))
        m_watcher.addPath(parentInfo.absoluteFilePath());
}
bool DisplayTopologyFeed::fail(const QString &message)
{
    if(m_error!=message) { m_error=message; emit errorOccurred(message); }
    return false;
}
bool DisplayTopologyFeed::refresh()
{
    const QFileInfo info(m_path), parentInfo(info.absolutePath());
    const auto publicBits=QFileDevice::ReadGroup|QFileDevice::WriteGroup|QFileDevice::ExeGroup|
                          QFileDevice::ReadOther|QFileDevice::WriteOther|QFileDevice::ExeOther;
    if(!info.isAbsolute() || !parentInfo.isDir() || parentInfo.isSymLink() || parentInfo.ownerId()!=geteuid() ||
       (parentInfo.permissions()&publicBits) || !info.isFile() || info.isSymLink() || info.ownerId()!=geteuid() ||
       (info.permissions()&publicBits) || info.size()<=0 || info.size()>65536)
        return fail("Display topology must be a bounded file in an owned private session directory.");
    QFile file(m_path);
    if(!file.open(QIODevice::ReadOnly)) return fail("Cannot read display topology.");
    if(!m_watcher.files().contains(m_path)) m_watcher.addPath(m_path);
    const auto bytes=file.read(65537);
    QString parseError;
    auto next=parseDisplaySnapshot(bytes,&parseError);
    if(!next) return fail(parseError);
    if(m_snapshot) {
        if(next->session!=m_snapshot->session) return fail("Display topology session identity changed.");
        if(next->generation<m_snapshot->generation) return fail("Stale display topology generation.");
        if(next->generation==m_snapshot->generation) {
            if(bytes!=m_bytes) return fail("Display topology changed without a new generation.");
            m_error.clear();
            return true;
        }
    }
    m_snapshot=std::move(next);
    m_bytes=bytes;
    m_error.clear();
    emit changed();
    return true;
}
bool DisplayTopologyFeed::acknowledge(quint64 generation)
{
    if(!m_snapshot || m_snapshot->generation!=generation) return fail("Cannot acknowledge a stale display snapshot.");
    const QString path=m_path+".applied";
    if(QFileInfo(path).isSymLink()) return fail("Display acknowledgement cannot be a symbolic link.");
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner))
        return fail("Cannot create display acknowledgement.");
    const QJsonObject reply{{"version",1},{"session",m_snapshot->session},{"generation",double(generation)}};
    const auto bytes=QJsonDocument(reply).toJson(QJsonDocument::Compact);
    if(file.write(bytes)!=bytes.size() || !file.commit()) return fail("Cannot save display acknowledgement.");
    m_error.clear();
    return true;
}
