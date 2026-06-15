#include "rqt_udp_bridge/model_navigation.h"

#include <QAbstractItemModel>
#include <QAbstractProxyModel>

namespace rqt_udp_bridge
{

QModelIndex mapToSource(const QModelIndex& index)
{
  if(const auto* proxy = qobject_cast<const QAbstractProxyModel*>(index.model()))
    return proxy->mapToSource(index);
  return index;
}

RemoteConnection remoteConnectionAt(const QModelIndex& index)
{
  auto i = mapToSource(index);
  if(i.isValid() && i.column() != 0)
    i = i.model()->sibling(i.row(), 0, i);

  while(i.isValid())
    if(i.parent().isValid()) // i is not remote
      if(i.parent().parent().isValid()) // i is not connection
        i = i.parent();
      else
        return std::make_pair(i.model()->data(i.parent()).toString().toStdString(), i.model()->data(i).toString().toStdString());
    else // i is remote
      return std::make_pair(i.model()->data(i).toString().toStdString(), std::string());
  return {};
}

TopicRemoteConnection topicRemoteConnectionAt(const QModelIndex& index)
{
  auto i = mapToSource(index);
  if(i.isValid() && i.column() != 0)
    i = i.model()->sibling(i.row(), 0, i);

  while(i.isValid())
    if(i.parent().isValid()) // i is not topic
      if(i.parent().parent().isValid()) // i is not remote
        if(i.parent().parent().parent().isValid()) // i is not connection
          i = i.parent();
        else
          return std::make_pair(i.model()->data(i.parent().parent()).toString().toStdString(), std::make_pair(i.model()->data(i.parent()).toString().toStdString(), i.model()->data(i).toString().toStdString()));
      else // i is remote
        return std::make_pair(i.model()->data(i.parent()).toString().toStdString(), std::make_pair(i.model()->data(i).toString().toStdString(), std::string()));
    else // i is topic
      return std::make_pair(i.model()->data(i).toString().toStdString(), std::make_pair(std::string(), std::string()));
  return {};
}

} // namespace rqt_udp_bridge
