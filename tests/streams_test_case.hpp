#ifndef STREAMS_TEST_CASE
#define STREAMS_TEST_CASE

#include <streams.h>

std::function<void(e_t e)> sink(std::vector<Event> & v) {
  return [&](e_t e) { v.push_back(e); };
}

TEST(call_rescue_streams_test_case, test)
{
  std::vector<Event> v1, v2, v3;

  Event e;
  call_rescue(e, {sink(v1)});
  ASSERT_EQ(1, v1.size());

  v1.clear();
  call_rescue(e, {sink(v1), sink(v2), sink(v3)});
  ASSERT_EQ(1, v1.size());
  ASSERT_EQ(1, v2.size());
  ASSERT_EQ(1, v3.size());
}

TEST(with_test_case, test)
{
  std::vector<Event> v;

  with_changes_t changes = {
    {"host", "host"},
    {"service", "service"},
    {"description", "description"},
    {"state", "state"},
    {"metric", 1},
    {"ttl", 2}
  };

  Event e;
  call_rescue(e, {with(changes, {sink(v)})});

  ASSERT_EQ(1, v.size());
  EXPECT_EQ("host", v[0].host());
  EXPECT_EQ("service", v[0].service());
  EXPECT_EQ("description", v[0].description());
  EXPECT_EQ("state", v[0].state());
  EXPECT_EQ(1, v[0].metric_sint64());
  EXPECT_EQ(2, v[0].ttl());

  v.clear();

  changes = {{"metric", 1.0}};
  call_rescue(e, {with(changes, {sink(v)})});
  ASSERT_EQ(1, v.size());
  EXPECT_EQ(1.0, v[0].metric_d());
  ASSERT_FALSE(v[0].has_metric_sint64());
  ASSERT_FALSE(v[0].has_metric_f());


  v.clear();
  changes = {{"attribute", "foo"}};
  call_rescue(e, {with(changes, {sink(v)})});
  ASSERT_EQ(1, v.size());
  EXPECT_EQ(1, v[0].attributes_size());
  EXPECT_EQ("attribute", v[0].attributes(0).key());
  EXPECT_EQ("foo", v[0].attributes(0).value());
}

TEST(with_ifempty_test_case, test)
{
  std::vector<Event> v;

  with_changes_t changes = {
    {"host", "host"},
    {"service", "service"},
  };

  Event e;
  e.set_host("localhost");
  call_rescue(e, {with_ifempty(changes, {sink(v)})});

  ASSERT_EQ(1, v.size());
  EXPECT_EQ("localhost", v[0].host());
  EXPECT_EQ("service", v[0].service());

  v.clear();

  ASSERT_FALSE(e.has_metric_d());
  ASSERT_FALSE(e.has_metric_f());
  ASSERT_FALSE(e.has_metric_sint64());

  changes = {{"metric", 1.0}};
  call_rescue(e, {with_ifempty(changes, {sink(v)})});
  ASSERT_TRUE(v[0].has_metric_d());
  ASSERT_EQ(1, v.size());
  EXPECT_EQ(1.0, v[0].metric_d());
  ASSERT_FALSE(v[0].has_metric_sint64());
  ASSERT_FALSE(v[0].has_metric_f());

  v.clear();

  changes = {{"metric", 2.0}};
  e.set_metric_d(1.0);
  call_rescue(e, {with_ifempty(changes, {sink(v)})});
  ASSERT_EQ(1, v.size());
  EXPECT_EQ(1.0, v[0].metric_d());
  ASSERT_FALSE(v[0].has_metric_sint64());
  ASSERT_FALSE(v[0].has_metric_f());

  v.clear();

  changes = {{"metric", 2}};
  e.set_metric_d(1.0);
  call_rescue(e, {with_ifempty(changes, {sink(v)})});
  ASSERT_EQ(1, v.size());
  EXPECT_EQ(1.0, v[0].metric_d());
  ASSERT_FALSE(v[0].has_metric_sint64());
  ASSERT_FALSE(v[0].has_metric_f());
}

TEST(split_test_case, test)
{
  std::vector<Event> v1, v2, v3;

  split_clauses_t clauses =
                            {

                              {PRED(e.host() == "host1"), sink(v1)},

                              {PRED(metric_to_double(e) > 3.3), sink(v2)}

                            };

  Event e;
  call_rescue(e, {split(clauses)});
  ASSERT_EQ(0, v1.size());
  ASSERT_EQ(0, v2.size());

  e.set_host("host2");
  call_rescue(e, {split(clauses)});
  ASSERT_EQ(0, v1.size());
  ASSERT_EQ(0, v2.size());

  e.set_host("host1");
  call_rescue(e, {split(clauses)});
  ASSERT_EQ(1, v1.size());
  ASSERT_EQ(0, v2.size());

  v1.clear();

  e.set_host("host1");
  e.set_metric_d(3.4);
  call_rescue(e, {split(clauses)});
  ASSERT_EQ(1, v1.size());
  ASSERT_EQ(0, v2.size());

  v1.clear();

  e.set_host("host2");
  e.set_metric_d(3.4);
  call_rescue(e, {split(clauses)});
  ASSERT_EQ(0, v1.size());
  ASSERT_EQ(1, v2.size());

  v2.clear();

  e.set_host("host3");
  e.set_metric_d(1.0);
  call_rescue(e, {split(clauses, sink(v3))});
  ASSERT_EQ(0, v1.size());
  ASSERT_EQ(0, v2.size());
  ASSERT_EQ(1, v3.size());
}

#include <iostream>
TEST(by_test_case, test)
{
  std::vector<std::vector<Event>> v;
  int i = 0;
  auto by_sink = [&]()
  {
    v.resize(++i);
    return [=,&v](e_t e) { v[i-1].push_back(e);};
  };

  by_keys_t by_keys = {"host", "service"};

  auto by_stream = by(by_keys, {by_sink});

  Event e1, e2, e3;
  e1.set_host("host1"); e1.set_service("service1");
  e2.set_host("host2"); e2.set_service("service2");
  e3.set_host("host3"); e3.set_service("service3");

  call_rescue(e1, {by_stream});
  call_rescue(e2, {by_stream});
  call_rescue(e3, {by_stream});

  ASSERT_EQ(3, v.size());
  ASSERT_EQ(1, v[0].size());
  ASSERT_EQ(1, v[1].size());
  ASSERT_EQ(1, v[2].size());

  call_rescue(e1, {by_stream});
  call_rescue(e2, {by_stream});
  call_rescue(e3, {by_stream});

  ASSERT_EQ(3, v.size());
  ASSERT_EQ(2, v[0].size());
  ASSERT_EQ(2, v[1].size());
  ASSERT_EQ(2, v[2].size());
}

#endif