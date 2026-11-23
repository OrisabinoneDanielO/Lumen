#pragma once

#include <QAbstractListModel>
#include <QVariantMap>

namespace lumen::app {

// A simple list model for QML: each row is a map of role name to value.
class RowModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit RowModel(QStringList roles, QObject *parent = nullptr);

    void setRows(QList<QVariantMap> rows);
    int count() const { return int(m_rows.size()); }
    Q_INVOKABLE QVariantMap get(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QStringList m_roles;
    QList<QVariantMap> m_rows;
};

} // namespace lumen::app
