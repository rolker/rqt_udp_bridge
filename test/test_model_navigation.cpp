// Unit tests for the tree-walk navigation and filter proxy. The highest-risk
// part of the UX refactor is that selection indices arrive in proxy coordinates
// and must be mapped to the source model before walking parent chains; these
// tests pin that down without a live ROS graph.

#include <gtest/gtest.h>

#include <QList>
#include <QStandardItemModel>

#include "rqt_udp_bridge/model_navigation.h"
#include "rqt_udp_bridge/topic_remote_filter_proxy.h"

using rqt_udp_bridge::TopicRemoteFilterProxy;
using rqt_udp_bridge::remoteConnectionAt;
using rqt_udp_bridge::topicRemoteConnectionAt;

namespace
{
// Build a remotes-style tree: remote rows with connection children. Only the
// name column (0) is populated; that is all the navigation walk reads.
QStandardItem* makeRemote(const QString& name, const QStringList& connections)
{
  auto* remote = new QStandardItem(name);
  for(const auto& c: connections)
    remote->appendRow(new QStandardItem(c));
  return remote;
}

// A remotes row with 12 columns (name + 11 data), so failure-column filtering
// can be exercised. value is written into every data column.
QList<QStandardItem*> makeConnectionRow(const QString& name, const QString& failValue)
{
  QList<QStandardItem*> row;
  row << new QStandardItem(name);
  for(int col = 1; col < 12; col++)
    row << new QStandardItem(col == 4 ? failValue : QStringLiteral("0 Bps"));
  return row;
}
}  // namespace

// --- Index mapping: source vs proxy coordinates -----------------------------

TEST(ModelNavigation, RemoteConnectionFromSourceIndex)
{
  QStandardItemModel model;
  model.appendRow(makeRemote("remoteA", {"conn1", "conn2"}));

  QModelIndex remote = model.index(0, 0);
  QModelIndex conn2 = model.index(1, 0, remote);

  auto rc = remoteConnectionAt(conn2);
  EXPECT_EQ(rc.first, "remoteA");
  EXPECT_EQ(rc.second, "conn2");
}

TEST(ModelNavigation, RemoteTopLevelHasEmptyConnection)
{
  QStandardItemModel model;
  model.appendRow(makeRemote("remoteA", {"conn1"}));

  TopicRemoteFilterProxy proxy;
  proxy.setSourceModel(&model);

  auto rc = remoteConnectionAt(proxy.index(0, 0));
  EXPECT_EQ(rc.first, "remoteA");
  EXPECT_EQ(rc.second, "");
}

TEST(ModelNavigation, RemoteConnectionFromProxyIndex)
{
  QStandardItemModel model;
  model.appendRow(makeRemote("remoteA", {"conn1", "conn2"}));

  TopicRemoteFilterProxy proxy;
  proxy.setSourceModel(&model);

  QModelIndex remote = proxy.index(0, 0);
  QModelIndex conn2 = proxy.index(1, 0, remote);

  auto rc = remoteConnectionAt(conn2);
  EXPECT_EQ(rc.first, "remoteA");
  EXPECT_EQ(rc.second, "conn2");
}

// The regression that motivated extracting this: once a filter drops rows, the
// proxy row index no longer equals the source row index. Without mapToSource
// the walk would read the wrong connection.
TEST(ModelNavigation, ProxyIndexMappingSurvivesRowShift)
{
  QStandardItemModel model;
  model.appendRow(makeRemote("remoteA", {"conn1", "conn2"}));
  model.appendRow(makeRemote("remoteB", {"conn3"}));

  TopicRemoteFilterProxy proxy;
  proxy.setSourceModel(&model);
  proxy.setFilterText("conn2");  // hides conn1 (source row 0) and remoteB

  ASSERT_EQ(proxy.rowCount(), 1);
  QModelIndex remote = proxy.index(0, 0);
  EXPECT_EQ(remote.data().toString().toStdString(), "remoteA");
  ASSERT_EQ(proxy.rowCount(remote), 1);

  QModelIndex visible = proxy.index(0, 0, remote);  // proxy row 0 == source row 1
  EXPECT_EQ(visible.data().toString().toStdString(), "conn2");

  auto rc = remoteConnectionAt(visible);
  EXPECT_EQ(rc.first, "remoteA");
  EXPECT_EQ(rc.second, "conn2");
}

TEST(ModelNavigation, TopicRemoteConnectionFromProxyIndex)
{
  QStandardItemModel model;
  auto* topic = new QStandardItem("t1");
  auto* remote = new QStandardItem("rA");
  remote->appendRow(new QStandardItem("c1"));
  topic->appendRow(remote);
  model.appendRow(topic);

  TopicRemoteFilterProxy proxy;
  proxy.setSourceModel(&model);

  QModelIndex t = proxy.index(0, 0);
  QModelIndex r = proxy.index(0, 0, t);
  QModelIndex c = proxy.index(0, 0, r);

  auto leaf = topicRemoteConnectionAt(c);
  EXPECT_EQ(leaf.first, "t1");
  EXPECT_EQ(leaf.second.first, "rA");
  EXPECT_EQ(leaf.second.second, "c1");

  auto mid = topicRemoteConnectionAt(r);
  EXPECT_EQ(mid.first, "t1");
  EXPECT_EQ(mid.second.first, "rA");
  EXPECT_EQ(mid.second.second, "");

  auto top = topicRemoteConnectionAt(t);
  EXPECT_EQ(top.first, "t1");
  EXPECT_EQ(top.second.first, "");
  EXPECT_EQ(top.second.second, "");
}

// --- Filter predicates ------------------------------------------------------

TEST(FilterProxy, TextFilterKeepsMatchingBranchOnly)
{
  QStandardItemModel model;
  model.appendRow(makeRemote("remoteA", {"conn1", "conn2"}));
  model.appendRow(makeRemote("remoteB", {"conn3"}));

  TopicRemoteFilterProxy proxy;
  proxy.setSourceModel(&model);
  proxy.setFilterText("conn1");

  ASSERT_EQ(proxy.rowCount(), 1);  // remoteB dropped
  QModelIndex remote = proxy.index(0, 0);
  EXPECT_EQ(remote.data().toString().toStdString(), "remoteA");
  ASSERT_EQ(proxy.rowCount(remote), 1);  // only conn1
  EXPECT_EQ(proxy.index(0, 0, remote).data().toString().toStdString(), "conn1");
}

TEST(FilterProxy, ShowFailingKeepsOnlyNonzeroRows)
{
  QStandardItemModel model;
  auto* remote = new QStandardItem("remoteA");
  remote->appendRow(makeConnectionRow("connOk", "0 Bps"));
  remote->appendRow(makeConnectionRow("connFail", "5 Bps"));
  model.appendRow(remote);

  TopicRemoteFilterProxy proxy;
  proxy.setFailureColumns({4, 5, 7, 8, 10, 11});
  proxy.setSourceModel(&model);
  proxy.setShowFailing(true);

  ASSERT_EQ(proxy.rowCount(), 1);  // remoteA kept via failing child
  QModelIndex pa = proxy.index(0, 0);
  ASSERT_EQ(proxy.rowCount(pa), 1);
  EXPECT_EQ(proxy.index(0, 0, pa).data().toString().toStdString(), "connFail");
}

TEST(FilterProxy, NoPredicatesShowsEverything)
{
  QStandardItemModel model;
  model.appendRow(makeRemote("remoteA", {"conn1", "conn2"}));
  model.appendRow(makeRemote("remoteB", {"conn3"}));

  TopicRemoteFilterProxy proxy;
  proxy.setSourceModel(&model);

  EXPECT_EQ(proxy.rowCount(), 2);
  EXPECT_EQ(proxy.rowCount(proxy.index(0, 0)), 2);
  EXPECT_EQ(proxy.rowCount(proxy.index(1, 0)), 1);
}
