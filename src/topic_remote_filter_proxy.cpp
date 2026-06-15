#include "rqt_udp_bridge/topic_remote_filter_proxy.h"

namespace rqt_udp_bridge
{

TopicRemoteFilterProxy::TopicRemoteFilterProxy(QObject* parent):
  QSortFilterProxyModel(parent)
{
  // Keep a matching leaf's ancestors visible; never sort (preserve source order).
  setRecursiveFilteringEnabled(true);
  setDynamicSortFilter(true);
}

void TopicRemoteFilterProxy::setFailureColumns(const QList<int>& columns)
{
  failure_columns_ = columns;
  invalidateFilter();
}

void TopicRemoteFilterProxy::setFilterText(const QString& text)
{
  if(text_ == text)
    return;
  text_ = text;
  invalidateFilter();
}

void TopicRemoteFilterProxy::setHideIdle(bool hide)
{
  if(hide_idle_ == hide)
    return;
  hide_idle_ = hide;
  invalidateFilter();
}

void TopicRemoteFilterProxy::setShowFailing(bool show)
{
  if(show_failing_ == show)
    return;
  show_failing_ = show;
  invalidateFilter();
}

bool TopicRemoteFilterProxy::textMatches(const QModelIndex& source_index0) const
{
  if(text_.isEmpty())
    return true;
  // Match the row itself or any ancestor, so typing a remote name reveals its
  // connection children (recursive filtering only walks toward ancestors).
  for(QModelIndex i = source_index0; i.isValid(); i = i.parent())
    if(i.data(Qt::DisplayRole).toString().contains(text_, Qt::CaseInsensitive))
      return true;
  return false;
}

bool TopicRemoteFilterProxy::rowIsActive(int source_row, const QModelIndex& source_parent) const
{
  const QAbstractItemModel* model = sourceModel();
  for(int col = 1; col < model->columnCount(source_parent); col++)
  {
    QModelIndex idx = model->index(source_row, col, source_parent);
    if(!idx.data(TimestampRole).isValid())
      continue;
    // A valid timestamp with no grey ForegroundRole means the cell is fresh;
    // BridgeNode clears the foreground brush when fresh, sets grey when stale.
    if(!idx.data(Qt::ForegroundRole).isValid())
      return true;
  }
  return false;
}

bool TopicRemoteFilterProxy::rowIsFailing(int source_row, const QModelIndex& source_parent) const
{
  const QAbstractItemModel* model = sourceModel();
  for(int col: failure_columns_)
  {
    QModelIndex idx = model->index(source_row, col, source_parent);
    QString value = idx.data(Qt::DisplayRole).toString();
    if(!value.isEmpty() && value != QStringLiteral("0 Bps"))
      return true;
  }
  return false;
}

bool TopicRemoteFilterProxy::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
  const QAbstractItemModel* model = sourceModel();
  if(!model)
    return true;

  QModelIndex index0 = model->index(source_row, 0, source_parent);
  const bool is_leaf = model->rowCount(index0) == 0;

  if(!is_leaf)
  {
    // No active predicate: show everything (including any empty parents).
    if(text_.isEmpty() && !hide_idle_ && !show_failing_)
      return true;
    // Otherwise defer to recursive filtering: the parent appears only if one of
    // its descendant leaves is accepted below.
    return false;
  }

  if(!textMatches(index0))
    return false;
  if(hide_idle_ && !rowIsActive(source_row, source_parent))
    return false;
  if(show_failing_ && !rowIsFailing(source_row, source_parent))
    return false;
  return true;
}

} // namespace rqt_udp_bridge
