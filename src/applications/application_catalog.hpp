#pragma once
#include "desktop_entry.hpp"
#include <QAbstractListModel>
#include <QMap>
#include <QMimeType>

class ApplicationCatalog final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY filterChanged)
    Q_PROPERTY(QString document READ document WRITE setDocument NOTIFY filterChanged)
    Q_PROPERTY(bool showAll READ showAll WRITE setShowAll NOTIFY filterChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY stateChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, DescriptionRole, LaunchableRole, ReasonRole };
    ApplicationCatalog(QStringList directories, QString workspace, QString profile,
                         QString binaryDirectory, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_visible.size()); }
    QString query() const { return m_query; }
    QString document() const { return m_document; }
    bool showAll() const { return m_showAll; }
    bool busy() const { return m_busy; }
    bool ready() const { return m_ready; }
    QString notice() const { return m_notice; }
    void setQuery(QString);
    void setDocument(QString);
    void setShowAll(bool);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap item(int row) const;
    frankenstein::applications::LaunchPlan launchPlan(const QString &id,
        const QStringList &documents, const QProcessEnvironment &environment) const;
    // Only reserved in-memory entries may request a desktop panel. Desktop files
    // cannot provide actions, and these entries never handle documents.
    QString builtInAction(const QString &id) const;
    QString preferredApplication(const QString &document) const;
    bool rememberApplication(const QString &id, const QString &document, QString &error);
    QString applicationName(const QString &waylandId) const;
  signals:
    void filterChanged();
    void stateChanged();
    void countChanged();
    void refreshed();
  private:
    void filter();
    QString reason(const frankenstein::applications::DesktopEntry &) const;
    bool matchesDocument(const frankenstein::applications::DesktopEntry &) const;
    const frankenstein::applications::DesktopEntry *find(const QString &id) const;
    QStringList m_directories;
    QString m_workspace, m_profile, m_binaryDirectory, m_query, m_document, m_notice;
    QVector<frankenstein::applications::DesktopEntry> m_entries;
    QVector<int> m_visible;
    QMap<QString, QString> m_associations;
    QMimeType m_documentType;
    bool m_busy = false, m_ready = false, m_showAll = false;
};
