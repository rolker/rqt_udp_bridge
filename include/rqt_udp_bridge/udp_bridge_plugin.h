#ifndef RQT_UDP_BRIDGE_UDP_BRIDGE_PLUGIN_H
#define RQT_UDP_BRIDGE_UDP_BRIDGE_PLUGIN_H

#include "rqt_gui_cpp/plugin.h"
#include "ui_udp_bridge_plugin.h"
#include "rclcpp/rclcpp.hpp"
#include "udp_bridge_interfaces/msg/topic_statistics_array.hpp"
#include "udp_bridge_interfaces/msg/bridge_info.hpp"
#include "rqt_udp_bridge/bridge_node.h"
#include "rqt_udp_bridge/topic_remote_filter_proxy.h"

#include <QMap>
#include <QStandardItemModel>

class QLabel;
class QLineEdit;
class QToolButton;
class QTreeView;

namespace rqt_udp_bridge
{

class UDPBridgePlugin: public rqt_gui_cpp::Plugin
{
  Q_OBJECT
public:
  UDPBridgePlugin();

  void initPlugin(qt_gui_cpp::PluginContext& context) override;
  void shutdownPlugin() override;
  void saveSettings(qt_gui_cpp::Settings& plugin_settings, qt_gui_cpp::Settings& instance_settings) const override;
  void restoreSettings(const qt_gui_cpp::Settings& plugin_settings, const qt_gui_cpp::Settings& instance_settings) override;

private slots:
  void updateNodeList();
  void selectNode(const QString& node);
  void onNodeChanged(int index);

  void addRemote();
  void subscribe(bool remote_advertise=false);

  /// Single source of truth for which remote drives the per-remote tabs. Called
  /// by the header combo and (indirectly) by remotes-tree selection.
  void setActiveRemote(const QString& remote);
  /// Rebuild the header combo's remote list from the remotes model.
  void refreshActiveRemoteCombo();

  void currentRemoteChanged(const QModelIndex& index, const QModelIndex& previous_index);
  void currentRemoteRemoteChanged(const QModelIndex& index, const QModelIndex& previous_index);

  void currentLocalTopicChanged(const QModelIndex& index, const QModelIndex& previous_index);
  void currentRemoteTopicChanged(const QModelIndex& index, const QModelIndex& previous_index);

  void updateCurrentRemoteDetails(QString remote, QString connection, rqt_udp_bridge::DetailFields fields);
  void updateCurrentRemoteRemoteDetails(QString remote, QString connection, rqt_udp_bridge::DetailFields fields);

private:
  using RemoteConnectionID = std::pair<std::string, std::string>;
  using TopicRemoteConnection = std::pair<std::string, RemoteConnectionID>;

  // Per-tab filter controls, built programmatically above each tree.
  struct FilterControls
  {
    QLineEdit* text = nullptr;
    QToolButton* hide_idle = nullptr;
    QToolButton* show_failing = nullptr;
    QToolButton* columns = nullptr;
  };

  RemoteConnectionID getRemoteConnection(const QModelIndex& index);
  TopicRemoteConnection getTopicRemoteConnection(const QModelIndex& index);

  /// Wraps a source-model index out of a (possibly proxied) view index.
  static QModelIndex mapToSourceIfProxy(const QModelIndex& index);

  /// Build a filter row (text + hide-idle + show-failing + columns) and insert
  /// it at the top of \p layout, wiring it to \p proxy / \p view.
  FilterControls buildFilterRow(QBoxLayout* layout, TopicRemoteFilterProxy* proxy, QTreeView* view);
  /// (Re)populate \p button's column-visibility menu from \p view's model.
  void buildColumnMenu(QToolButton* button, QTreeView* view);

  static void populateDetailTable(QStandardItemModel& model, const DetailFields& fields);

  // Settings helpers (column hide state is serialized as a comma-separated list).
  static QString hiddenColumnsString(const QTreeView* view);
  static void applyHiddenColumns(QTreeView* view, const QString& csv);

  Ui::UDPBridgeWidget ui_;
  QWidget* widget_ = nullptr;

  std::string node_namespace_;

  QString arg_node_;

  std::string active_remote_;
  std::string active_connection_;
  std::string active_local_topic_;
  std::string active_remote_topic_;
  std::string active_remote_remote_;
  std::string active_remote_connection_;

  BridgeNode* bridge_node_ = nullptr;

  // Stable filtering proxies — one per tree, created once, source-model swapped.
  TopicRemoteFilterProxy* local_topics_proxy_ = nullptr;
  TopicRemoteFilterProxy* remotes_proxy_ = nullptr;
  TopicRemoteFilterProxy* remote_topics_proxy_ = nullptr;
  TopicRemoteFilterProxy* remote_remotes_proxy_ = nullptr;

  FilterControls local_filter_;
  FilterControls remotes_filter_;
  FilterControls remote_topics_filter_;
  FilterControls remote_remotes_filter_;

  // Key/value detail tables, fed from the cached structured details.
  QStandardItemModel remote_details_model_;
  QStandardItemModel remote_remote_details_model_;
  // details_cache_[remote][connection] -> latest fields. The remote-remote cache
  // is scoped to the active remote and cleared when it changes.
  QMap<QString, QMap<QString, DetailFields>> details_cache_;
  QMap<QString, QMap<QString, DetailFields>> remote_remote_details_cache_;

  QMetaObject::Connection update_remote_remote_details_connection_;
  bool remote_columns_menu_built_ = false;
  bool remote_remotes_columns_menu_built_ = false;

  // Restored-from-settings state, applied once the relevant data is available.
  QString pending_active_remote_;
  // Hidden-column CSVs for the per-remote views, applied when their source model
  // is first attached (the local/remotes views are applied directly on restore).
  QString pending_remote_topics_hidden_;
  QString pending_remote_remotes_hidden_;
};

} // namespace rqt_udp_bridge

#endif
