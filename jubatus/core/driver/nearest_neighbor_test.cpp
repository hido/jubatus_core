// Jubatus: Online machine learning framework for distributed environment
// Copyright (C) 2013 Preferred Infrastructure and Nippon Telegraph and Telephone Corporation.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License version 2.1 as published by the Free Software Foundation.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "jubatus/core/fv_converter/datum.hpp"
#include "../common/jsonconfig.hpp"
#include "../framework/stream_writer.hpp"

#include "../fv_converter/datum.hpp"
#include "../nearest_neighbor/nearest_neighbor.hpp"
#include "../nearest_neighbor/nearest_neighbor_factory.hpp"
#include "../unlearner/unlearner.hpp"
#include "nearest_neighbor.hpp"
#include "test_util.hpp"

using std::vector;
using std::pair;
using std::make_pair;
using std::string;
using jubatus::util::lang::lexical_cast;
using jubatus::util::lang::shared_ptr;
using jubatus::util::text::json::json;
using jubatus::util::text::json::json_object;
using jubatus::util::text::json::json_integer;
using jubatus::util::text::json::json_string;
using jubatus::util::text::json::json_float;
using jubatus::core::nearest_neighbor::euclid_lsh;
using jubatus::core::nearest_neighbor::lsh;
using jubatus::core::nearest_neighbor::minhash;
using jubatus::core::table::column_table;
using jubatus::core::fv_converter::datum;
using jubatus::core::nearest_neighbor::nearest_neighbor_base;
using jubatus::core::unlearner::unlearner_base;
using jubatus::core::unlearner::lru_unlearner;
using jubatus::core::unlearner::random_unlearner;

namespace jubatus {
namespace core {
namespace nearest_neighbor {

void PrintTo(const shared_ptr<nearest_neighbor_base> base, ::std::ostream* os) {
  *os << "<" << base->type() << ">";
}
}  // namespace nearest_neighbor
namespace unlearner {
void PrintTo(const shared_ptr<unlearner_base> base, ::std::ostream* os) {
  *os << "<" << base->type() << ">";
}
}  // namespace unlearner
namespace driver {

namespace {

template<typename Iterator>
fv_converter::datum create_datum(Iterator first, Iterator last) {
  fv_converter::datum d;
  for (size_t i = 0; first + i < last; ++i) {
    d.num_values_.push_back(std::make_pair(
                                lexical_cast<std::string>(i),
                                first[i]));
  }
  return d;
}

fv_converter::datum create_datum_1d(float x) {
  const float vec[] = { x };
  return create_datum(vec, vec + 1);
}

fv_converter::datum create_datum_2d(float x, float y) {
  const float vec[] = { x, y };
  return create_datum(vec, vec + 2);
}

std::vector<shared_ptr<nearest_neighbor_base> >
create_nearest_neighbor_bases() {
  const std::string id("my_id");
  std::vector<shared_ptr<nearest_neighbor_base> > nearest_neighbors;

  vector<pair<string, int> > pattern;
  for (size_t i = 8; i < 3000; i = i << 1) {  // up to 2048
    pattern.push_back(make_pair("lsh", i));
    pattern.push_back(make_pair("euclid_lsh", i));
    pattern.push_back(make_pair("minhash", i));
  }
  for (size_t i = 0; i < pattern.size(); ++i) {
    shared_ptr<core::table::column_table> table(new core::table::column_table);

    json jsconf(new json_object);
    jsconf["hash_num"] = new json_integer(pattern[i].second);
    common::jsonconfig::config conf(jsconf);
    nearest_neighbors.push_back(
        core::nearest_neighbor::create_nearest_neighbor(
            pattern[i].first,
            conf,
            table,
            id));
  }
  return nearest_neighbors;
}

const size_t MAX_SIZE = 3;

std::vector<shared_ptr<unlearner_base> > create_unlearners() {
  std::vector<shared_ptr<unlearner_base> > unlearners;

  {
    lru_unlearner::config config;
    config.max_size = MAX_SIZE;
    unlearners.push_back(shared_ptr<unlearner_base>(new lru_unlearner(config)));
  }
  for (int i = 1091; i < 1092; ++i) {
    random_unlearner::config config;
    config.max_size = MAX_SIZE;
    config.seed = i;
    unlearners.push_back(shared_ptr<unlearner_base>(new random_unlearner(
        config)));
  }

  return unlearners;
}

}  // namespace


class nearest_neighbor_test
    : public ::testing::TestWithParam<
        shared_ptr<core::nearest_neighbor::nearest_neighbor_base> > {
 protected:
  shared_ptr<core::driver::nearest_neighbor> create_driver() const {
    return shared_ptr<core::driver::nearest_neighbor>(
         new core::driver::nearest_neighbor(GetParam(), make_fv_converter()));
  }
  void SetUp() {
    nn_driver_ = create_driver();
  }
  void TearDown() {
    nn_driver_->clear();
  }
  shared_ptr<core::driver::nearest_neighbor> nn_driver_;
};

datum single_num_datum(const string& key, double value) {
  core::fv_converter::datum d;
  d.num_values_.push_back(make_pair(key, value));
  return d;
}
datum single_str_datum(const string& key, const string& value) {
  core::fv_converter::datum d;
  d.string_values_.push_back(make_pair(key, value));
  return d;
}

TEST_P(nearest_neighbor_test, set_row) {
  nn_driver_->set_row("a", single_str_datum("a", "hoge"));
}

TEST_P(nearest_neighbor_test, similar_row_from_id) {
  nn_driver_->set_row("a", single_str_datum("x", "hoge"));
  nn_driver_->set_row("b", single_str_datum("y", "fuga"));
  vector<pair<string, float> > result =
      nn_driver_->similar_row("a", 100);
  ASSERT_EQ(2, result.size());
  ASSERT_EQ("a", result[0].first);
  ASSERT_EQ("b", result[1].first);
}

TEST_P(nearest_neighbor_test, similar_row_from_datum) {
  nn_driver_->set_row("a", single_str_datum("x", "hoge"));
  nn_driver_->set_row("b", single_str_datum("y", "fuga"));
  vector<pair<string, float> > result =
      nn_driver_->similar_row(single_str_datum("x", "hoge"), 100);
  ASSERT_EQ(2, result.size());
  ASSERT_EQ("a", result[0].first);
  ASSERT_EQ("b", result[1].first);
}

TEST_P(nearest_neighbor_test, neighbor_row_from_id) {
  nn_driver_->set_row("a", single_str_datum("x", "hoge"));
  nn_driver_->set_row("b", single_str_datum("y", "fuga"));
  vector<pair<string, float> > result =
      nn_driver_->neighbor_row_from_id("a", 100);
  ASSERT_EQ(2, result.size());
  ASSERT_EQ("a", result[0].first);
  ASSERT_EQ("b", result[1].first);
}

TEST_P(nearest_neighbor_test, neighbor_row2_from_datum) {
  nn_driver_->set_row("a", single_str_datum("x", "hoge"));
  nn_driver_->set_row("b", single_str_datum("y", "fuga"));
  vector<pair<string, float> > result =
      nn_driver_->neighbor_row_from_datum(
          single_str_datum("x", "hoge"), 100);
  ASSERT_EQ(2, result.size());
  ASSERT_EQ("a", result[0].first);
  ASSERT_EQ("b", result[1].first);
}

TEST_P(nearest_neighbor_test, neighbor_row_and_similar_row) {
  // neighbor_row and similar_row should return the same order result
  const size_t num = 200;
  for (size_t i = 0; i < num; ++i) {
    const string key = lexical_cast<string>(i);
    datum d = single_str_datum("x" + key, "a");
    d.string_values_.push_back(make_pair("y", "b"));
    d.string_values_.push_back(make_pair("z" + key + key, "c"));
    nn_driver_->set_row(key, d);
  }

  for (size_t i = 0; i < num; ++i) {
    const string key = lexical_cast<string>(i);
    datum d = single_str_datum("x" + key, "a");

    vector<pair<string, float> > nr_result =
        nn_driver_->neighbor_row_from_datum(d, 100);
    vector<pair<string, float> > sr_result =
        nn_driver_->similar_row(d, 100);
    ASSERT_EQ(nr_result.size(), sr_result.size());
    for (size_t j = 0; j < nr_result.size(); ++j) {
      ASSERT_EQ(nr_result[j].first, sr_result[j].first);
    }
  }
}

TEST_P(nearest_neighbor_test, clear) {
  for (int i = 0; i < 100; ++i) {
    nn_driver_->set_row("a" + lexical_cast<string>(i),
                               single_num_datum("x", i));
  }
  {
    vector<pair<string, float> > result =
        nn_driver_->neighbor_row_from_datum(
            single_num_datum("x", 1), 100);
    ASSERT_EQ("a1", result[0].first);
  }
  nn_driver_->clear();
  {
    for (int i = 0; i < 100; ++i) {
    vector<pair<string, float> > result =
        nn_driver_->neighbor_row_from_datum(
          single_num_datum("a" + lexical_cast<string>(i), i), 100);
      ASSERT_EQ(0u, result.size());
    }
  }
}

TEST_P(nearest_neighbor_test, save_load) {
  {
    core::fv_converter::datum d;
    d.string_values_.push_back(make_pair("k1", "val"));
    nn_driver_->set_row("1", d);
  }

  // save to a buffer
  msgpack::sbuffer sbuf;
  framework::stream_writer<msgpack::sbuffer> st(sbuf);
  framework::jubatus_packer jp(st);
  framework::packer packer(jp);
  nn_driver_->pack(packer);

  // restart the driver
  TearDown();
  SetUp();

  // unpack the buffer
  msgpack::unpacked unpacked;
  msgpack::unpack(&unpacked, sbuf.data(), sbuf.size());
  nn_driver_->unpack(unpacked.get());

  vector<pair<string, float> > res
      = nn_driver_->similar_row("1", 1);
  ASSERT_EQ(1u, res.size());
  EXPECT_EQ("1", res[0].first);
}

TEST_P(nearest_neighbor_test, small) {
  nn_driver_->set_row("id1", create_datum_2d(2.f, 0.f));
  nn_driver_->set_row("id2", create_datum_2d(2.f, 1.f));
  nn_driver_->set_row("id3", create_datum_2d(0.f, 2.f));

  nn_driver_->neighbor_row_from_id("id1", 2);
  nn_driver_->neighbor_row_from_id("id2", 2);
  nn_driver_->neighbor_row_from_id("id3", 2);

  nn_driver_->neighbor_row_from_datum(create_datum_2d(1.f, 1.f), 2);
}

TEST_P(nearest_neighbor_test, small_mix) {
  framework::linear_mixable* nn_mixable =
    dynamic_cast<framework::linear_mixable*>(nn_driver_->get_mixable());
  shared_ptr<driver::nearest_neighbor> other = create_driver();
  framework::linear_mixable* other_mixable =
    dynamic_cast<framework::linear_mixable*>(other->get_mixable());
  ASSERT_TRUE(nn_mixable);
  ASSERT_TRUE(other_mixable);

  nn_driver_->set_row("a", single_str_datum("x", "hoge"));
  nn_driver_->set_row("b", single_str_datum("y", "fuga"));

  msgpack::sbuffer data;
  {
    core::framework::stream_writer<msgpack::sbuffer> st(data);
    core::framework::jubatus_packer jp(st);
    core::framework::packer pk(jp);
    nn_mixable->get_diff(pk);
  }
  {
    msgpack::sbuffer sbuf;
    core::framework::stream_writer<msgpack::sbuffer> st(sbuf);
    core::framework::jubatus_packer jp(st);
    core::framework::packer pk(jp);
    other_mixable->get_diff(pk);

    msgpack::unpacked msg;
    msgpack::unpack(&msg, sbuf.data(), sbuf.size());
    framework::diff_object diff = other_mixable->convert_diff_object(msg.get());

    msgpack::unpacked data_msg;
    msgpack::unpack(&data_msg, data.data(), data.size());

    other_mixable->mix(data_msg.get(), diff);
    other_mixable->put_diff(diff);
  }
}

INSTANTIATE_TEST_CASE_P(nearest_neighbor_test_instance,
    nearest_neighbor_test,
    testing::ValuesIn(create_nearest_neighbor_bases()));

class nearest_neighbor_with_unlearning_test
    : public ::testing::TestWithParam<
          std::tr1::tuple<
              shared_ptr<nearest_neighbor_base>,
              shared_ptr<unlearner_base> > > {
 protected:
  void SetUp() {
    nn_driver_.reset(new nearest_neighbor(
        std::tr1::get<0>(GetParam()),
        make_fv_converter(),
        std::tr1::get<1>(GetParam())));
  }
  void TearDown() {
    nn_driver_->clear();
    nn_driver_.reset();
  }

  bool is_hit(
      const std::string& should_hit_id,
      const fv_converter::datum& d,
      size_t size) const {
    std::vector<std::pair<std::string, float> > hit =
        nn_driver_->neighbor_row_from_datum(d, size);
    for (size_t i = 0; i < hit.size(); ++i) {
      if (hit[i].first == should_hit_id) {
        return true;
      }
    }
    return false;
  }

  shared_ptr<nearest_neighbor> nn_driver_;
};

TEST_P(nearest_neighbor_with_unlearning_test, unlearning) {
  nn_driver_->set_row("id1", create_datum_2d(2.f, 0.f));
  nn_driver_->set_row("id2", create_datum_2d(2.f, 1.f));
  nn_driver_->set_row("id3", create_datum_2d(0.f, 2.f));
  EXPECT_TRUE(is_hit("id1", create_datum_2d(1.f, 1.f), 3));

  nn_driver_->set_row("id2", create_datum_2d(2.f, 2.f));
  EXPECT_TRUE(is_hit("id1", create_datum_2d(1.f, 1.f), 3));

  nn_driver_->set_row("id4", create_datum_2d(1.f, 2.f));
  size_t hit_count = 0;
  hit_count += is_hit("id1", create_datum_2d(1.f, 1.f), 3);
  hit_count += is_hit("id2", create_datum_2d(1.f, 1.f), 3);
  hit_count += is_hit("id3", create_datum_2d(1.f, 1.f), 3);
  EXPECT_EQ(2u, hit_count);
}

INSTANTIATE_TEST_CASE_P(
    nearest_neighbor_with_unlearning_test_instance,
    nearest_neighbor_with_unlearning_test,
    ::testing::Combine(
        ::testing::ValuesIn(create_nearest_neighbor_bases()),
        ::testing::ValuesIn(create_unlearners())));

}  // namespace driver
}  // namespace core
}  // namespace jubatus
