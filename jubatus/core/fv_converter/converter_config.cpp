// Jubatus: Online machine learning framework for distributed environment
// Copyright (C) 2011 Preferred Infrastructure and Nippon Telegraph and Telephone Corporation.
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

#include "converter_config.hpp"

#include <map>
#include <string>
#include <vector>
#include "jubatus/util/text/json.h"
#include "jubatus/util/lang/bind.h"
#include "jubatus/util/lang/function.h"
#include "binary_feature.hpp"
#include "binary_feature_factory.hpp"
#include "combination_feature.hpp"
#include "combination_feature_factory.hpp"
#include "except_match.hpp"
#include "datum_to_fv_converter.hpp"
#include "exception.hpp"
#include "factory.hpp"
#include "key_matcher.hpp"
#include "key_matcher_factory.hpp"
#include "num_feature.hpp"
#include "num_feature_impl.hpp"
#include "num_feature_factory.hpp"
#include "num_filter.hpp"
#include "num_filter_factory.hpp"
#include "space_splitter.hpp"
#include "string_feature_factory.hpp"
#include "string_filter.hpp"
#include "string_filter_factory.hpp"
#include "without_split.hpp"

namespace jubatus {
namespace core {
namespace fv_converter {

namespace {

typedef jubatus::util::lang::shared_ptr<string_feature> string_feature_ptr;
typedef jubatus::util::lang::shared_ptr<key_matcher> matcher_ptr;
typedef jubatus::util::lang::shared_ptr<num_feature> num_feature_ptr;
typedef jubatus::util::lang::shared_ptr<binary_feature> binary_feature_ptr;
typedef jubatus::util::lang::shared_ptr<combination_feature>
    combination_feature_ptr;
typedef jubatus::util::lang::shared_ptr<string_filter> string_filter_ptr;
typedef jubatus::util::lang::shared_ptr<num_filter> num_filter_ptr;

splitter_weight_type make_weight_type(
    const std::string& sample, const std::string& global) {
  frequency_weight_type sample_type;
  if (sample == "bin") {
    sample_type = FREQ_BINARY;
  } else if (sample == "tf") {
    sample_type = TERM_FREQUENCY;
  } else if (sample == "log_tf") {
    sample_type = LOG_TERM_FREQUENCY;
  } else {
    throw JUBATUS_EXCEPTION(
        converter_exception(std::string("unknown sample weight: ") + sample));
  }

  term_weight_type global_type;
  if (global == "bin") {
    global_type = TERM_BINARY;
  } else if (global == "idf") {
    global_type = IDF;
  } else if (global == "weight") {
    global_type = WITH_WEIGHT_FILE;
  } else {
    throw JUBATUS_EXCEPTION(
        converter_exception(std::string("unknown global weight: ") + global));
  }
  return splitter_weight_type(sample_type, global_type);
}

std::string get_or_die(
    const std::map<std::string, std::string>& m,
    const std::string& key) {
  std::map<std::string, std::string>::const_iterator it = m.find(key);
  if (it == m.end()) {
    throw JUBATUS_EXCEPTION(
        converter_exception(std::string("unknown parameter: ") + key));
  } else {
    return it->second;
  }
}

matcher_ptr create_key_matcher(
    const std::string& key,
    const jubatus::util::data::optional<std::string>& except) {
  key_matcher_factory f;
  if (except) {
    matcher_ptr m1(f.create_matcher(key));
    matcher_ptr m2(f.create_matcher(*except));
    return matcher_ptr(new except_match(m1, m2));
  } else {
    return matcher_ptr(f.create_matcher(key));
  }
}

void init_string_filter_types(
    const std::map<std::string, param_t>& filter_types,
    std::map<std::string, string_filter_ptr>& filters,
    const string_filter_factory::create_function ext) {
  string_filter_factory f(ext);
  for (std::map<std::string, param_t>::const_iterator it = filter_types.begin();
      it != filter_types.end(); ++it) {
    const std::string& name = it->first;
    const std::map<std::string, std::string>& param = it->second;

    std::string method = get_or_die(param, "method");
    string_filter_ptr filter(f.create(method, param));
    filters[name] = filter;
  }
}

void init_num_filter_types(
    const std::map<std::string, param_t>& filter_types,
    std::map<std::string, num_filter_ptr>& filters,
    const num_filter_factory::create_function& ext) {
  num_filter_factory f(ext);
  for (std::map<std::string, param_t>::const_iterator it = filter_types.begin();
      it != filter_types.end(); ++it) {
    const std::string& name = it->first;
    const std::map<std::string, std::string>& param = it->second;

    std::string method = get_or_die(param, "method");
    num_filter_ptr filter(f.create(method, param));
    filters[name] = filter;
  }
}

void init_num_filter_rules(
    const std::vector<filter_rule>& filter_rules,
    const std::map<std::string, num_filter_ptr>& filters,
    datum_to_fv_converter& conv) {
  for (size_t i = 0; i < filter_rules.size(); ++i) {
    const filter_rule& rule = filter_rules[i];
    std::map<std::string, num_filter_ptr>::const_iterator it =
        filters.find(rule.type);
    if (it == filters.end()) {
      throw JUBATUS_EXCEPTION(
          converter_exception("unknown type: " + rule.type));
    }

    matcher_ptr m(create_key_matcher(rule.key, rule.except));
    conv.register_num_filter(m, it->second, rule.suffix);
  }
}

void register_default_string_types(
    std::map<std::string, string_feature_ptr>& splitters) {
  splitters["str"] = string_feature_ptr(new without_split());
  splitters["space"] = string_feature_ptr(new space_splitter());
}

void init_string_types(
    const std::map<std::string, param_t>& string_types,
    std::map<std::string, string_feature_ptr>& splitters,
    string_feature_factory::create_function ext) {
  string_feature_factory f(ext);
  for (std::map<std::string, param_t>::const_iterator it = string_types.begin();
      it != string_types.end(); ++it) {
    const std::string& name = it->first;
    const std::map<std::string, std::string>& param = it->second;

    std::string method = get_or_die(param, "method");
    string_feature_ptr splitter(f.create(method, param));
    splitters[name] = splitter;
  }
}

void init_string_filter_rules(
    const std::vector<filter_rule>& filter_rules,
    const std::map<std::string, string_filter_ptr>& filters,
    datum_to_fv_converter& conv) {
  for (size_t i = 0; i < filter_rules.size(); ++i) {
    const filter_rule& rule = filter_rules[i];
    std::map<std::string, string_filter_ptr>::const_iterator it =
        filters.find(rule.type);
    if (it == filters.end()) {
      throw JUBATUS_EXCEPTION(
          converter_exception("unknown type: " + rule.type));
    }

    matcher_ptr m(create_key_matcher(rule.key, rule.except));
    conv.register_string_filter(m, it->second, rule.suffix);
  }
}

void init_string_rules(
    const std::vector<string_rule>& string_rules,
    const std::map<std::string, string_feature_ptr>& splitters,
    datum_to_fv_converter& conv) {
  for (size_t i = 0; i < string_rules.size(); ++i) {
    const string_rule& rule = string_rules[i];
    matcher_ptr m(create_key_matcher(rule.key, rule.except));
    std::map<std::string, string_feature_ptr>::const_iterator it =
        splitters.find(rule.type);
    if (it == splitters.end()) {
      throw JUBATUS_EXCEPTION(
          converter_exception("unknown type: " + rule.type));
    }

    std::vector<splitter_weight_type> ws;
    ws.push_back(make_weight_type(rule.sample_weight, rule.global_weight));
    conv.register_string_rule(rule.type, m, it->second, ws);
  }
}

void register_default_num_types(
    std::map<std::string, num_feature_ptr>& num_features) {
  num_features["num"] = num_feature_ptr(new num_value_feature());
  num_features["log"] = num_feature_ptr(new num_log_feature());
  num_features["str"] = num_feature_ptr(new num_string_feature());
}

void init_num_types(
    const std::map<std::string, param_t>& num_types,
    std::map<std::string, num_feature_ptr>& num_features,
    num_feature_factory::create_function ext) {
  num_feature_factory f(ext);
  for (std::map<std::string, param_t>::const_iterator it = num_types.begin();
      it != num_types.end(); ++it) {
    const std::string& name = it->first;
    const std::map<std::string, std::string>& param = it->second;

    std::string method = get_or_die(param, "method");
    num_feature_ptr feature(f.create(method, param));
    num_features[name] = feature;
  }
}

void init_num_rules(
    const std::vector<num_rule>& num_rules,
    const std::map<std::string, num_feature_ptr>& num_features,
    datum_to_fv_converter& conv) {
  for (size_t i = 0; i < num_rules.size(); ++i) {
    const num_rule& rule = num_rules[i];
    matcher_ptr m(create_key_matcher(rule.key, rule.except));
    std::map<std::string, num_feature_ptr>::const_iterator it =
        num_features.find(rule.type);
    if (it == num_features.end()) {
      throw JUBATUS_EXCEPTION(
          converter_exception("unknown type: " + rule.type));
    }

    conv.register_num_rule(rule.type, m, it->second);
  }
}

void init_binary_types(
    const std::map<std::string, param_t>& binary_types,
    std::map<std::string, binary_feature_ptr>& binary_features,
    binary_feature_factory::create_function ext) {
  binary_feature_factory f(ext);
  for (std::map<std::string, param_t>::const_iterator it = binary_types.begin();
       it != binary_types.end(); ++it) {
    const std::string& name = it->first;
    const std::map<std::string, std::string>& param = it->second;

    std::string method = get_or_die(param, "method");
    binary_feature_ptr feature(f.create(method, param));
    binary_features[name] = feature;
  }
}

void init_binary_rules(
    const std::vector<binary_rule>& binary_rules,
    const std::map<std::string, binary_feature_ptr>& binary_features,
    datum_to_fv_converter& conv) {
  key_matcher_factory f;
  for (size_t i = 0; i < binary_rules.size(); ++i) {
    const binary_rule& rule = binary_rules[i];
    matcher_ptr m(f.create_matcher(rule.key));
    std::map<std::string, binary_feature_ptr>::const_iterator it =
        binary_features.find(rule.type);
    if (it == binary_features.end()) {
      throw JUBATUS_EXCEPTION(
          converter_exception("unknown type: " + rule.type));
    }

    conv.register_binary_rule(rule.type, m, it->second);
  }
}

void init_combination_types(
    const std::map<std::string, param_t>& combination_types,
    std::map<std::string, combination_feature_ptr>& combination_features,
    combination_feature_factory::create_function ext) {
  combination_feature_factory f(ext);
  for (
      std::map<std::string, param_t>::const_iterator it =
          combination_types.begin();
      it != combination_types.end(); ++it) {
    const std::string& name = it->first;
    const std::map<std::string, std::string>& param = it->second;

    std::string method = get_or_die(param, "method");
    combination_feature_ptr feature(f.create(method, param));
    combination_features[name] = feature;
  }
}

void init_combination_rules(
    const std::vector<combination_rule>& combination_rules,
    const std::map<std::string, combination_feature_ptr>& combination_features,
    datum_to_fv_converter& conv) {
  key_matcher_factory f;
  for (size_t i = 0; i < combination_rules.size(); ++i) {
    const combination_rule& rule = combination_rules[i];
    matcher_ptr m_left(f.create_matcher(rule.key_left));
    matcher_ptr m_right(f.create_matcher(rule.key_right));
    std::map<std::string, combination_feature_ptr>::const_iterator it =
        combination_features.find(rule.type);
    if (it == combination_features.end()) {
      throw JUBATUS_EXCEPTION(
          converter_exception("unknown type: " + rule.type));
    }

    conv.register_combination_rule(rule.type, m_left, m_right, it->second);
  }
}

}  // namespace

void initialize_converter(
    const converter_config& config,
    datum_to_fv_converter& conv,
    const factory_extender* ext) {
  using jubatus::util::lang::bind;
  using jubatus::util::lang::_1;
  using jubatus::util::lang::_2;

  if (config.hash_max_size.bool_test() && *config.hash_max_size.get() <= 0) {
    std::stringstream msg;
    msg << "hash_max_size must be positive, but is "
        << *config.hash_max_size.get();
    throw JUBATUS_EXCEPTION(converter_exception(msg.str()));
  }

  std::map<std::string, string_filter_ptr> string_filters;
  if (config.string_filter_types) {
    string_filter_factory::create_function f;
    if (ext) {
      f = bind(&factory_extender::create_string_filter, ext, _1, _2);
    }
    init_string_filter_types(*config.string_filter_types, string_filters, f);
  }

  std::map<std::string, num_filter_ptr> num_filters;
  if (config.num_filter_types) {
    num_filter_factory::create_function f;
    if (ext) {
      f = bind(&factory_extender::create_num_filter, ext, _1, _2);
    }
    init_num_filter_types(*config.num_filter_types, num_filters, f);
  }

  std::map<std::string, string_feature_ptr> splitters;
  register_default_string_types(splitters);
  if (config.string_types) {
    string_feature_factory::create_function f;
    if (ext) {
      f = bind(&factory_extender::create_string_feature, ext, _1, _2);
    }
    init_string_types(*config.string_types, splitters, f);
  }

  std::map<std::string, num_feature_ptr> num_features;
  register_default_num_types(num_features);
  if (config.num_types) {
    num_feature_factory::create_function f;
    if (ext) {
      f = bind(&factory_extender::create_num_feature, ext, _1, _2);
    }
    init_num_types(*config.num_types, num_features, f);
  }

  std::map<std::string, binary_feature_ptr> binary_features;
  if (config.binary_types) {
    binary_feature_factory::create_function f;
    if (ext) {
      f = bind(&factory_extender::create_binary_feature, ext, _1, _2);
    }
    init_binary_types(*config.binary_types, binary_features, f);
  }

  conv.clear_rules();
  if (config.string_filter_rules) {
    init_string_filter_rules(*config.string_filter_rules, string_filters, conv);
  }
  if (config.num_filter_rules) {
    init_num_filter_rules(*config.num_filter_rules, num_filters, conv);
  }
  if (config.string_rules) {
    init_string_rules(*config.string_rules, splitters, conv);
  }
  if (config.num_rules) {
    init_num_rules(*config.num_rules, num_features, conv);
  }
  if (config.binary_rules) {
    init_binary_rules(*config.binary_rules, binary_features, conv);
  }

  if (config.hash_max_size.bool_test()) {
    conv.set_hash_max_size(*config.hash_max_size.get());
  }
}

jubatus::util::lang::shared_ptr<datum_to_fv_converter> make_fv_converter(
    const converter_config& config, const factory_extender* extender) {
  jubatus::util::lang::shared_ptr<fv_converter::datum_to_fv_converter>
    converter(new fv_converter::datum_to_fv_converter);
  fv_converter::initialize_converter(config, *converter, extender);
  return converter;
}

}  // namespace fv_converter
}  // namespace core
}  // namespace jubatus
