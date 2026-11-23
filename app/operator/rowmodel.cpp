#include "rowmodel.h"

namespace lumen::app {

RowModel::RowModel(QStringList roles, QObject *parent)
    : QAbstractListModel(parent), m_roles(std::move(roles))
{}

void RowModel::setRows(QList<QVariantMap> rows)
{
    const bool countChanges = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (countChanges)
        emit countChanged();
}

void RowModel::updateRows(const QList<QVariantMap> &rows)
{
    if (rows.size() != m_rows.size()) {
        setRows(rows);
        return;
    }
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (rows.at(i) == m_rows.at(i))
            continue;
        m_rows[i] = rows.at(i);
        emit dataChanged(index(int(i)), index(int(i)));
    }
}

QVariantMap RowModel::get(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : QVariantMap();
}

int RowModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant RowModel::data(const QModelIndex &index, int role) const
{
    const int column = role - Qt::UserRole;
    if (!index.isValid() || index.row() >= m_rows.size() || column < 0 || column >= m_roles.size())
        return {};
    return m_rows.at(index.row()).value(m_roles.at(column));
}

QHash<int, QByteArray> RowModel::roleNames() const
{
    QHash<int, QByteArray> names;
    for (qsizetype i = 0; i < m_roles.size(); ++i)
        names.insert(Qt::UserRole + int(i), m_roles.at(i).toUtf8());
    return names;
}

} // namespace lumen::app
