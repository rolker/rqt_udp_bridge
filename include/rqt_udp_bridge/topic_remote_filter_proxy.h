#ifndef RQT_UDP_BRIDGE_TOPIC_REMOTE_FILTER_PROXY_H
#define RQT_UDP_BRIDGE_TOPIC_REMOTE_FILTER_PROXY_H

#include <QList>
#include <QSortFilterProxyModel>

namespace rqt_udp_bridge
{

/// Filtering proxy for the udp_bridge tree models. Supports three independent,
/// combinable predicates evaluated on leaf rows (the rows that carry stats):
///
///  - substring match on the name column (column 0), matching the row itself or
///    any of its ancestors so typing a remote name reveals its connections;
///  - "hide idle": hide leaves whose data cells are all stale (greyed) or absent;
///  - "show failing": keep only leaves with a nonzero failed/dropped data cell.
///
/// Non-leaf rows are never accepted on their own merits when any predicate is
/// active; they appear only via Qt's recursive filtering when a descendant leaf
/// passes (so an all-idle remote disappears rather than showing empty). The
/// proxy is created once per tree and kept stable across source-model swaps —
/// callers use setSourceModel() on remote change, preserving filter state.
///
/// Staleness is read from the same TimestampRole / ForegroundRole that
/// BridgeNode stamps; the "zero" sentinel for failing detection matches
/// humanReadableDataRate(0) == "0 Bps". Both couplings are intentional and
/// documented here so a change to BridgeNode's stamping is caught.
class TopicRemoteFilterProxy : public QSortFilterProxyModel
{
  Q_OBJECT
public:
  explicit TopicRemoteFilterProxy(QObject* parent = nullptr);

  /// Columns (in source-model coordinates) whose nonzero value marks a row as
  /// "failing" — the failed/dropped byte-rate columns for the wrapped model.
  void setFailureColumns(const QList<int>& columns);

public slots:
  void setFilterText(const QString& text);
  void setHideIdle(bool hide);
  void setShowFailing(bool show);

protected:
  bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

private:
  /// Must match BridgeNode::TimestampRole. A row's data cell is "fresh" when it
  /// carries a valid TimestampRole and no grey ForegroundRole (set when stale).
  static constexpr int TimestampRole = Qt::UserRole + 1;

  bool textMatches(const QModelIndex& source_index0) const;
  bool rowIsActive(int source_row, const QModelIndex& source_parent) const;
  bool rowIsFailing(int source_row, const QModelIndex& source_parent) const;

  QString text_;
  bool hide_idle_ = false;
  bool show_failing_ = false;
  QList<int> failure_columns_;
};

} // namespace rqt_udp_bridge

#endif
