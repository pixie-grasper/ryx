// codegen.cc -- Code Generator which parses the grammar.
// Copyright (C) 2018 pixie-grasper
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "codegen.h"

#include <set>

class ostream_with_newlines {
  std::ostream* os;

 public:
  explicit ostream_with_newlines(std::ostream* os_) : os(os_) {
    return;
  }

  template <typename T>
  ostream_with_newlines& operator<<(T&& s) {
    (*os) << std::forward<T>(s) << std::endl;
    return *this;
  }
};

extern void generate_code(std::ostream* header_,
                          std::ostream* ccfile_,
                          const token_set_type& terminate_symbols,
                          const token_set_type& non_terminate_symbols,
                          const id_to_token_type& id_to_token,
                          const token_to_id_type& token_to_id,
                          const rules_type& rules,
                          const table_type& table) {
  if (header_ == nullptr || ccfile_ == nullptr) {
    return;
  }

  ostream_with_newlines header(header_);
  ostream_with_newlines ccfile(ccfile_);

  header << "// Copyright (C) 2018 pixie-grasper"
         << "//"
         << "// This program is free software: you can redistribute it and/or modify"
         << "// it under the terms of the GNU General Public License as published by"
         << "// the Free Software Foundation, either version 3 of the License, or"
         << "// (at your option) any later version."
         << "//"
         << "// This program is distributed in the hope that it will be useful,"
         << "// but WITHOUT ANY WARRANTY; without even the implied warranty of"
         << "// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the"
         << "// GNU General Public License for more details."
         << "//"
         << "// You should have received a copy of the GNU General Public License"
         << "// along with this program.  If not, see <https://www.gnu.org/licenses/>."
         << "";

  ccfile << "// Copyright (C) 2018 pixie-grasper"
         << "//"
         << "// This program is free software: you can redistribute it and/or modify"
         << "// it under the terms of the GNU General Public License as published by"
         << "// the Free Software Foundation, either version 3 of the License, or"
         << "// (at your option) any later version."
         << "//"
         << "// This program is distributed in the hope that it will be useful,"
         << "// but WITHOUT ANY WARRANTY; without even the implied warranty of"
         << "// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the"
         << "// GNU General Public License for more details."
         << "//"
         << "// You should have received a copy of the GNU General Public License"
         << "// along with this program.  If not, see <https://www.gnu.org/licenses/>."
         << "";

  header << "#ifndef RYX_H_"
         << "#define RYX_H_"
         << "";

  header << "#include <stdint.h>"
         << "#include <stdlib.h>"
         << "";

  header << "typedef user_data_t void*;"
         << "";

  header << "enum node_kind_t {";

  std::unordered_map<token_id, std::size_t> token_id_to_enum_id{};
  std::vector<std::string> es_list{};
  for (int i = 0; i < 256; ++i) {
    std::string token_string{};
    if (0x20 <= i && i <= 0x7E) {
      token_string.push_back('\'');
      token_string.push_back(static_cast<char>(i));
      if (i == '\\') {
        token_string.push_back(static_cast<char>(i));
      }
      token_string.push_back('\'');
    } else {
      token_string = "0x";
      token_string.push_back(itoh((i & 0xF0) >> 4));
      token_string.push_back(itoh(i & 0x0F));
    }
    auto&& it = token_to_id.find(token_string);
    if (it == token_to_id.end()) {
      continue;
    }
    std::size_t number = es_list.size();
    std::string enum_string = "node_kind_char_0x";
    enum_string.push_back(itoh((i & 0xF0) >> 4));
    enum_string.push_back(itoh(i & 0x0F));
    std::string header_string = "  "
                              + enum_string
                              + " = "
                              + std::to_string(number)
                              + ", // "
                              + token_string;
    header << header_string;
    token_id_to_enum_id[it->second] = number;
    es_list.push_back(enum_string);
  }

  std::set<std::string> sorted_ts_string{};
  for (auto&& it = terminate_symbols.begin();
              it != terminate_symbols.end();
              ++it) {
    if (token_id_to_enum_id.find(*it) != token_id_to_enum_id.end()) {
      continue;
    }
    sorted_ts_string.insert(id_to_token.find(*it)->second);
  }
  std::size_t ts_index = 0;
  for (auto&& it = sorted_ts_string.begin(); it != sorted_ts_string.end(); ++it) {
    std::size_t number = es_list.size();
    std::string enum_string = "node_kind_term_" + std::to_string(ts_index);
    std::string header_string = "  "
                              + enum_string
                              + " = "
                              + std::to_string(number)
                              + ", // "
                              + *it;
    header << header_string;
    token_id_to_enum_id[token_to_id.find(*it)->second] = number;
    es_list.push_back(enum_string);
    ++ts_index;
  }

  std::set<std::string> sorted_nts_string{};
  for (auto&& it = non_terminate_symbols.begin();
              it != non_terminate_symbols.end();
              ++it) {
    sorted_nts_string.insert(id_to_token.at(*it));
  }
  std::size_t nts_index = 0;
  for (auto&& it = sorted_nts_string.begin(); it != sorted_nts_string.end(); ++it) {
    std::size_t number = es_list.size();
    std::string enum_string = "node_kind_nonterm_" + std::to_string(nts_index);
    std::string header_string = "  "
                              + enum_string
                              + " = "
                              + std::to_string(number)
                              + ", // "
                              + *it;
    header << header_string;
    token_id_to_enum_id[token_to_id.find(*it)->second] = number;
    es_list.push_back(enum_string);
    ++nts_index;
  }

  header << "};"
         << "";

  header << "struct ryx_tree_t {"
         << "  size_t node_length;"
         << "  node_kind_t kind;"
         << "  struct ryx_tree_t* node;"
         << "};"
         << "";

  header << "#ifdef __cplusplus"
         << "extern \"C\" {"
         << "#endif"
         << "";

  header << "// TODO: need to implement yourself!"
         << "user_data_t input_initialize(void);"
         << "int input_getchar(user_data_t data);"
         << "";

  header << "// RYX interface"
         << "struct ryx_tree_t* parse(void);"
         << "";

  header << "#ifdef __cplusplus"
         << "}  // extern \"C\""
         << "#endif"
         << "";

  header << "#endif  // RYX_H_"
         << "";

  return;
}
