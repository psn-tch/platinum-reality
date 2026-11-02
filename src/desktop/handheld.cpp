#include "handheld.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <algorithm>

namespace
{
// A setting in hhd's tree by dotted path ("tdp.qam.tdp"); containers keep theirs under "children".
QJsonObject setting(QJsonObject tree, const QString &path)
{
    for (const auto &part : path.split('.'))
    {
        tree = (tree.contains("children") ? tree.value("children").toObject() : tree).value(part).toObject();
        if (tree.isEmpty())
            return {};
    }
    return tree;
}
QJsonValue stateValue(const QJsonObject &state, const QString &path)
{
    QJsonValue current = state;
    for (const auto &part : path.split('.'))
        current = current.toObject().value(part);
    return current;
}
// A mode setting's modes or a multiple-choice setting's options, as hhd titles them. JSON objects
// lose hhd's order: the familiar controller modes come first, everything else by title.
QVariantList choices(const QJsonObject &setting, const QStringList &first = {})
{
    QVariantList result;
    const bool modes = setting.contains("modes");
    const auto all = setting.value(modes ? "modes" : "options").toObject();
    for (auto it = all.begin(); it != all.end(); ++it)
        result.push_back(QVariantMap{{"value", it.key()},
                                     {"title", modes ? it.value().toObject().value("title").toString(it.key())
                                                     : it.value().toString(it.key())}});
    std::stable_sort(result.begin(), result.end(), [&](const QVariant &a, const QVariant &b)
    {
        const auto ia = first.indexOf(a.toMap().value("value").toString());
        const auto ib = first.indexOf(b.toMap().value("value").toString());
        if (ia != ib)
            return ia >= 0 && (ib < 0 || ia < ib);
        return a.toMap().value("title").toString() < b.toMap().value("title").toString();
    });
    return result;
}
} // namespace

DesktopHandheldEndpoint desktopHandheldEndpoint(bool hasCompositor, const QProcessEnvironment &environment)
{
    const DesktopHandheldEndpoint disabled{QUrl("http://127.0.0.1:9"), QString()};
    if (!hasCompositor)
        return disabled;
    if (environment.value("FRANKENSTEIN_NATIVE_TEST") == "1")
    {
        const auto fixture = environment.value("FRANKENSTEIN_TEST_HHD");
        return fixture.isEmpty() ? disabled
                                : DesktopHandheldEndpoint{QUrl(fixture.section(' ', 0, 0)), fixture.section(' ', 1)};
    }
    // Installed-production-path fixtures cannot set NATIVE_TEST (wallet routing
    // deliberately distinguishes it). This opt-out never redirects a token.
    if (environment.value("PLATINUM_DISABLE_HOST_HANDHELD") == "1")
        return disabled;
    return {QUrl("http://127.0.0.1:5335"), QStringLiteral("/etc/hhd/.token")};
}

HandheldDaemon::HandheldDaemon(QUrl base, QString tokenFile, QObject *parent)
    : QObject(parent), m_base(std::move(base)), m_tokenFile(std::move(tokenFile))
{
    // The token goes only to hhd on this computer: never through a proxy, never after a redirect.
    m_network.setProxy(QNetworkProxy::NoProxy);
}

void HandheldDaemon::request(const QString &path, const QByteArray &body,
                             std::function<void(const QJsonObject &)> done)
{
    QFile file(m_tokenFile);
    const auto token = file.open(QIODevice::ReadOnly) ? file.read(256).trimmed() : QByteArray();
    if (token.isEmpty())
    {
        done({});
        return;
    }
    QNetworkRequest message(m_base.resolved(QUrl(path)));
    message.setRawHeader("Authorization", "Bearer " + token);
    message.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // hhd answers a change after its next loop (up to about 2 s).
    message.setTransferTimeout(10000);
    QNetworkReply *reply;
    if (body.isNull())
        reply = m_network.get(message);
    else
    {
        message.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_network.post(message, body);
    }
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)]
    {
        reply->deleteLater();
        const auto document = QJsonDocument::fromJson(reply->read(4 << 20));
        done(reply->error() == QNetworkReply::NoError && document.isObject() ? document.object() : QJsonObject());
    });
}

void HandheldDaemon::refresh()
{
    if (m_refreshing || m_setting)
        return;
    m_refreshing = true;
    if (!m_available) // "Checking…" until the first answer; later refreshes leave the menu alone
        emit changed();
    const auto generation = m_generation;
    const auto finish = [this, generation](const QJsonObject &state)
    {
        m_refreshing = false;
        if (generation == m_generation)
            apply(state);
        else
            emit changed();
    };
    if (!m_settings.isEmpty())
    {
        request("/api/v1/state", {}, finish);
        return;
    }
    request("/api/v1/settings", {}, [this, finish](const QJsonObject &settings)
    {
        if (settings.isEmpty())
        {
            finish({});
            return;
        }
        m_settings = settings;
        request("/api/v1/state", {}, finish);
    });
}

void HandheldDaemon::apply(const QJsonObject &state)
{
    QVariantMap result;
    if (!state.isEmpty())
    {
        // Top-level sections hold their groups directly; groups keep theirs under "children".
        auto controllers = setting(m_settings, "controllers");
        if (controllers.contains("children"))
            controllers = controllers.value("children").toObject();
        for (auto it = controllers.begin(); it != controllers.end(); ++it)
        {
            const auto path = "controllers." + it.key() + ".controller_mode";
            const auto mode = setting(m_settings, path);
            if (mode.value("type") == "mode" && stateValue(state, path + ".mode").isString())
            {
                result["controller"] = QVariantMap{
                    {"key", path + ".mode"}, {"value", stateValue(state, path + ".mode").toString()},
                    {"options", choices(mode, {"uinput", "xbox_elite", "dualsense", "hidden", "disabled"})}};
                break;
            }
        }
        const auto tdp = setting(m_settings, "tdp.qam.tdp");
        if (tdp.value("type") == "int" && stateValue(state, "tdp.qam.tdp").isDouble())
            result["tdp"] = QVariantMap{{"key", "tdp.qam.tdp"}, {"value", stateValue(state, "tdp.qam.tdp").toInt()},
                                        {"min", tdp.value("min").toInt()}, {"max", tdp.value("max").toInt()}};
        if (setting(m_settings, "tdp.qam.boost").value("type") == "bool" && stateValue(state, "tdp.qam.boost").isBool())
            result["boost"] = QVariantMap{{"key", "tdp.qam.boost"}, {"value", stateValue(state, "tdp.qam.boost").toBool()}};
        const auto fan = setting(m_settings, "tdp.qam.fan");
        if (fan.value("type") == "mode" && stateValue(state, "tdp.qam.fan.mode").isString())
            result["fan"] = QVariantMap{{"key", "tdp.qam.fan.mode"}, {"value", stateValue(state, "tdp.qam.fan.mode").toString()},
                                        {"options", choices(fan, {"disabled"})}};
        const auto charge = setting(m_settings, "tdp.battery.charge_limit");
        if (charge.value("type") == "multiple" && stateValue(state, "tdp.battery.charge_limit").isString())
            result["chargeLimit"] = QVariantMap{{"key", "tdp.battery.charge_limit"},
                                                {"value", stateValue(state, "tdp.battery.charge_limit").toString()},
                                                {"options", choices(charge)}};
    }
    m_available = !result.isEmpty();
    m_state = result;
    emit changed();
}

void HandheldDaemon::set(const QString &key, const QVariant &value)
{
    // Only what the module offers, within hhd's own limits and choices.
    bool allowed = false;
    for (const auto &entry : std::as_const(m_state))
    {
        const auto item = entry.toMap();
        if (item.value("key") != key)
            continue;
        if (item.contains("options"))
            allowed = std::any_of(item.value("options").toList().cbegin(), item.value("options").toList().cend(),
                                  [&](const QVariant &o) { return o.toMap().value("value") == value.toString(); });
        else if (item.contains("min"))
            allowed = value.toInt() >= item.value("min").toInt() && value.toInt() <= item.value("max").toInt();
        else
            allowed = value.typeId() == QMetaType::Bool;
    }
    if (!allowed || m_setting)
        return;
    m_setting = true;
    ++m_generation;
    emit changed();
    const auto body = QJsonDocument(QJsonObject{{key, QJsonValue::fromVariant(
        value.typeId() == QMetaType::Bool || value.typeId() == QMetaType::QString ? value : QVariant(value.toInt()))}})
                          .toJson(QJsonDocument::Compact);
    request("/api/v1/state", body, [this](const QJsonObject &state)
    {
        m_setting = false;
        if (state.isEmpty())
        {
            emit changed();
            emit failed("Handheld Daemon did not accept the change.");
            return;
        }
        apply(state);
    });
}
