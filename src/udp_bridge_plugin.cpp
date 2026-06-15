#include "rqt_udp_bridge/udp_bridge_plugin.h"
#include "rqt_udp_bridge/model_navigation.h"
#include "ui_add_remote_dialog.h"
#include "ui_subscribe_dialog.h"

#include "udp_bridge_interfaces/srv/list_remotes.hpp"
#include "udp_bridge_interfaces/srv/add_remote.hpp"
#include "udp_bridge_interfaces/srv/subscribe.hpp"

#include <pluginlib/class_list_macros.hpp>

#include <QAbstractProxyModel>
#include <QAction>
#include <QBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeView>

namespace rqt_udp_bridge
{

namespace
{
// Tab indices, matching the order of tabs declared in udp_bridge_plugin.ui.
constexpr int kLocalTopicsTab = 0;
constexpr int kRemoteTopicsTab = 2;
constexpr int kRemoteRemotesTab = 3;

// QStackedWidget page indices for the per-remote tabs.
constexpr int kEmptyPage = 0;
constexpr int kContentPage = 1;

// Failed + dropped byte-rate columns (source-model coordinates) used by the
// "show failing" filter. The topics model has failed/dropped bytes at 4/5; the
// remotes model has message/overhead/resend fail+drop at 4,5,7,8,10,11.
const QList<int> kTopicsFailureColumns = {4, 5};
const QList<int> kRemotesFailureColumns = {4, 5, 7, 8, 10, 11};
}  // namespace

UDPBridgePlugin::UDPBridgePlugin():rqt_gui_cpp::Plugin()
{
  setObjectName("UDPBridge");
}

void UDPBridgePlugin::initPlugin(qt_gui_cpp::PluginContext& context)
{
  widget_ = new QWidget();
  ui_.setupUi(widget_);

  if(!bridge_node_)
    bridge_node_ = new BridgeNode(this);

  // --- Stable filtering proxies (created once, source models swapped later) ---
  local_topics_proxy_ = new TopicRemoteFilterProxy(this);
  local_topics_proxy_->setFailureColumns(kTopicsFailureColumns);
  local_topics_proxy_->setSourceModel(bridge_node_->topicsModel());
  ui_.localTopicsTreeView->setModel(local_topics_proxy_);

  remotes_proxy_ = new TopicRemoteFilterProxy(this);
  remotes_proxy_->setFailureColumns(kRemotesFailureColumns);
  remotes_proxy_->setSourceModel(bridge_node_->remotesModel());
  ui_.remotesTreeView->setModel(remotes_proxy_);

  remote_topics_proxy_ = new TopicRemoteFilterProxy(this);
  remote_topics_proxy_->setFailureColumns(kTopicsFailureColumns);
  ui_.remoteTopicsTreeView->setModel(remote_topics_proxy_);

  remote_remotes_proxy_ = new TopicRemoteFilterProxy(this);
  remote_remotes_proxy_->setFailureColumns(kRemotesFailureColumns);
  ui_.remoteRemotesTreeView->setModel(remote_remotes_proxy_);

  // --- Detail key/value tables ---
  ui_.selectedRemoteDetailsTable->setModel(&remote_details_model_);
  ui_.selectedRemoteRemoteDetailsTable->setModel(&remote_remote_details_model_);

  // --- Filter rows (built above each tree) ---
  local_filter_ = buildFilterRow(qobject_cast<QBoxLayout*>(ui_.localTopicsLayout), local_topics_proxy_, ui_.localTopicsTreeView);
  remotes_filter_ = buildFilterRow(qobject_cast<QBoxLayout*>(ui_.remotesLayout), remotes_proxy_, ui_.remotesTreeView);
  remote_topics_filter_ = buildFilterRow(qobject_cast<QBoxLayout*>(ui_.remoteTopicsLayout), remote_topics_proxy_, ui_.remoteTopicsTreeView);
  remote_remotes_filter_ = buildFilterRow(qobject_cast<QBoxLayout*>(ui_.remoteRemotesLayout), remote_remotes_proxy_, ui_.remoteRemotesTreeView);

  // Column menus for the views whose models are already attached.
  buildColumnMenu(local_filter_.columns, ui_.localTopicsTreeView);
  buildColumnMenu(remotes_filter_.columns, ui_.remotesTreeView);

  // Per-remote tabs start empty until a remote is selected.
  ui_.remoteTopicsStack->setCurrentIndex(kEmptyPage);
  ui_.remoteRemotesStack->setCurrentIndex(kEmptyPage);

  widget_->setWindowTitle(widget_->windowTitle() + " (" + QString::number(context.serialNumber()) + ")");

  context.addWidget(widget_);

  updateNodeList();
  ui_.nodesComboBox->setCurrentIndex(ui_.nodesComboBox->findText(""));
  connect(ui_.nodesComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &UDPBridgePlugin::onNodeChanged);

  ui_.refreshNodesPushButton->setIcon(QIcon::fromTheme("view-refresh"));
  connect(ui_.refreshNodesPushButton, &QPushButton::pressed, this, &UDPBridgePlugin::updateNodeList);

  // The header combo is the single source of truth for the active remote.
  connect(ui_.activeRemoteComboBox, &QComboBox::currentTextChanged, this, &UDPBridgePlugin::setActiveRemote);

  // Wire the Remote Subscribe / Remote Advertise buttons to the dialog. These
  // exist in udp_bridge_plugin.ui but lost their connections in the tab-layout
  // rework (4170587); without this, clicking them does nothing. Use lambdas so
  // the QPushButton::clicked(bool checked) signal does not leak into subscribe()'s
  // remote_advertise argument.
  connect(ui_.subscribePushButton, &QPushButton::clicked, this, [this]() { subscribe(false); });
  connect(ui_.advertisePushButton, &QPushButton::clicked, this, [this]() { subscribe(true); });

  // Selection models are stable (the proxy is never replaced), so connect once.
  connect(ui_.remotesTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &UDPBridgePlugin::currentRemoteChanged);
  connect(ui_.localTopicsTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &UDPBridgePlugin::currentLocalTopicChanged);
  connect(ui_.remoteTopicsTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &UDPBridgePlugin::currentRemoteTopicChanged);
  connect(ui_.remoteRemotesTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &UDPBridgePlugin::currentRemoteRemoteChanged);

  // Keep the header combo's remote list in sync with the remotes model.
  auto* remotes_model = bridge_node_->remotesModel();
  connect(remotes_model, &QAbstractItemModel::rowsInserted, this, &UDPBridgePlugin::refreshActiveRemoteCombo);
  connect(remotes_model, &QAbstractItemModel::rowsRemoved, this, &UDPBridgePlugin::refreshActiveRemoteCombo);
  connect(remotes_model, &QAbstractItemModel::modelReset, this, &UDPBridgePlugin::refreshActiveRemoteCombo);

  connect(bridge_node_, &BridgeNode::remoteDetailsUpdated, this, &UDPBridgePlugin::updateCurrentRemoteDetails);

  refreshActiveRemoteCombo();

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

UDPBridgePlugin::FilterControls UDPBridgePlugin::buildFilterRow(QBoxLayout* layout, TopicRemoteFilterProxy* proxy, QTreeView* view)
{
  FilterControls controls;
  auto* row = new QWidget(widget_);
  auto* row_layout = new QHBoxLayout(row);
  row_layout->setContentsMargins(0, 0, 0, 0);

  controls.text = new QLineEdit(row);
  controls.text->setPlaceholderText("Filter…");
  controls.text->setClearButtonEnabled(true);
  row_layout->addWidget(controls.text, 1);

  controls.hide_idle = new QToolButton(row);
  controls.hide_idle->setText("Hide idle");
  controls.hide_idle->setCheckable(true);
  controls.hide_idle->setToolTip("Hide rows whose statistics have stopped updating");
  row_layout->addWidget(controls.hide_idle);

  controls.show_failing = new QToolButton(row);
  controls.show_failing->setText("Show failing");
  controls.show_failing->setCheckable(true);
  controls.show_failing->setToolTip("Show only rows with nonzero failed or dropped bytes");
  row_layout->addWidget(controls.show_failing);

  controls.columns = new QToolButton(row);
  controls.columns->setText("Columns");
  controls.columns->setPopupMode(QToolButton::InstantPopup);
  controls.columns->setToolTip("Show or hide columns");
  row_layout->addWidget(controls.columns);

  if(layout)
    layout->insertWidget(0, row);

  connect(controls.text, &QLineEdit::textChanged, proxy, &TopicRemoteFilterProxy::setFilterText);
  connect(controls.hide_idle, &QToolButton::toggled, proxy, &TopicRemoteFilterProxy::setHideIdle);
  connect(controls.show_failing, &QToolButton::toggled, proxy, &TopicRemoteFilterProxy::setShowFailing);
  // Refresh the column menu's checkmarks each time it is about to open, so a
  // column hidden via persistence/another path is reflected here too.
  connect(controls.columns, &QToolButton::pressed, this, [this, button = controls.columns, view]() { buildColumnMenu(button, view); });

  return controls;
}

void UDPBridgePlugin::buildColumnMenu(QToolButton* button, QTreeView* view)
{
  if(!button || !view || !view->model())
    return;
  auto* menu = button->menu();
  if(!menu)
  {
    menu = new QMenu(button);
    button->setMenu(menu);
  }
  menu->clear();
  const int columns = view->model()->columnCount();
  for(int col = 0; col < columns; col++)
  {
    QString label = view->model()->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
    if(label.isEmpty())
      label = QString("Column %1").arg(col);
    auto* action = menu->addAction(label);
    action->setCheckable(true);
    action->setChecked(!view->isColumnHidden(col));
    // Column 0 (the name) is always kept to avoid an unusable tree.
    if(col == 0)
      action->setEnabled(false);
    connect(action, &QAction::toggled, view, [view, col](bool checked) { view->setColumnHidden(col, !checked); });
  }
}

void UDPBridgePlugin::saveSettings(qt_gui_cpp::Settings& plugin_settings, qt_gui_cpp::Settings& instance_settings) const
{
  (void)plugin_settings;
  instance_settings.setValue("node", ui_.nodesComboBox->currentText());
  instance_settings.setValue("active_tab", ui_.tabWidget->currentIndex());
  instance_settings.setValue("active_remote", ui_.activeRemoteComboBox->currentText());

  auto save_filter = [&](const QString& prefix, const FilterControls& f)
  {
    if(f.text)
      instance_settings.setValue(prefix + "_filter_text", f.text->text());
    if(f.hide_idle)
      instance_settings.setValue(prefix + "_hide_idle", f.hide_idle->isChecked());
    if(f.show_failing)
      instance_settings.setValue(prefix + "_show_failing", f.show_failing->isChecked());
  };
  save_filter("local", local_filter_);
  save_filter("remotes", remotes_filter_);
  save_filter("remote_topics", remote_topics_filter_);
  save_filter("remote_remotes", remote_remotes_filter_);

  instance_settings.setValue("local_hidden_cols", hiddenColumnsString(ui_.localTopicsTreeView));
  instance_settings.setValue("remotes_hidden_cols", hiddenColumnsString(ui_.remotesTreeView));
  instance_settings.setValue("remote_topics_hidden_cols", hiddenColumnsString(ui_.remoteTopicsTreeView));
  instance_settings.setValue("remote_remotes_hidden_cols", hiddenColumnsString(ui_.remoteRemotesTreeView));
}

void UDPBridgePlugin::restoreSettings(const qt_gui_cpp::Settings& plugin_settings, const qt_gui_cpp::Settings& instance_settings)
{
  (void)plugin_settings;

  auto restore_filter = [&](const QString& prefix, const FilterControls& f)
  {
    if(f.text)
      f.text->setText(instance_settings.value(prefix + "_filter_text", "").toString());
    if(f.hide_idle)
      f.hide_idle->setChecked(instance_settings.value(prefix + "_hide_idle", false).toBool());
    if(f.show_failing)
      f.show_failing->setChecked(instance_settings.value(prefix + "_show_failing", false).toBool());
  };
  restore_filter("local", local_filter_);
  restore_filter("remotes", remotes_filter_);
  restore_filter("remote_topics", remote_topics_filter_);
  restore_filter("remote_remotes", remote_remotes_filter_);

  // Local/remotes views have their models already; apply column state now.
  applyHiddenColumns(ui_.localTopicsTreeView, instance_settings.value("local_hidden_cols", "").toString());
  applyHiddenColumns(ui_.remotesTreeView, instance_settings.value("remotes_hidden_cols", "").toString());
  // Per-remote views attach their model on first selection; defer.
  pending_remote_topics_hidden_ = instance_settings.value("remote_topics_hidden_cols", "").toString();
  pending_remote_remotes_hidden_ = instance_settings.value("remote_remotes_hidden_cols", "").toString();

  int active_tab = instance_settings.value("active_tab", kLocalTopicsTab).toInt();
  if(active_tab >= 0 && active_tab < ui_.tabWidget->count())
    ui_.tabWidget->setCurrentIndex(active_tab);

  // The active remote may not exist yet; remember it and select once it appears.
  pending_active_remote_ = instance_settings.value("active_remote", "").toString();

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

QString UDPBridgePlugin::hiddenColumnsString(const QTreeView* view)
{
  if(!view || !view->model())
    return {};
  QStringList hidden;
  for(int col = 0; col < view->model()->columnCount(); col++)
    if(view->isColumnHidden(col))
      hidden << QString::number(col);
  return hidden.join(",");
}

void UDPBridgePlugin::applyHiddenColumns(QTreeView* view, const QString& csv)
{
  if(!view || !view->model())
    return;
  const int columns = view->model()->columnCount();
  for(int col = 0; col < columns; col++)
    view->setColumnHidden(col, false);
  if(csv.isEmpty())
    return;
  for(const QString& token: csv.split(",", Qt::SkipEmptyParts))
  {
    bool ok = false;
    int col = token.toInt(&ok);
    if(ok && col > 0 && col < columns)  // never hide the name column
      view->setColumnHidden(col, true);
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
  // Reset per-remote state before the bridge node clears its child models
  // (which back remote_topics_proxy_ / remote_remotes_proxy_).
  setActiveRemote("");
  active_local_topic_.clear();
  active_remote_topic_.clear();
  active_connection_.clear();
  details_cache_.clear();

  QString node = ui_.nodesComboBox->itemData(index).toString();
  node_namespace_ = node.toStdString();
  bridge_node_->setTopicsPrefix(node_, node_namespace_, true);
}

void UDPBridgePlugin::refreshActiveRemoteCombo()
{
  auto* combo = ui_.activeRemoteComboBox;
  const QString current = QString::fromStdString(active_remote_);

  QStringList remotes;
  auto* model = bridge_node_->remotesModel();
  for(int row = 0; row < model->rowCount(); row++)
    if(auto* item = model->item(row))
      remotes << item->data(Qt::DisplayRole).toString();
  remotes.sort();

  QSignalBlocker blocker(combo);
  combo->clear();
  combo->addItem("");  // empty selection => no active remote
  combo->addItems(remotes);

  // Prefer a pending restored selection if it has now appeared.
  QString desired = current;
  if(desired.isEmpty() && !pending_active_remote_.isEmpty() && remotes.contains(pending_active_remote_))
  {
    desired = pending_active_remote_;
    pending_active_remote_.clear();
  }
  int idx = combo->findText(desired);
  combo->setCurrentIndex(idx >= 0 ? idx : 0);
  blocker.unblock();

  // If the selection effectively changed (the active remote vanished, or a
  // pending one appeared), reconcile the dependent state.
  if(combo->currentText() != current)
    setActiveRemote(combo->currentText());
}

void UDPBridgePlugin::setActiveRemote(const QString& remote)
{
  const std::string remote_str = remote.toStdString();

  active_remote_ = remote_str;
  active_remote_remote_.clear();
  active_remote_connection_.clear();
  remote_remote_details_cache_.clear();
  remote_remote_details_model_.clear();

  // Keep the header combo in sync when driven from elsewhere (tree selection).
  if(ui_.activeRemoteComboBox->currentText() != remote)
  {
    QSignalBlocker blocker(ui_.activeRemoteComboBox);
    int idx = ui_.activeRemoteComboBox->findText(remote);
    if(idx >= 0)
      ui_.activeRemoteComboBox->setCurrentIndex(idx);
  }

  disconnect(update_remote_remote_details_connection_);

  if(remote_str.empty())
  {
    remote_topics_proxy_->setSourceModel(nullptr);
    remote_remotes_proxy_->setSourceModel(nullptr);
    ui_.remoteTopicsStack->setCurrentIndex(kEmptyPage);
    ui_.remoteRemotesStack->setCurrentIndex(kEmptyPage);
    ui_.tabWidget->setTabText(kRemoteTopicsTab, "Topics @ —");
    ui_.tabWidget->setTabText(kRemoteRemotesTab, "Peers @ —");
    ui_.headlineStatsLabel->clear();
    return;
  }

  remote_topics_proxy_->setSourceModel(bridge_node_->remoteTopicsModel(remote_str));
  remote_remotes_proxy_->setSourceModel(bridge_node_->remoteRemotesModel(remote_str));
  ui_.remoteTopicsStack->setCurrentIndex(kContentPage);
  ui_.remoteRemotesStack->setCurrentIndex(kContentPage);
  ui_.tabWidget->setTabText(kRemoteTopicsTab, QString("Topics @ %1").arg(remote));
  ui_.tabWidget->setTabText(kRemoteRemotesTab, QString("Peers @ %1").arg(remote));

  // Build the per-remote column menus and apply restored column state once.
  if(!remote_columns_menu_built_)
  {
    buildColumnMenu(remote_topics_filter_.columns, ui_.remoteTopicsTreeView);
    applyHiddenColumns(ui_.remoteTopicsTreeView, pending_remote_topics_hidden_);
    remote_columns_menu_built_ = true;
  }
  if(!remote_remotes_columns_menu_built_)
  {
    buildColumnMenu(remote_remotes_filter_.columns, ui_.remoteRemotesTreeView);
    applyHiddenColumns(ui_.remoteRemotesTreeView, pending_remote_remotes_hidden_);
    remote_remotes_columns_menu_built_ = true;
  }

  if(auto* remote_node = bridge_node_->remoteBridgeNode(remote_str))
    update_remote_remote_details_connection_ = connect(remote_node, &BridgeNode::remoteDetailsUpdated, this, &UDPBridgePlugin::updateCurrentRemoteRemoteDetails);

  const int connection_count = bridge_node_->connections(remote_str).size();
  ui_.headlineStatsLabel->setText(QString("%1 connection%2").arg(connection_count).arg(connection_count == 1 ? "" : "s"));
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

QModelIndex UDPBridgePlugin::mapToSourceIfProxy(const QModelIndex& index)
{
  return mapToSource(index);
}

UDPBridgePlugin::RemoteConnectionID UDPBridgePlugin::getRemoteConnection(const QModelIndex& index)
{
  return remoteConnectionAt(index);
}

UDPBridgePlugin::TopicRemoteConnection UDPBridgePlugin::getTopicRemoteConnection(const QModelIndex& index)
{
  return topicRemoteConnectionAt(index);
}

void UDPBridgePlugin::currentRemoteChanged(const QModelIndex& index, const QModelIndex& previous_index)
{
  (void)previous_index;
  auto current = getRemoteConnection(index);
  active_connection_ = current.second;

  // The header combo is the source of truth; selecting a remote in the tree
  // drives it (which in turn runs setActiveRemote for the per-remote tabs).
  const QString remote = QString::fromStdString(current.first);
  if(ui_.activeRemoteComboBox->currentText() != remote)
  {
    int idx = ui_.activeRemoteComboBox->findText(remote);
    if(idx >= 0)
      ui_.activeRemoteComboBox->setCurrentIndex(idx);  // emits -> setActiveRemote
    else
      setActiveRemote(remote);
  }

  // Show the selected connection's details immediately from cache, if known.
  auto remote_it = details_cache_.find(remote);
  if(remote_it != details_cache_.end())
  {
    auto conn_it = remote_it->find(QString::fromStdString(active_connection_));
    if(conn_it != remote_it->end())
      populateDetailTable(remote_details_model_, *conn_it);
  }
}

void UDPBridgePlugin::currentRemoteRemoteChanged(const QModelIndex& index, const QModelIndex& previous_index)
{
  (void)previous_index;
  auto current = getRemoteConnection(index);
  active_remote_remote_ = current.first;
  active_remote_connection_ = current.second;

  auto remote_it = remote_remote_details_cache_.find(QString::fromStdString(active_remote_remote_));
  if(remote_it != remote_remote_details_cache_.end())
  {
    auto conn_it = remote_it->find(QString::fromStdString(active_remote_connection_));
    if(conn_it != remote_it->end())
      populateDetailTable(remote_remote_details_model_, *conn_it);
  }
}

void UDPBridgePlugin::currentLocalTopicChanged(const QModelIndex& index, const QModelIndex& previous_index)
{
  (void)previous_index;
  auto current = getTopicRemoteConnection(index);
  active_local_topic_ = current.first;
}

void UDPBridgePlugin::currentRemoteTopicChanged(const QModelIndex& index, const QModelIndex& previous_index)
{
  (void)previous_index;
  auto current = getTopicRemoteConnection(index);
  active_remote_topic_ = current.first;
}

void UDPBridgePlugin::populateDetailTable(QStandardItemModel& model, const DetailFields& fields)
{
  model.clear();
  model.setHorizontalHeaderLabels({"field", "value"});
  for(const auto& field: fields)
  {
    auto* key = new QStandardItem(field.first);
    key->setEditable(false);
    auto* value = new QStandardItem(field.second);
    value->setEditable(false);
    model.appendRow(QList<QStandardItem*>{key, value});
  }
}

void UDPBridgePlugin::updateCurrentRemoteDetails(QString remote, QString connection, DetailFields fields)
{
  details_cache_[remote][connection] = fields;
  if(remote.toStdString() == active_remote_ && connection.toStdString() == active_connection_)
    populateDetailTable(remote_details_model_, fields);
}

void UDPBridgePlugin::updateCurrentRemoteRemoteDetails(QString remote, QString connection, DetailFields fields)
{
  remote_remote_details_cache_[remote][connection] = fields;
  if(remote.toStdString() == active_remote_remote_ && connection.toStdString() == active_remote_connection_)
    populateDetailTable(remote_remote_details_model_, fields);
}

} // namespace rqt_udp_bridge

PLUGINLIB_EXPORT_CLASS(rqt_udp_bridge::UDPBridgePlugin, rqt_gui_cpp::Plugin)
