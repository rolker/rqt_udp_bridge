#ifndef RQT_UDP_BRIDGE_BRIDGE_NODE_H
#define RQT_UDP_BRIDGE_BRIDGE_NODE_H

#include <chrono>
#include <QObject>
#include <QStandardItemModel>
#include <QTimer>
#include "rclcpp/rclcpp.hpp"
#include "udp_bridge_interfaces/msg/bridge_info.hpp"
#include "udp_bridge_interfaces/msg/topic_statistics_array.hpp"
#include "udp_bridge_interfaces/srv/add_remote.hpp"
#include "udp_bridge_interfaces/srv/subscribe.hpp"

namespace rqt_udp_bridge
{

class BridgeNode: public QObject
{
  Q_OBJECT
public:
  BridgeNode(QObject* parent = nullptr);
  ~BridgeNode();

  /// Set the path to where the bridge_info and topic_statistics topics are located.
  /// @param local True if represents udp_bridge running locally.
  void setTopicsPrefix(rclcpp::Node::SharedPtr node, std::string node_namespace, bool local);

  void clear();

  QStandardItemModel* topicsModel();
  QStandardItemModel* remotesModel();
  QStandardItemModel* remoteTopicsModel(const std::string &remote);
  QStandardItemModel* remoteRemotesModel(const std::string &remote);
  BridgeNode* remoteBridgeNode(const std::string &remote);

  bool addRemote(std::shared_ptr<udp_bridge_interfaces::srv::AddRemote::Request> add_remote);
  bool remoteAdvertise(std::shared_ptr<udp_bridge_interfaces::srv::Subscribe::Request>  subscribe);
  bool remoteSubscribe(std::shared_ptr<udp_bridge_interfaces::srv::Subscribe::Request> subscribe);

  QStringList topics();
  QStringList remotes();
  QStringList connections(const std::string &remote);
  QStringList remoteTopics(const std::string &remote);

signals:
  void remoteDetailsUpdated(QString remote, QString connection, QString details);

private slots:
  void bridgeInfoUpdated();
  void topicStatisticsUpdated();
  void checkStaleness();

private:
  void bridgeInfoCallback(udp_bridge_interfaces::msg::BridgeInfo::UniquePtr bridge_info);
  void topicStatisticsCallback(udp_bridge_interfaces::msg::TopicStatisticsArray::UniquePtr topic_statistics_array);

  using TimePoint = std::chrono::steady_clock::time_point;
  static constexpr int TimestampRole = Qt::UserRole + 1;

  void stampItem(QStandardItem* item);
  void stampChildData(QStandardItem* parent, int row, int column_count, int start_column = 1);
  void checkModelStaleness(QStandardItemModel& model, int data_column_count, int start_column = 1);
  void markItemStale(QStandardItem* item, bool stale);

  static constexpr std::chrono::seconds stale_timeout_{5};

  /// name of the bridge node as reported by BridgeInfo message
  std::string name_;

  /// namespace used to subscribe to information topics
  std::string node_namespace_;
  rclcpp::Node::SharedPtr node_;

  std::map<std::string, BridgeNode*> remotes_;

  /// Flag indicating if this represents a locally running udp_bridge node.
  bool local_ = false;

  rclcpp::Subscription<udp_bridge_interfaces::msg::BridgeInfo>::SharedPtr bridge_info_subscriber_;
  rclcpp::Subscription<udp_bridge_interfaces::msg::TopicStatisticsArray>::SharedPtr topic_statistics_subscriber_;

  rclcpp::Client<udp_bridge_interfaces::srv::AddRemote>::SharedPtr add_remote_service_;
  rclcpp::Client<udp_bridge_interfaces::srv::Subscribe>::SharedPtr remote_advertise_service_;
  rclcpp::Client<udp_bridge_interfaces::srv::Subscribe>::SharedPtr remote_subscribe_service_;

  QStandardItemModel topics_model_;
  QStandardItemModel remotes_model_;

  udp_bridge_interfaces::msg::BridgeInfo bridge_info_;
  std::mutex bridge_info_mutex_;

  udp_bridge_interfaces::msg::TopicStatisticsArray topic_statistics_array_;
  std::mutex topic_statistics_array_mutex_;

  QTimer stale_timer_;
};

} // namespace rqt_udp_plugin

#endif
