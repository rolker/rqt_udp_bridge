#include "rqt_udp_bridge/bridge_node.h"
#include "udp_bridge_interfaces/srv/add_remote.hpp"
#include "udp_bridge_interfaces/srv/subscribe.hpp"

namespace rqt_udp_bridge
{

using namespace udp_bridge_interfaces::msg;
using namespace udp_bridge_interfaces::srv;

BridgeNode::BridgeNode(QObject* parent):
  QObject(parent)
{
}

BridgeNode::~BridgeNode()
{
  clear();
}

void BridgeNode::clear()
{
  local_ = false;
  node_namespace_.clear();
  topic_statistics_subscriber_.reset();
  bridge_info_subscriber_.reset();
  add_remote_service_.reset();
  remote_advertise_service_.reset();
  remote_subscribe_service_.reset();
  topics_model_.clear();
  remotes_model_.clear();

  for(auto remote: remotes_)
    delete remote.second;
  remotes_.clear();

}

void BridgeNode::setTopicsPrefix(rclcpp::Node::SharedPtr node, std::string node_namespace, bool local)
{
  clear();

  local_ = local;
  node_namespace_ = node_namespace;

  QStringList labels;
  labels.push_back("topic/remote/connection");
  labels.push_back("messages");
  labels.push_back("msg bytes");
  labels.push_back("sent bytes");
  labels.push_back("failed bytes");
  labels.push_back("dropped bytes");
  labels.push_back("avg fragment count");
  topics_model_.setHorizontalHeaderLabels(labels);

  QStringList remotes_labels;
  remotes_labels.push_back("remote/connection");
  remotes_labels.push_back("received");
  remotes_labels.push_back("duplicate");
  remotes_labels.push_back("msg sent ok");
  remotes_labels.push_back("msg sent fail");
  remotes_labels.push_back("msg sent drop");
  remotes_labels.push_back("ovrhd sent ok");
  remotes_labels.push_back("ovrhd sent fail");
  remotes_labels.push_back("ovrhd sent drop");
  remotes_labels.push_back("resend sent ok");
  remotes_labels.push_back("resend sent fail");
  remotes_labels.push_back("resend sent drop");
  remotes_model_.setHorizontalHeaderLabels(remotes_labels);

  topic_statistics_subscriber_ = node->create_subscription<TopicStatisticsArray>(node_namespace+"/topic_statistics", 1, std::bind(&BridgeNode::topicStatisticsCallback, this, std::placeholders::_1));
  bridge_info_subscriber_ = node->create_subscription<BridgeInfo>(node_namespace+"/bridge_info", 1, std::bind(&BridgeNode::bridgeInfoCallback, this, std::placeholders::_1));

  if(local_)
  {
    add_remote_service_ = node->create_client<AddRemote>(node_namespace+"/add_remote");
    remote_advertise_service_ = node->create_client<Subscribe>(node_namespace+"/remote_advertise");
    remote_subscribe_service_ = node->create_client<Subscribe>(node_namespace+"/remote_subscribe");
  }
}

QStandardItemModel* BridgeNode::topicsModel()
{
  return &topics_model_;
}

QStandardItemModel* BridgeNode::remotesModel()
{
  return &remotes_model_;
}

QStandardItemModel* BridgeNode::remoteTopicsModel(const std::string &remote)
{
  auto remote_node = remotes_.find(remote);
  if(remote_node == remotes_.end())
    return nullptr;
  return remote_node->second->topicsModel();
}

QStandardItemModel* BridgeNode::remoteRemotesModel(const std::string &remote)
{
  auto remote_node = remotes_.find(remote);
  if(remote_node == remotes_.end())
    return nullptr;
  return remote_node->second->remotesModel();
}

BridgeNode* BridgeNode::remoteBridgeNode(const std::string &remote)
{
  auto remote_node = remotes_.find(remote);
  if(remote_node == remotes_.end())
    return nullptr;
  return remote_node->second;
}

QString humanReadableDataRate(float rate)
{
  std::vector<QString> units = {"Bps","KBps","MBps"};
  for(auto u: units)
  {
    if(rate < 1024)
      return QString::number(rate)+" "+u;
    rate /= 1024.0;
  }
  return QString::number(rate)+" GBps";
}

void setChildData(QStandardItem* parent, int row, const std::vector<QString> &values, int start_column = 1)
{
  for(int i = 0; i < values.size(); i++)
  {
    int column = start_column+i;
    if(!parent->child(row, column))
      parent->setChild(row, column, new QStandardItem(values[i]));
    else
      parent->child(row, column)->setData(values[i], Qt::DisplayRole);
  }
}

void setData(QStandardItemModel& model, int row, const std::vector<QString> &values, int start_column = 1)
{
  for(int i = 0; i < values.size(); i++)
  {
    int column = start_column+i;
    if(model.item(row, column) == nullptr)
      model.setItem(row, column, new QStandardItem(values[i]));
    else
      model.item(row, column)->setData(values[i], Qt::DisplayRole);
  }
}

void BridgeNode::bridgeInfoCallback(BridgeInfo::UniquePtr bridge_info)
{
  std::lock_guard<std::mutex> lock(bridge_info_mutex_);
  bridge_info_ = *bridge_info;
  QMetaObject::invokeMethod(this, &BridgeNode::bridgeInfoUpdated);
}

void BridgeNode::bridgeInfoUpdated()
{
  std::lock_guard<std::mutex> lock(bridge_info_mutex_);
  name_ = bridge_info_.name;

  std::map<std::string, bool> existing_topics;
  for(int i = 0; i < topics_model_.rowCount(); i++)
  {
    auto item = topics_model_.item(i);
    if(item)
    {
      std::string topic = item->data(Qt::DisplayRole).toString().toStdString();
      existing_topics[topic] = false;
    }
  }

  for(const auto& topic: bridge_info_.topics)
  {
    existing_topics[topic.topic] = true;
    auto items = topics_model_.findItems(topic.topic.c_str());
    QStandardItem* item = nullptr;
    for(auto i: items)
      if(i->parent() == nullptr) // make sure it's a top level item
      {
        item = i;
        break;
      }
    if(!item)
    {
      item = new QStandardItem(topic.topic.c_str());
      topics_model_.appendRow(item);
    }
    for(const auto& remote: topic.remotes)
    {
      if(remote.remote.empty())
        continue;
      QStandardItem* remote_item = nullptr;
      for(auto i = 0; i < item->rowCount(); i++)
        if(item->child(i)->data(Qt::DisplayRole).toString().toStdString() == remote.remote)
        {
          remote_item = item->child(i);
          break;
        }
      if(remote_item == nullptr)
      {
        remote_item = new QStandardItem(remote.remote.c_str());
        item->appendRow(remote_item);
      }
      for(const auto& connection: remote.connections)
      {
        QStandardItem* connection_item = nullptr;
        for(auto i = 0; i < remote_item->rowCount(); i++)
          if(remote_item->child(i)->data(Qt::DisplayRole).toString().toStdString() == connection.connection_id)
          {
            connection_item = remote_item->child(i);
            break;
          }
        if(!connection_item)
        {
          connection_item = new QStandardItem(connection.connection_id.c_str());
          remote_item->appendRow(connection_item);
        }
      }


    }

  }


  for(auto topic: existing_topics)
    if(topic.second == false) // not in the new set of topics
    {
      auto items = topics_model_.findItems(topic.first.c_str());
      for(auto i: items)
        if(i->parent() == nullptr) // make sure it's a top level item
        {
          topics_model_.removeRow(topics_model_.indexFromItem(i).row());
          break;
        }
    }

  topics_model_.sort(0);

  std::map<std::string, bool> existing_remotes;
  for(int i = 0; i < remotes_model_.rowCount(); i++)
  {
    auto item = remotes_model_.item(i);
    if(item)
    {
      std::string remote = item->data(Qt::DisplayRole).toString().toStdString();
      existing_remotes[remote] = false;
    }
  }

  for(const auto& remote: bridge_info_.remotes)
  {
    existing_remotes[remote.name] = true;
    auto items = remotes_model_.findItems(remote.name.c_str());
    QStandardItem* item = nullptr;
    for(auto i: items)
      if(i->parent() == nullptr) // make sure it's a top level item
      {
        item = i;
        break;
      }
    if(!item)
    {
      item = new QStandardItem(remote.name.c_str());
      remotes_model_.appendRow(item);
    }
    for(const auto& connection: remote.connections)
    {
      QStandardItem* connection_item = nullptr;
      for(auto i = 0; i < item->rowCount(); i++)
        if(item->child(i)->data(Qt::DisplayRole).toString().toStdString() == connection.connection_id)
        {
          connection_item = item->child(i);
          break;
        }
      if(!connection_item)
      {
        connection_item = new QStandardItem(connection.connection_id.c_str());
        item->appendRow(connection_item);
      }
      std::stringstream tooltip;
      tooltip << "remote: " << remote.name << " connection: " << connection.connection_id << " host: " << connection.host << " port: " << connection.port << " ip address: " << connection.ip_address << " return host: " << connection.return_host << " return port: " << connection.return_port << " source ip: " << connection.source_ip_address << " source port: " << connection.source_port << " max rate: " << connection.maximum_bytes_per_second << " bytes/sec";
      connection_item->setData(tooltip.str().c_str(), Qt::ToolTipRole);
      emit remoteDetailsUpdated(remote.name.c_str(), connection.connection_id.c_str(), tooltip.str().c_str());
      std::vector<QString> values;
      values.push_back(humanReadableDataRate(connection.received_bytes_per_second));
      values.push_back(humanReadableDataRate(connection.duplicate_bytes_per_second));
      values.push_back(humanReadableDataRate(connection.message.success_bytes_per_second));
      values.push_back(humanReadableDataRate(connection.message.failed_bytes_per_second));
      values.push_back(humanReadableDataRate(connection.message.dropped_bytes_per_second));
      values.push_back(humanReadableDataRate(connection.overhead.success_bytes_per_second));
      values.push_back(humanReadableDataRate(connection.overhead.failed_bytes_per_second));
      values.push_back(humanReadableDataRate(connection.overhead.dropped_bytes_per_second));
      values.push_back(humanReadableDataRate(connection.resend.success_bytes_per_second));
      values.push_back(humanReadableDataRate(connection.resend.failed_bytes_per_second));
      values.push_back(humanReadableDataRate(connection.resend.dropped_bytes_per_second));
      setChildData(item, connection_item->row(), values);
    }

    if(local_)
    {
      auto remote_iterator = remotes_.find(remote.name);
      if(remote_iterator == remotes_.end())
      {
        remotes_[remote.name] = new BridgeNode(this);
        remotes_[remote.name]->setTopicsPrefix(node_, node_namespace_+"/remotes/"+remote.topic_name, false);
      }
    }

  }
}

void BridgeNode::topicStatisticsCallback(TopicStatisticsArray::UniquePtr topic_statistics_array)
{
  std::lock_guard<std::mutex> lock(topic_statistics_array_mutex_);
  topic_statistics_array_ = *topic_statistics_array;
  QMetaObject::invokeMethod(this, &BridgeNode::topicStatisticsUpdated);
}

void BridgeNode::topicStatisticsUpdated()
{
  std::lock_guard<std::mutex> lock(topic_statistics_array_mutex_);
  for(const auto& topic_statistics: topic_statistics_array_.topics)
    if(!name_.empty() && topic_statistics.source_node == name_)
    {
      QStandardItem* topic_item = nullptr;
      auto items = topics_model_.findItems(topic_statistics.source_topic.c_str());
      for(auto i: items)
        if(i->parent() == nullptr) // make sure it's a top level item
        {
          topic_item = i;
          break;
        }
      if(topic_item)
      {
        if(topic_statistics.destination_node.empty())
        {
          std::vector<QString> values;
          values.push_back(QString::number(topic_statistics.messages_per_second)+"/s");
          values.push_back(humanReadableDataRate(topic_statistics.message_bytes_per_second));
          setData(topics_model_, topic_item->row(), values);
        }
        else
        {
          QStandardItem* remote_item = nullptr;
          for(auto i = 0; i < topic_item->rowCount(); i++)
            if(topic_item->child(i)->data(Qt::DisplayRole).toString().toStdString() == topic_statistics.destination_node)
            {
              remote_item = topic_item->child(i);
              break;
            }
          if(remote_item)
          {
            QStandardItem* connection_item = nullptr;
            for(auto i = 0; i < remote_item->rowCount(); i++)
              if(remote_item->child(i)->data(Qt::DisplayRole).toString().toStdString() == topic_statistics.connection_id)
              {
                connection_item = remote_item->child(i);
                break;
              }
            if(connection_item)
            {
              std::vector<QString> values;
              values.push_back(QString::number(topic_statistics.messages_per_second)+"/s");
              values.push_back(humanReadableDataRate(topic_statistics.message_bytes_per_second));
              values.push_back(humanReadableDataRate(topic_statistics.send.success_bytes_per_second));
              values.push_back(humanReadableDataRate(topic_statistics.send.failed_bytes_per_second));
              values.push_back(humanReadableDataRate(topic_statistics.send.dropped_bytes_per_second));
              values.push_back(QString::number(topic_statistics.average_fragment_count));
              setChildData(remote_item, connection_item->row(), values);
            }
          }
        }
      }
    }


}

bool BridgeNode::addRemote(std::shared_ptr<AddRemote::Request> add_remote)
{
  auto result = add_remote_service_->async_send_request(add_remote);
  // Wait for the result.
  if (rclcpp::spin_until_future_complete(node_, result) ==
    rclcpp::FutureReturnCode::SUCCESS)
  {
    return true;
  } else {
    return false;
  }
}

bool BridgeNode::remoteAdvertise(std::shared_ptr<Subscribe::Request> subscribe)
{
  auto result = remote_advertise_service_->async_send_request(subscribe);
  // Wait for the result.
  if (rclcpp::spin_until_future_complete(node_, result) ==
    rclcpp::FutureReturnCode::SUCCESS)
  {
    return true;
  } else {
    return false;
  }
}

bool BridgeNode::remoteSubscribe(std::shared_ptr<Subscribe::Request> subscribe)
{
  auto result = remote_subscribe_service_->async_send_request(subscribe);
  // Wait for the result.
  if (rclcpp::spin_until_future_complete(node_, result) ==
    rclcpp::FutureReturnCode::SUCCESS)
  {
    return true;
  } else {
    return false;
  }
}

QStringList BridgeNode::topics()
{
  QStringList ret;
  std::lock_guard<std::mutex> lock(bridge_info_mutex_);
  for(auto topic: bridge_info_.topics)
    ret.push_back(topic.topic.c_str());
  return ret;
}

QStringList BridgeNode::remotes()
{
  QStringList ret;
  std::lock_guard<std::mutex> lock(bridge_info_mutex_);
  for(const auto& remote: bridge_info_.remotes)
    ret.push_back(remote.name.c_str());
  return ret;
}

QStringList BridgeNode::connections(const std::string &remote)
{
  QStringList ret;
  std::lock_guard<std::mutex> lock(bridge_info_mutex_);
  for(const auto& r: bridge_info_.remotes)
    if(r.name == remote)
    {
      for(const auto& c: r.connections)
        ret.push_back(c.connection_id.c_str());
      break;
    }
  return ret;
}

QStringList BridgeNode::remoteTopics(const std::string &remote)
{
  auto r = remotes_.find(remote);
  if(r != remotes_.end() && r->second != nullptr)
    return r->second->topics();
  return {};
}

} // namespace rqt_udp_plugin

