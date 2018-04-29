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
                          token_id first_nonterm,
                          token_id last_term,
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

  ccfile << "#include \"ryx_parse.h\""
         << ""
         << "#include <stdint.h>"
         << "#include <stdio.h>"
         << "#include <stdlib.h>"
         << "";

  header << "#ifdef __cplusplus"
         << "#define INTERN namespace {"
         << "#define INTERN_END }"
         << "#define EXTERN extern \"C\""
         << "#define CAST(x,y) static_cast<x>(y)"
         << "#if __cplusplus >= 201103L"
         << "#define NULLPTR nullptr"
         << "#else"
         << "#define NULLPTR 0"
         << "#endif"
         << "#else"
         << "#define INTERN static"
         << "#define INTERN_END"
         << "#define EXTERN extern"
         << "#define CAST(x,y) ((x)(y))"
         << "#define NULLPTR 0"
         << "#endif"
         << "";

  ccfile << "#define MALLOC(t) CAST(t*, malloc(sizeof(t)))"
         << "";

  header << "typedef void* ryx_user_data;"
         << "";

  header << "enum ryx_node_kind {";

  std::unordered_map<token_id, std::string> token_id_to_enum_string{};
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
    std::size_t number = token_id_to_enum_string.size();
    std::string enum_string = "ryx_node_kind_char_0x";
    enum_string.push_back(itoh((i & 0xF0) >> 4));
    enum_string.push_back(itoh(i & 0x0F));
    std::string header_string = "  "
                              + enum_string
                              + " = "
                              + std::to_string(number)
                              + ", // "
                              + token_string;
    header << header_string;
    token_id_to_enum_string[it->second] = enum_string;
  }

  std::set<std::string> sorted_ts_string{};
  for (auto&& it = terminate_symbols.begin();
              it != terminate_symbols.end();
              ++it) {
    if (token_id_to_enum_string.find(*it) != token_id_to_enum_string.end()) {
      continue;
    }
    sorted_ts_string.insert(id_to_token.find(*it)->second);
  }
  std::size_t ts_index = 0;
  for (auto&& it = sorted_ts_string.begin(); it != sorted_ts_string.end(); ++it) {
    std::size_t number = token_id_to_enum_string.size();
    std::string enum_string = "ryx_node_kind_term_" + std::to_string(ts_index);
    std::string header_string = "  "
                              + enum_string
                              + " = "
                              + std::to_string(number)
                              + ", // "
                              + *it;
    header << header_string;
    token_id_to_enum_string[token_to_id.find(*it)->second] = enum_string;
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
    std::size_t number = token_id_to_enum_string.size();
    std::string enum_string = "ryx_node_kind_nonterm_" + std::to_string(nts_index);
    std::string header_string = "  "
                              + enum_string
                              + " = "
                              + std::to_string(number)
                              + ", // "
                              + *it;
    header << header_string;
    token_id_to_enum_string[token_to_id.find(*it)->second] = enum_string;
    ++nts_index;
  }

  header << "};"
         << "";

  header << "struct ryx_token {"
         << "  enum ryx_node_kind kind;"
         << "  ryx_user_data data;"
         << "  void (*free)(struct ryx_token* token);"
         << "};"
         << "";

  ccfile << "struct ryx_shared_token {"
         << "  struct ryx_token* token;"
         << "  int refcount;"
         << "};"
         << "";

  header << "struct ryx_tree;"
         << "";

  ccfile << "struct ryx_tree {"
         << "  struct ryx_shared_token* shared_token;"
         << "  struct ryx_tree* next_node;"
         << "  struct ryx_tree* sub_node;"
         << "};"
         << "";

  ccfile << "struct ryx_stack {"
         << "  struct ryx_shared_token* shared_token;"
         << "  struct ryx_stack* next;"
         << "};"
         << "";

  header << "// TODO: need to implement yourself!"
         << "EXTERN struct ryx_token* ryx_get_next_token(ryx_user_data input);"
         << "";

  header << "// RYX interface begin";

  ccfile << "INTERN"
         << "void ryx_free_internal_token(struct ryx_token* token) {"
         << "  free(token);"
         << "  return;"
         << "}"
         << "INTERN_END"
         << "";

  ccfile << "INTERN"
         << "void ryx_ref_shared_token(struct ryx_shared_token* shared_token) {"
         << "  shared_token->refcount++;"
         << "  return;"
         << "}"
         << "INTERN_END"
         << "";

  ccfile << "INTERN"
         << "void ryx_unref_shared_token(struct ryx_shared_token* shared_token) {"
         << "  shared_token->refcount--;"
         << "  if (shared_token->refcount == 0) {"
         << "    if (shared_token->token->free != NULLPTR) {"
         << "      shared_token->token->free(shared_token->token);"
         << "    }"
         << "    free(shared_token);"
         << "  }"
         << "  return;"
         << "}"
         << "INTERN_END"
         << "";

  ccfile << "INTERN"
         << "struct ryx_shared_token* ryx_make_internal_token(enum ryx_node_kind kind) {"
         << "  struct ryx_shared_token* shared_token;"
         << "  struct ryx_token* token;"
         << ""
         << "  token = MALLOC(struct ryx_token);"
         << "  token->kind = kind;"
         << "  token->data = NULLPTR;"
         << "  token->free = ryx_free_internal_token;"
         << ""
         << "  shared_token = MALLOC(struct ryx_shared_token);"
         << "  shared_token->token = token;"
         << "  shared_token->refcount = 1;"
         << ""
         << "  return shared_token;"
         << "}"
         << "INTERN_END"
         << "";

  ccfile << "INTERN"
         << "struct ryx_shared_token* ryx_make_shared_token(struct ryx_token* token) {"
         << "  struct ryx_shared_token* shared_token;"
         << ""
         << "  shared_token = MALLOC(struct ryx_shared_token);"
         << "  shared_token->token = token;"
         << "  shared_token->refcount = 1;"
         << ""
         << "  return shared_token;"
         << "}"
         << "INTERN_END"
         << "";

  ccfile << "INTERN"
         << "struct ryx_stack* ryx_stack_push_copy(struct ryx_stack* stack,"
         << "                                      struct ryx_shared_token* shared_token) {"
         << "  struct ryx_stack* ret;"
         << ""
         << "  ret = MALLOC(struct ryx_stack);"
         << "  ret->shared_token = shared_token;"
         << "  ret->next = stack;"
         << ""
         << "  ryx_ref_shared_token(shared_token);"
         << ""
         << "  return ret;"
         << "}"
         << "INTERN_END"
         << "";

  ccfile << "INTERN"
         << "struct ryx_stack* ryx_stack_push_move(struct ryx_stack* stack,"
         << "                                      struct ryx_shared_token* shared_token) {"
         << "  struct ryx_stack* ret;"
         << ""
         << "  ret = MALLOC(struct ryx_stack);"
         << "  ret->shared_token = shared_token;"
         << "  ret->next = stack;"
         << ""
         << "  return ret;"
         << "}"
         << "INTERN_END"
         << "";

  ccfile << "INTERN"
         << "struct ryx_stack* ryx_make_initial_stack(void) {"
         << "  struct ryx_stack* ret;"
         << ""
         << "  ret = NULLPTR;"
         << ""
         << "  ret = ryx_stack_push_move(ret, ryx_make_internal_token("
            + token_id_to_enum_string[last_term]
            + "));"
         << "  ret = ryx_stack_push_move(ret, ryx_make_internal_token("
            + token_id_to_enum_string[first_nonterm]
            + "));"
         << ""
         << "  return ret;"
         << "}"
         << "INTERN_END"
         << "";

  header << "EXTERN struct ryx_tree* ryx_parse(ryx_user_data input);";
  ccfile << "EXTERN struct ryx_tree* ryx_parse(ryx_user_data input) {"
         << "  struct ryx_stack* stack;"
         << "  struct ryx_tree* ret;"
         << "  struct ryx_tree* node;"
         << "  struct ryx_shared_token* shared_token;"
         << "  int finished;"
         << ""
         << "  stack = ryx_make_initial_stack();"
         << "  ret = NULLPTR;"
         << "  node = ret;"
         << "  finished = 0;"
         << ""
         << "  while (!finished) {"
         << "    switch (stack->shared_token->token->kind) {";

  ccfile << "    }"
         << "  }"
         << "  return ret;"
         << "}"
         << "";

  header << "EXTERN struct ryx_token* ryx_get_token(struct ryx_tree* node);";
  ccfile << "EXTERN struct ryx_token* ryx_get_token(struct ryx_tree* node) {"
         << "  if (node == NULL) {"
         << "    return NULL;"
         << "  } else {"
         << "    return node->shared_token->token;"
         << "  }"
         << "}"
         << "";

  header << "EXTERN struct ryx_tree* ryx_get_next_node(struct ryx_tree* node);";
  ccfile << "EXTERN struct ryx_tree* ryx_get_next_node(struct ryx_tree* node) {"
         << "  if (node == NULL) {"
         << "    return NULL;"
         << "  } else {"
         << "    return node->next_node;"
         << "  }"
         << "}"
         << "";

  header << "EXTERN struct ryx_tree* ryx_get_sub_node(struct ryx_tree* node);";
  ccfile << "EXTERN struct ryx_tree* ryx_get_sub_node(struct ryx_tree* node) {"
         << "  if (node == NULL) {"
         << "    return NULL;"
         << "  } else {"
         << "    return node->sub_node;"
         << "  }"
         << "}"
         << "";

  header << "// RYX interface end"
         << "";

  header << "#endif  // RYX_H_"
         << "";

  return;
}
