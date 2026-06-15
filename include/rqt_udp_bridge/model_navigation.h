#ifndef RQT_UDP_BRIDGE_MODEL_NAVIGATION_H
#define RQT_UDP_BRIDGE_MODEL_NAVIGATION_H

#include <QModelIndex>
#include <string>
#include <utility>

namespace rqt_udp_bridge
{

using RemoteConnection = std::pair<std::string, std::string>;
using TopicRemoteConnection = std::pair<std::string, RemoteConnection>;

/// If \p index belongs to a proxy model, return the corresponding source index;
/// otherwise return \p index unchanged. Centralizes the proxy→source hop so the
/// tree-walks below always operate in source-model coordinates.
QModelIndex mapToSource(const QModelIndex& index);

/// Walk a remotes-tree index (remote → connection) to its
/// (remote_name, connection_id). connection_id is empty when \p index is a
/// remote (top-level) row. Accepts proxy or source indices.
RemoteConnection remoteConnectionAt(const QModelIndex& index);

/// Walk a topics-tree index (topic → remote → connection) to its
/// (topic, (remote_name, connection_id)). Inner fields are empty at the levels
/// above them. Accepts proxy or source indices.
TopicRemoteConnection topicRemoteConnectionAt(const QModelIndex& index);

} // namespace rqt_udp_bridge

#endif
