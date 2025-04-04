#include "rqt_udp_bridge/udp_bridge_plugin.h"
#include "ui_add_remote_dialog.h"
#include "ui_subscribe_dialog.h"

#include "udp_bridge_interfaces/srv/list_remotes.hpp"
#include "udp_bridge_interfaces/srv/add_remote.hpp"
#include "udp_bridge_interfaces/srv/subscribe.hpp"

#include <pluginlib/class_list_macros.hpp>

#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>

namespace rqt_udp_bridge
{
    
UDPBridgePlugin::UDPBridgePlugin():rqt_gui_cpp::Plugin()
{
  setObjectName("UDPBridge");
}
 
void UDPBridgePlugin::initPlugin(qt_gui_cpp::PluginContext& context)
{
  widget_ = new QWidget();
  ui_.setupUi(widget_);
  ui_.splitter->setHandleWidth(8);
  ui_.splitter->setStyleSheet("QSplitter::handle{background: #3030FF;}");

  if(!bridge_node_)
    bridge_node_ = new BridgeNode(this);
  ui_.localTopicsTreeView->setModel(bridge_node_->topicsModel());
  ui_.remotesTreeView->setModel(bridge_node_->remotesModel());

  widget_->setWindowTitle(widget_->windowTitle() + " (" + QString::number(context.serialNumber()) + ")");
  
  context.addWidget(widget_);
  
  updateNodeList();
  ui_.nodesComboBox->setCurrentIndex(ui_.nodesComboBox->findText(""));
  connect(ui_.nodesComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &UDPBridgePlugin::onNodeChanged);

  ui_.refreshNodesPushButton->setIcon(QIcon::fromTheme("view-refresh"));
  connect(ui_.refreshNodesPushButton, &QPushButton::pressed, this, &UDPBridgePlugin::updateNodeList);

  connect(ui_.remotesTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &UDPBridgePlugin::currentRemoteChanged);
  connect(ui_.localTopicsTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &UDPBridgePlugin::currentLocalTopicChanged);

  connect(bridge_node_, &BridgeNode::remoteDetailsUpdated, this, & UDPBridgePlugin::updateCurrentRemoteDetails);
  
  connect(ui_.addRemotePushButton, &QPushButton::pressed, this, &UDPBridgePlugin::addRemote);
  connect(ui_.advertisePushButton, &QPushButton::pressed, this, [this](){this->subscribe(true);});
  connect(ui_.subscribePushButton, &QPushButton::pressed, this, [this](){this->subscribe();});

  // set node name if passed in as argument
  const QStringList& argv = context.argv();
  if (!argv.empty()) {
      arg_node_ = argv[0];
      selectNode(arg_node_);
  }
}

void UDPBridgePlugin::shutdownPlugin()
{
  delete bridge_node_;
  bridge_node_ = nullptr;
}

void UDPBridgePlugin::saveSettings(qt_gui_cpp::Settings& plugin_settings, qt_gui_cpp::Settings& instance_settings) const
{
  QString node = ui_.nodesComboBox->currentText();
  instance_settings.setValue("node", node);
}

void UDPBridgePlugin::restoreSettings(const qt_gui_cpp::Settings& plugin_settings, const qt_gui_cpp::Settings& instance_settings)
{
  QString node = instance_settings.value("node", "").toString();
  // don't overwrite topic name passed as command line argument
  if (!arg_node_.isEmpty())
  {
    arg_node_ = "";
  }
  else
  {
    selectNode(node);
  }
}

void UDPBridgePlugin::updateNodeList()
{
  std::vector<std::string> potential_nodes;

  auto services = node_->get_service_names_and_types();

  for (const auto& service: services)
  {
    for (const auto& type: service.second)
    {
      if (type == "udp_bridge_interfaces/srv/ListRemotes")
      {
        potential_nodes.push_back(service.first.substr(0, service.first.find("/list_remotes")));
      }
    }
  }

  QList<QString> nodes;

  for(auto node: potential_nodes)
  {
    auto service_client = node_->create_client<udp_bridge_interfaces::srv::ListRemotes>(node+"/list_remotes");
    if(service_client->service_is_ready())
      nodes.append(node.c_str());
  }

  nodes.append("");
  std::sort(nodes.begin(), nodes.end());

  QString selected = ui_.nodesComboBox->currentText();

  ui_.nodesComboBox->clear();
  for (QList<QString>::const_iterator it = nodes.begin(); it != nodes.end(); it++)
  {
    QString label(*it);
    label.replace(" ", "/");
    ui_.nodesComboBox->addItem(label, QVariant(*it));
  }

  // restore previous selection
  selectNode(selected);
}

void UDPBridgePlugin::selectNode(const QString& node)
{
  int index = ui_.nodesComboBox->findText(node);
  if (index == -1)
  {
    // add topic name to list if not yet in
    QString label(node);
    label.replace(" ", "/");
    ui_.nodesComboBox->addItem(label, QVariant(node));
    index = ui_.nodesComboBox->findText(node);
  }
  ui_.nodesComboBox->setCurrentIndex(index);
}

void UDPBridgePlugin::onNodeChanged(int index)
{
  active_remote_.clear();
  active_connection_.clear();
  active_local_topic_.clear();
  active_remote_topic_.clear();
  ui_.remoteTopicsTreeView->setModel(nullptr);
  ui_.remoteRemotesTreeView->setModel(nullptr);

  QString node = ui_.nodesComboBox->itemData(index).toString();
  node_namespace_ = node.toStdString();
  bridge_node_->setTopicsPrefix(node_, node_namespace_, true);
}

void UDPBridgePlugin::addRemote()
{
  Ui::AddRemoteDialog addRemoteDialogUI;
  QDialog addRemoteDialog;
  addRemoteDialogUI.setupUi(&addRemoteDialog);
  if(addRemoteDialog.exec())
  {
    auto add_remote = std::make_shared<udp_bridge_interfaces::srv::AddRemote::Request>();
    add_remote->name = addRemoteDialogUI.nameLineEdit->text().toStdString();
    add_remote->connection_id = addRemoteDialogUI.connectionLineEdit->text().toStdString();
    add_remote->address = addRemoteDialogUI.addressLineEdit->text().toStdString();
    add_remote->port = addRemoteDialogUI.portLineEdit->text().toInt();
    add_remote->return_address = addRemoteDialogUI.returnAddressLineEdit->text().toStdString();
    bool ok;
    uint16_t return_port = addRemoteDialogUI.returnPortLineEdit->text().toUInt(&ok);
    if(ok)
      add_remote->return_port = return_port;
    uint32_t max_rate = addRemoteDialogUI.rateLimitLineEdit->text().toUInt(&ok);
    if(ok)
      add_remote->maximum_bytes_per_second = max_rate;
    max_rate = addRemoteDialogUI.returnRateLimitLineEdit->text().toUInt(&ok);
    if(ok)
      add_remote->return_maximum_bytes_per_second = max_rate;
    
    if(!bridge_node_->addRemote(add_remote))
    {
      QMessageBox::warning(widget_, "UDPBridge add remote", "The add_remote service failed.");
    }
  }
}

void UDPBridgePlugin::subscribe(bool remote_advertise)
{
  Ui::SubscribeDialog dialog_ui;
  QDialog dialog;
  dialog_ui.setupUi(&dialog);
  if(remote_advertise)
    dialog.setWindowTitle("Remote Advertise");
  else
    dialog.setWindowTitle("Remote Subscribe");

  dialog_ui.remoteComboBox->insertItems(0, bridge_node_->remotes());
  auto item = dialog_ui.remoteComboBox->findText(active_remote_.c_str());
  if(item >= 0)
    dialog_ui.remoteComboBox->setCurrentIndex(item);

  dialog_ui.connectionComboBox->insertItems(0, bridge_node_->connections(dialog_ui.remoteComboBox->currentText().toStdString()));
  auto connection_item = dialog_ui.connectionComboBox->findText(active_connection_.c_str());
  if(connection_item >= 0)
    dialog_ui.connectionComboBox->setCurrentIndex(connection_item);

  dialog_ui.remoteComboBox->connect(dialog_ui.remoteComboBox, &QComboBox::currentTextChanged, this, [&](const QString & remote)
  {
    dialog_ui.connectionComboBox->clear();
    dialog_ui.connectionComboBox->insertItems(0, this->bridge_node_->connections(remote.toStdString()));
  });

  if(remote_advertise)
  {
    dialog_ui.sourceTopicComboBox->insertItems(0, bridge_node_->topics());
    dialog_ui.sourceTopicComboBox->setCurrentIndex(dialog_ui.sourceTopicComboBox->findText(active_local_topic_.c_str()));
  }
  else
  {
    dialog_ui.sourceTopicComboBox->insertItems(0, bridge_node_->remoteTopics(active_remote_));
    dialog_ui.sourceTopicComboBox->setCurrentIndex(dialog_ui.sourceTopicComboBox->findText(active_remote_topic_.c_str()));
  }

  if(dialog.exec())
  {
    auto s = std::make_shared<udp_bridge_interfaces::srv::Subscribe::Request>();
    s->remote = dialog_ui.remoteComboBox->currentText().toStdString();
    s->connection_id = dialog_ui.connectionComboBox->currentText().toStdString();
    s->source_topic = dialog_ui.sourceTopicComboBox->currentText().toStdString();
    s->destination_topic = dialog_ui.destinationTopicLineEdit->text().toStdString();
    s->queue_size = dialog_ui.queueSizeSpinBox->value();
    s->period = dialog_ui.periodLineEdit->text().toFloat();
    if(remote_advertise)
    {
      if(!bridge_node_->remoteAdvertise(s))
      {
        QMessageBox::warning(widget_, "UDPBridge advertise", "The remote_advertise service failed.");
      }
    }
    else
    {
      if(!bridge_node_->remoteSubscribe(s))
      {
        QMessageBox::warning(widget_, "UDPBridge subscribe", "The remote_subscribe service failed.");
      }
    }

  }
}

UDPBridgePlugin::RemoteConnectionID UDPBridgePlugin::getRemoteConnection(const QModelIndex& index)
{
  auto i = index;
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

UDPBridgePlugin::TopicRemoteConnection UDPBridgePlugin::getTopicRemoteConnection(const QModelIndex& index)
{
  auto i = index;
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
        return std::make_pair(i.model()->data(i.parent()).toString().toStdString(),std::make_pair(i.model()->data(i).toString().toStdString(), std::string()));
    else // i is topic
      return std::make_pair(i.model()->data(i).toString().toStdString(),std::make_pair(std::string(), std::string()));
  return {};
}


void UDPBridgePlugin::currentRemoteChanged(const QModelIndex& index, const QModelIndex& previous_index)
{
  auto previous = getRemoteConnection(previous_index);
  auto current = getRemoteConnection(index);
  active_remote_ = current.first;
  active_connection_ = current.second;
  ui_.remoteTopicsTreeView->setModel(bridge_node_->remoteTopicsModel(active_remote_));

  disconnect(remote_topic_changed_connection_);
  ui_.remoteRemotesTreeView->setModel(bridge_node_->remoteRemotesModel(active_remote_));
  remote_topic_changed_connection_ = connect(ui_.remoteTopicsTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &UDPBridgePlugin::currentRemoteTopicChanged);

  disconnect(remote_remote_changed_connection_);
  remote_remote_changed_connection_ = connect(ui_.remoteRemotesTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &UDPBridgePlugin::currentRemoteRemoteChanged);

  disconnect(update_remote_remote_details_connection_);
  update_remote_remote_details_connection_ = connect(bridge_node_->remoteBridgeNode(active_remote_), &BridgeNode::remoteDetailsUpdated, this, & UDPBridgePlugin::updateCurrentRemoteRemoteDetails);

  ui_.remoteTopicsGroupBox->setTitle(QString("Remote Topics (")+active_remote_.c_str()+")");
  ui_.remoteRemotesGroupBox->setTitle(QString("Remote Remotes (")+active_remote_.c_str()+")");
}

void UDPBridgePlugin::currentRemoteRemoteChanged(const QModelIndex& index, const QModelIndex& previous_index)
{
  auto previous = getRemoteConnection(previous_index);
  auto current = getRemoteConnection(index);
  active_remote_remote_ = current.first;
  active_remote_connection_ = current.second;
}


void UDPBridgePlugin::currentLocalTopicChanged(const QModelIndex& index, const QModelIndex& previous_index)
{
  auto current = getTopicRemoteConnection(index);
  active_local_topic_ = current.first;
}

void UDPBridgePlugin::currentRemoteTopicChanged(const QModelIndex& index, const QModelIndex& previous_index)
{
  auto current = getTopicRemoteConnection(index);
  active_remote_topic_ = current.first;
}


void UDPBridgePlugin::updateCurrentRemoteDetails(QString remote, QString connection, QString details)
{
  if(remote.toStdString() == active_remote_ && connection.toStdString() == active_connection_)
    ui_.selectedRemotedetailsLabel->setText(details);
}

void UDPBridgePlugin::updateCurrentRemoteRemoteDetails(QString remote, QString connection, QString details)
{
  if(remote.toStdString() == active_remote_remote_ && connection.toStdString() == active_remote_connection_)
    ui_.selectedRemoteRemoteDetailsLabel->setText(QString(active_remote_.c_str())+"'s "+ details);
}

} // namespace rqt_udp_bridge

PLUGINLIB_EXPORT_CLASS(rqt_udp_bridge::UDPBridgePlugin, rqt_gui_cpp::Plugin)
