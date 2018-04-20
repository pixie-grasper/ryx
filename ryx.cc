// Truly RYX -- Check the grammar is in the LL(1) class.
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

#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define RESET   "\x1B[0m"
#define BOLD    "\x1B[1m"
#define RED     "\x1B[31m"
#define GREEN   "\x1B[32m"
#define YELLOW  "\x1B[33m"
#define BLUE    "\x1B[34m"
#define MAGENTA "\x1B[35m"
#define CYAN    "\x1B[36m"
#define WHITE   "\x1B[37m"

#define FATAL   "fatal error: "
#define ERROR   "      error: "
#define WARNING "    warning: "
#define NOTE    "       note: "
#define INDENT  "             "

static void put_error(void) {
  std::cout << BOLD RED ERROR RESET;
  return;
}

static void put_warning(void) {
  std::cout << BOLD MAGENTA WARNING RESET;
  return;
}

static void put_note(void) {
  std::cout << BOLD NOTE RESET;
  return;
}

static void put_indent(void) {
  std::cout << INDENT;
  return;
}

static void put_ok(void) {
  std::cout << WHITE "ok" RESET;
}

static void put_bad(void) {
  std::cout << BOLD RED "bad" RESET;
}

class context {
  using token_id = std::size_t;
  using rule_id = std::size_t;

  enum class token_kind {
    end_of_file = 0,
    begin_rule,
    end_of_body,
    invalid,
    input,
    syntax_list,
    syntax,
    body_list,
    body_list_rest,
    id_list,
    id,
    is,
    or_,
    end,
    tokenlist,
  };

  struct token {
    token_kind kind;
    token_id id;

    token() = default;
    explicit token(token_kind kind_) : kind(kind_), id(0) { return; }
    token(token_kind kind_, token_id id_) : kind(kind_), id(id_) { return; }
  };

  struct syntax_tree {
    token token;
    std::weak_ptr<syntax_tree> parent;
    std::vector<std::shared_ptr<syntax_tree>> subtree;

    syntax_tree() = default;
    explicit syntax_tree(const std::shared_ptr<syntax_tree>& parent_)
      : token(),
        parent(parent_),
        subtree() {
      return;
    }
  };

  using shared_syntax_tree = std::shared_ptr<syntax_tree>;

  struct working_memory {
    std::unordered_map<rule_id, std::pair<token_id, std::vector<token_id>>> rules;
    std::unordered_map<token_id, std::unordered_set<rule_id>> rules_of_nts;
    std::unordered_set<token_id> ts, nts;
    std::unordered_map<rule_id, std::unordered_set<token_id>> first;
    std::unordered_map<token_id, std::unordered_set<token_id>> follow;
    std::unordered_map<token_id, std::unordered_map<token_id, rule_id>> table;
  };

  using shared_working_memory = std::shared_ptr<working_memory>;

  std::unordered_map<std::string, token_id> token_to_id;
  std::unordered_map<token_id, std::string> id_to_token;
  shared_syntax_tree parsed_input;

  std::istream* is;
  bool verbose, quiet, table;
  bool parsed, checked, ll1p;
  int lr, ln;

  void put_linenumber(void) {
    std::cout << "line " << (std::max(lr, ln) + 1) << std::endl;
  }

  token_id get_id(const std::string& token_string) {
    auto&& it = token_to_id.find(token_string);
    if (it == token_to_id.end()) {
      token_id id = token_to_id.size();
      token_to_id[token_string] = id;
      id_to_token[id] = token_string;
      return id;
    } else {
      return it->second;
    }
  }

  char itoh(int x) {
    if (x < 10) {
      return '0' + static_cast<char>(x);
    } else {
      return 'A' + (static_cast<char>(x) - 10);
    }
  }

  void put_error_while_get_token(void) {
    std::cout << std::endl;
    put_linenumber();

    if (is == nullptr) {
      put_error();
      std::cout << "input stream has not set." << std::endl;
      return;
    } else {
      put_error();
      std::cout << "invalid character detected." << std::endl;

      put_note();
      std::cout << "next characters are ..." << std::endl;

      put_indent();
      for (int i = 0; i < 10; ++i) {
        if (i != 0) {
          std::cout << " ";
        }

        if (is->peek() == EOF) {
          std::cout << "(EOF)";
          break;
        } else {
          int ch = is->get();
          std::cout << "0x" << std::string{itoh((ch & 0xF0) >> 4), itoh(ch & 0x0F)};

          if (0x20 <= ch && ch <= 0x7E) {
            std::cout << "(" << std::string{static_cast<char>(ch)} << ")";
          } else {
            std::cout << "(.)";
          }
        }
      }

      if (is->peek() != EOF) {
        std::cout << " ...";
      }
      std::cout << std::endl;

      put_indent();
      std::cout << GREEN "^^^^^^^" RESET << std::endl;
    }

    return;
  }

  token get_token() {
    if (is == nullptr) {
      return token(token_kind::invalid);
    } else if (is->eof()) {
      return token(token_kind::end_of_file);
    }

    for (;;) {
      int ch = is->get();
      switch (ch) {
        case EOF:
          return token(token_kind::end_of_file);

        case ' ':
        case '\t':
          continue;

        case '\r':
          ++lr;
          continue;

        case '\n':
          ++ln;
          continue;

        case '=':
          return token(token_kind::is);

        case '|':
          return token(token_kind::or_);

        case ';':
          return token(token_kind::end);

        case '%':
          return token(token_kind::tokenlist);

        case '#':
          while (!(ch == EOF || ch == '\r' || ch == '\n')) {
            ch = is->get();
            switch (ch) {
              case '\r':
                ++lr;
                break;

              case '\n':
                ++ln;
                break;

              default:
                break;
            }
          }
          continue;

        default: {
          std::string token_string{};

          is->unget();
          while (ch == '_' || ('0' <= ch && ch <= '9') ||
                 ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z')) {
            token_string.push_back(static_cast<char>(is->get()));
            ch = is->peek();
          }
          if (token_string.size() == 0) {
            put_error_while_get_token();
            return token(token_kind::invalid);
          } else {
            return token(token_kind::id, get_id(token_string));
          }
        }
      }
    }
  }

  void put_error_while_parse(std::vector<token_kind>& stack, token t) {
    put_linenumber();
    put_error();
    std::cout << "invalid token sequence detected in the grammar file." << std::endl;

    put_note();
    std::cout << "symbols in the stack are ..." << std::endl;

    put_indent();
    bool first = true;
    for (int i = 0; i < 10; ++i) {
      if (first) {
        first = false;
      } else {
        std::cout << " ";
      }

      if (stack.empty()) {
        break;
      } else {
        switch (stack.back()) {
          // skip the internals
          case token_kind::begin_rule:
          case token_kind::end_of_body:
          case token_kind::invalid:
            first = true;
            i--;
            break;

          case token_kind::end_of_file:     std::cout << "$";               break;
          case token_kind::input:           std::cout << "input";           break;
          case token_kind::syntax_list:     std::cout << "syntax-list";     break;
          case token_kind::syntax:          std::cout << "syntax";          break;
          case token_kind::body_list:       std::cout << "body-list";       break;
          case token_kind::body_list_rest:  std::cout << "body-list-rest";  break;
          case token_kind::id_list:         std::cout << "id-list";         break;
          case token_kind::id:              std::cout << "ID";              break;
          case token_kind::is:              std::cout << "=";               break;
          case token_kind::or_:             std::cout << "|";               break;
          case token_kind::end:             std::cout << ";";               break;
          case token_kind::tokenlist:       std::cout << "%";               break;
        }
        stack.pop_back();
      }
    }
    if (!stack.empty()) {
      std::cout << " ...";
    }
    std::cout << std::endl;

    put_note();
    std::cout << "next tokens are ..." << std::endl;
    put_indent();
    for (int i = 0; i < 10; ++i) {
      if (i != 0) {
        std::cout << " ";
        t = get_token();
      }

      if (t.kind == token_kind::invalid) {
        break;
      } else {
        switch (t.kind) {
          case token_kind::id:          std::cout << id_to_token[t.id]; break;
          case token_kind::end_of_file: std::cout << "$"; break;
          case token_kind::is:          std::cout << "="; break;
          case token_kind::or_:         std::cout << "|"; break;
          case token_kind::end:         std::cout << ";"; break;
          case token_kind::tokenlist:   std::cout << "%"; break;
          default:                                        break;
        }
      }
    }
    if (t.kind != token_kind::end_of_file && t.kind != token_kind::invalid) {
      std::cout << " ...";
    }
    std::cout << std::endl;

    return;
  }

  shared_syntax_tree parse() {
    std::vector<token_kind> stack = {token_kind::begin_rule};
    shared_syntax_tree ret = std::make_shared<syntax_tree>();
    shared_syntax_tree node = ret;
    token t = get_token();
    bool end = false;

    while (!end) {
      switch (stack.back()) {
        case token_kind::begin_rule:
          stack.pop_back();
          stack.push_back(token_kind::end_of_body);
          stack.push_back(token_kind::end_of_file);
          stack.push_back(token_kind::input);
          node->token = token(token_kind::begin_rule);
          break;

        case token_kind::end_of_body:
          stack.pop_back();
          node = node->parent.lock();
          break;

        case token_kind::end_of_file:
          switch (t.kind) {
            case token_kind::end_of_file:
              end = true;
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::input:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::input);
          switch (t.kind) {
            case token_kind::id:
            case token_kind::tokenlist:
            case token_kind::end_of_file:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::syntax_list);
              node = node->subtree.back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::syntax_list:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::syntax_list);
          switch (t.kind) {
            case token_kind::id:
            case token_kind::tokenlist:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::syntax_list);
              stack.push_back(token_kind::syntax);
              node = node->subtree.back();
              break;

            case token_kind::end_of_file:
              stack.pop_back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::syntax:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::syntax);
          switch (t.kind) {
            case token_kind::id:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::end);
              stack.push_back(token_kind::body_list);
              stack.push_back(token_kind::is);
              stack.push_back(token_kind::id);
              node = node->subtree.back();
              break;

            case token_kind::tokenlist:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::end);
              stack.push_back(token_kind::id_list);
              stack.push_back(token_kind::tokenlist);
              node = node->subtree.back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::body_list:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::body_list);
          switch (t.kind) {
            case token_kind::id:
            case token_kind::or_:
            case token_kind::end:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::body_list_rest);
              stack.push_back(token_kind::id_list);
              node = node->subtree.back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::body_list_rest:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::body_list_rest);
          switch (t.kind) {
            case token_kind::or_:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::body_list);
              stack.push_back(token_kind::or_);
              node = node->subtree.back();
              break;

            case token_kind::id:
            case token_kind::end:
              stack.pop_back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::id_list:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::id_list);
          switch (t.kind) {
            case token_kind::id:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::id_list);
              stack.push_back(token_kind::id);
              node = node->subtree.back();
              break;

            case token_kind::or_:
            case token_kind::end:
              stack.pop_back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::invalid:
          put_error_while_parse(stack, t);
          ret = nullptr;
          end = true;
          break;

        default:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(t);
          if (stack.back() == t.kind) {
            stack.pop_back();
            t = get_token();
          } else {
            put_error_while_parse(stack, t);
            ret = nullptr;
            end = true;
          }
          break;
      }
    }

    parsed = true;
    return ret;
  }

  shared_working_memory rule_list_from_syntax_tree(const shared_syntax_tree& tree) {
    std::unordered_set<token_id> ts{}, nts{}, unknown{};
    shared_syntax_tree syntax_list = tree->subtree[0]->subtree[0];
    shared_working_memory ret = std::make_shared<working_memory>();
    bool errored = false;

    while (!syntax_list->subtree.empty() && !errored) {
      shared_syntax_tree syntax = syntax_list->subtree[0];
      syntax_list = syntax_list->subtree[1];
      if (syntax->subtree[0]->token.kind == token_kind::tokenlist) {
        shared_syntax_tree id_list = syntax->subtree[1];
        while (!id_list->subtree.empty() && !errored) {
          token_id id = id_list->subtree[0]->token.id;
          id_list = id_list->subtree[1];
          if (ts.find(id) != ts.end()) {
            if (!quiet) {
              put_warning();
              std::cout << "token '"
                        << id_to_token[id]
                        << "' is already registered as a terminate symbol."
                        << std::endl;
            }
          } else if (nts.find(id) != nts.end()) {
            put_error();
            std::cout << "token '"
                      << id_to_token[id]
                      << "' is already registered as a NON-TERMINATE symbol."
                      << std::endl;
            errored = true;
          } else {
            if (unknown.find(id) != unknown.end()) {
              unknown.erase(id);
            }
            ts.insert(id);
          }
        }
      } else {
        token_id id = syntax->subtree[0]->token.id;
        if (ts.find(id) != ts.end()) {
          put_error();
          std::cout << "token '"
                    << id_to_token[id]
                    << "' is already registered as a TERMINATE symbol."
                    << std::endl;
          errored = true;
        } else {
          if (nts.find(id) == nts.end()) {
            nts.insert(id);
          }
          if (unknown.find(id) != unknown.end()) {
            unknown.erase(id);
          }
          token_id head_id = id;
          shared_syntax_tree body_list = syntax->subtree[2];
          ret->rules_of_nts.insert(std::make_pair(head_id, std::unordered_set<rule_id>()));
          while (body_list != nullptr && !errored) {
            shared_syntax_tree id_list = body_list->subtree[0];
            shared_syntax_tree body_list_rest = body_list->subtree[1];
            if (body_list_rest->subtree.empty()) {
              body_list = nullptr;
            } else {
              body_list = body_list_rest->subtree[1];
            }
            std::vector<token_id> rule{};
            while (!id_list->subtree.empty() && !errored) {
              id = id_list->subtree[0]->token.id;
              if (ts.find(id) == ts.end() && nts.find(id) == nts.end()) {
                unknown.insert(id);
              }
              rule.push_back(id);
              id_list = id_list->subtree[1];
            }
            rule_id rule_id = ret->rules.size();
            ret->rules.insert(std::make_pair(rule_id,
                                             std::make_pair(head_id, std::move(rule))));
            ret->rules_of_nts[head_id].insert(rule_id);
          }
        }
      }
    }

    if (errored) {
      return nullptr;
    }

    if (!unknown.empty()) {
      if (!quiet) {
        put_warning();
        std::cout << "assumed they are terminate symbols." << std::endl;

        put_indent();
        bool first = true;
        for (auto&& it = unknown.begin(); it != unknown.end(); ++it) {
          if (first) {
            first = false;
          } else {
            std::cout << " ";
          }

          token_id id = *it;
          std::cout << id_to_token[id];
        }
        std::cout << std::endl;
      }

      for (auto&& it = unknown.begin(); it != unknown.end(); ++it) {
        ts.insert(*it);
      }
    }

    ret->ts = std::move(ts);
    ret->nts = std::move(nts);

    if (verbose) {
      std::cout << "rule-list:" << std::endl;
      for (rule_id rule_id = 0; rule_id < ret->rules.size(); ++rule_id) {
        std::cout << "  # rule " << static_cast<int>(rule_id) << std::endl;
        std::cout << "  " << id_to_token[ret->rules[rule_id].first] << " =";
        for (auto&& body = ret->rules[rule_id].second.begin();
                    body != ret->rules[rule_id].second.end();
                    ++body) {
          std::cout << " " << id_to_token[*body];
        }
        std::cout << ";" << std::endl << std::endl;
      }
    }

    return ret;
  }

  bool build_first_set(const shared_working_memory& work) {
    token_id eid = get_id("<epsilon>");
    std::unordered_map<rule_id, bool> complete_to_build{};
    for (auto&& it = work->rules.begin(); it != work->rules.end(); ++it) {
      rule_id rule_id = it->first;
      work->first.insert(std::make_pair(rule_id, std::unordered_set<token_id>()));
      complete_to_build[rule_id] = false;
    }

    bool updated = true;
    while (updated) {
      updated = false;
      for (auto&& it = work->rules.begin(); it != work->rules.end(); ++it) {
        rule_id target_rule_id = it->first;
        if (complete_to_build[target_rule_id]) {
          continue;
        }
        bool need_to_update = false;
        bool has_epsilon = true;
        for (std::size_t body_index = 0;
                         body_index < it->second.second.size() && has_epsilon;
                         ++body_index) {
          has_epsilon = false;
          token_id body_token_id = it->second.second[body_index];
          if (work->ts.find(body_token_id) == work->ts.end()) {
            for (auto&& rule = work->rules_of_nts[body_token_id].begin();
                        rule != work->rules_of_nts[body_token_id].end();
                        ++rule) {
              rule_id depending_rule_id = *rule;
              if (!complete_to_build[depending_rule_id]) {
                need_to_update = true;
              } else {
                for (auto&& first = work->first[depending_rule_id].begin();
                            first != work->first[depending_rule_id].end();
                            ++first) {
                  token_id first_id = *first;
                  if (*first == eid) {
                    has_epsilon = true;
                  } else if (work->first[target_rule_id].find(first_id) ==
                             work->first[target_rule_id].end()) {
                    updated = true;
                    work->first[target_rule_id].insert(first_id);
                  }
                }
              }
            }
          } else {
            if (work->first[target_rule_id].find(body_token_id) ==
                work->first[target_rule_id].end()) {
              updated = true;
              work->first[target_rule_id].insert(body_token_id);
            }
          }
        }
        if (has_epsilon) {
          if (work->first[target_rule_id].find(eid) == work->first[target_rule_id].end()) {
            updated = true;
            work->first[target_rule_id].insert(eid);
          }
        }
        if (!need_to_update) {
          complete_to_build[target_rule_id] = true;
        }
      }
    }

    if (verbose) {
      std::cout << "first:" << std::endl;
      for (rule_id rule_id = 0; rule_id < work->rules.size(); ++rule_id) {
        std::cout << "  rule " << static_cast<int>(rule_id) << ": ";
        std::cout << id_to_token[work->rules[rule_id].first] << " ->";
        for (auto&& first = work->first[rule_id].begin();
                    first != work->first[rule_id].end();
                    ++first) {
          std::cout << " " << id_to_token[*first];
        }
        if (!complete_to_build[rule_id]) {
          std::cout << " : ";
          put_bad();
          std::cout << std::endl;
        } else {
          std::cout << " : ";
          put_ok();
          std::cout << std::endl;
        }
      }
      std::cout << std::endl;
    }

    for (auto&& it = work->rules.begin(); it != work->rules.end(); ++it) {
      if (!complete_to_build[it->first]) {
        return false;
      }
    }

    return true;
  }

  bool build_follow_set(const shared_working_memory& work) {
    token_id eid = get_id("<epsilon>");
    token_id did = get_id("$");

    std::unordered_map<token_id, bool> complete_to_build{};
    std::unordered_map<token_id, bool> not_need_complete{};
    for (auto&& it = work->nts.begin(); it != work->nts.end(); ++it) {
      token_id target_token_id = *it;
      work->follow.insert(std::make_pair(target_token_id, std::unordered_set<token_id>()));
      complete_to_build[target_token_id] = false;
      bool has_epsilon = false;
      for (auto&& rule = work->rules_of_nts[target_token_id].begin();
                  rule != work->rules_of_nts[target_token_id].end();
                  ++rule) {
        rule_id rule_id = *rule;
        if (work->first[rule_id].find(eid) != work->first[rule_id].end()) {
          has_epsilon = true;
          break;
        }
      }
      if (has_epsilon) {
        not_need_complete[target_token_id] = true;
      } else {
        not_need_complete[target_token_id] = false;
      }
    }

    token_id start_symbol_id = get_id("input");
    work->follow[start_symbol_id].insert(did);

    bool updated = true;
    while (updated) {
      updated = false;
      for (auto&& it = work->nts.begin(); it != work->nts.end(); ++it) {
        token_id target_token_id = *it;
        if (complete_to_build[target_token_id]) {
          continue;
        }
        bool need_to_update = false;
        for (auto&& rule = work->rules.begin(); rule != work->rules.end(); ++rule) {
          token_id depending_token_id = rule->second.first;
          for (std::size_t body_index = 0;
                           body_index < rule->second.second.size();
                           ++body_index) {
            if (rule->second.second[body_index] == target_token_id) {
              std::unordered_set<token_id> follow_first{};
              bool has_epsilon = true;
              for (std::size_t follow_index = body_index + 1;
                               follow_index < rule->second.second.size() && has_epsilon;
                               ++follow_index) {
                has_epsilon = false;
                token_id follow_token_id = rule->second.second[follow_index];
                if (work->ts.find(follow_token_id) == work->ts.end()) {
                  for (auto&& follow_rule = work->rules_of_nts[follow_token_id].begin();
                              follow_rule != work->rules_of_nts[follow_token_id].end();
                              ++follow_rule) {
                    rule_id follow_rule_id = *follow_rule;
                    for (auto&& first = work->first[follow_rule_id].begin();
                                first != work->first[follow_rule_id].end();
                                ++first) {
                      if (*first == eid) {
                        has_epsilon = true;
                      } else if (follow_first.find(*first) == follow_first.end()) {
                        follow_first.insert(*first);
                      }
                    }
                  }
                } else if (follow_first.find(follow_token_id) == follow_first.end()) {
                  follow_first.insert(follow_token_id);
                }
              }
              for (auto&& first = follow_first.begin(); first != follow_first.end(); ++first) {
                if (work->follow[target_token_id].find(*first) ==
                    work->follow[target_token_id].end()) {
                  updated = true;
                  work->follow[target_token_id].insert(*first);
                }
              }
              if (has_epsilon && depending_token_id != target_token_id) {
                if (!complete_to_build[depending_token_id]) {
                  need_to_update = true;
                } else {
                  for (auto&& follow = work->follow[depending_token_id].begin();
                              follow != work->follow[depending_token_id].end();
                              ++follow) {
                    if (work->follow[target_token_id].find(*follow) ==
                        work->follow[target_token_id].end()) {
                      updated = true;
                      work->follow[target_token_id].insert(*follow);
                    }
                  }
                }
              }
            }
          }
        }
        if (!need_to_update) {
          complete_to_build[target_token_id] = true;
        }
      }
    }

    if (verbose) {
      std::cout << "follow:" << std::endl;
      std::unordered_set<token_id> nts{};
      for (rule_id rule_id = 0; rule_id < work->rules.size(); ++rule_id) {
        token_id id = work->rules[rule_id].first;
        if (nts.find(id) != nts.end()) {
          continue;
        }
        nts.insert(id);
        std::cout << "  " << id_to_token[id] << " ->";
        for (auto&& follow = work->follow[id].begin();
                    follow != work->follow[id].end();
                    ++follow) {
          std::cout << " " << id_to_token[*follow];
        }
        if (!complete_to_build[id] && !not_need_complete[id]) {
          std::cout << " : ";
          put_bad();
          std::cout << std::endl;
        } else {
          std::cout << " : ";
          put_ok();
          std::cout << std::endl;
        }
      }
      std::cout << std::endl;
    }

    for (auto&& it = work->nts.begin(); it != work->nts.end(); ++it) {
      if (!complete_to_build[*it]) {
        return false;
      }
    }

    return true;
  }

  bool build_table(const std::shared_ptr<working_memory>& work) {
    token_id eid = get_id("<epsilon>");
    token_id did = get_id("$");
    rule_id empty_rule_id = work->rules.size();
    rule_id booked_rule_id = work->rules.size() + 1;

    for (auto&& stack_token = work->nts.begin();
                stack_token != work->nts.end();
                ++stack_token) {
      token_id stack_token_id = *stack_token;
      work->table.insert(std::make_pair(stack_token_id,
                                        std::unordered_map<token_id, rule_id>()));
      for (auto&& input_token = work->ts.begin();
                  input_token != work->ts.end();
                  ++input_token) {
        token_id input_token_id = *input_token;
        work->table[stack_token_id][input_token_id] = empty_rule_id;
      }
      work->table[stack_token_id][did] = empty_rule_id;
    }

    bool booked = false;
    for (auto&& it = work->first.begin(); it != work->first.end(); ++it) {
      rule_id rule_id = it->first;
      token_id stack_token_id = work->rules[rule_id].first;

      if (work->ts.find(stack_token_id) == work->ts.end()) {
        bool has_epsilon = false;
        for (auto&& input_token = it->second.begin();
                    input_token != it->second.end();
                    ++input_token) {
          token_id input_token_id = *input_token;
          if (input_token_id == eid) {
            has_epsilon = true;
          } else if (work->table[stack_token_id][input_token_id] == empty_rule_id) {
            work->table[stack_token_id][input_token_id] = rule_id;
          } else {
            booked = true;
            work->table[stack_token_id][input_token_id] = booked_rule_id;
          }
        }

        if (has_epsilon) {
          for (auto&& input_token = work->follow[stack_token_id].begin();
                      input_token != work->follow[stack_token_id].end();
                      ++input_token) {
            token_id input_token_id = *input_token;
            if (work->table[stack_token_id][input_token_id] == empty_rule_id) {
              work->table[stack_token_id][input_token_id] = rule_id;
            } else if (work->table[stack_token_id][input_token_id] != rule_id) {
              booked = true;
              work->table[stack_token_id][input_token_id] = booked_rule_id;
            }
          }
        }
      }
    }

    if (verbose || table) {
      std::vector<std::size_t> column_width{};
      std::vector<token_id> column_item{};
      column_item.push_back(get_id(""));
      column_width.push_back(0);
      for (auto&& stack_token = work->nts.begin();
                  stack_token != work->nts.end();
                  ++stack_token) {
        token_id stack_token_id = *stack_token;
        if (column_width.back() < id_to_token[stack_token_id].size()) {
          column_width.back() = id_to_token[stack_token_id].size();
        }
      }

      work->ts.insert(did);
      for (auto&& input_token = work->ts.begin();
                  input_token != work->ts.end();
                  ++input_token) {
        token_id input_token_id = *input_token;
        column_item.push_back(input_token_id);
        column_width.push_back(id_to_token[input_token_id].size());
        for (auto&& stack_token = work->nts.begin();
                    stack_token != work->nts.end();
                    ++stack_token) {
          token_id stack_token_id = *stack_token;
          rule_id rule_id = work->table[stack_token_id][input_token_id];
          std::size_t num_width = std::to_string(rule_id).size();
          if (rule_id == empty_rule_id || rule_id == booked_rule_id) {
            num_width = 1;
          }
          if (column_width.back() < num_width) {
            column_width.back() = num_width;
          }
        }
      }

      std::cout << "table:" << std::endl;
      std::cout << "  ";
      for (std::size_t col = 0; col < column_item.size(); ++col) {
        if (col != 0) {
          std::cout << " ";
        }
        std::string text = id_to_token[column_item[col]];
        std::size_t cw = text.size();
        std::size_t lw = (column_width[col] - cw) / 2;
        std::size_t rw = column_width[col] - lw - cw;
        std::cout << std::string(lw, ' ') << text << std::string(rw, ' ');
      }
      std::cout << std::endl;
      std::unordered_set<token_id> nts{};
      for (rule_id rid = 0; rid < work->rules.size(); ++rid) {
        token_id stack_token_id = work->rules[rid].first;
        if (nts.find(stack_token_id) != nts.end()) {
          continue;
        }
        nts.insert(stack_token_id);
        std::cout << "  ";
        for (std::size_t col = 0; col < column_item.size(); ++col) {
          if (col != 0) {
            std::cout << " ";
          }
          std::string text{};
          std::size_t cw, lw;
          if (col == 0) {
            text = id_to_token[stack_token_id];
            cw = text.size();
            lw = 0;
          } else {
            token_id input_token_id = column_item[col];
            rule_id rule_id = work->table[stack_token_id][input_token_id];
            if (rule_id == empty_rule_id) {
              text = "-";
              cw = 1;
            } else if (rule_id == booked_rule_id) {
              text = BOLD YELLOW "*" RESET;
              cw = 1;
            } else {
              text = std::to_string(rule_id);
              cw = text.size();
            }
            lw = (column_width[col] - cw + (cw & 1)) / 2;
          }
          std::size_t rw = column_width[col] - lw - cw;
          std::cout << std::string(lw, ' ') << text << std::string(rw, ' ');
        }
        std::cout << std::endl;
      }
    }

    if (booked) {
      return false;
    } else {
      return true;
    }
  }

  bool check(void) {
    if (!parsed) {
      parsed_input = parse();
    }

    if (parsed_input == nullptr) {
      checked = true;
      return false;
    }

    std::shared_ptr<working_memory> work = rule_list_from_syntax_tree(parsed_input);
    if (work == nullptr) {
      checked = true;
      return false;
    }

    if (!build_first_set(work)) {
      put_error();
      std::cout << "building FIRST set failed." << std::endl;
      checked = true;
      return false;
    }

    if (!build_follow_set(work)) {
      put_error();
      std::cout << "building FOLLOW set failed." << std::endl;
      checked = true;
      return false;
    }

    if (!build_table(work)) {
      put_error();
      std::cout << "building TABLE failed." << std::endl;
      checked = true;
      return false;
    }

    checked = true;
    return true;
  }

 public:
  context() = default;

  void clear(void) {
    token_to_id.clear();
    id_to_token.clear();
    is = nullptr;
    parsed_input = nullptr;
    parsed = false;
    checked = false;
    verbose = false;
    quiet = false;
    table = false;
    ll1p = false;
    lr = 0;
    ln = 0;
    return;
  }

  void set_input(std::istream& is_) {
    clear();
    is = &is_;
    return;
  }

  bool is_ll1(void) {
    if (!checked) {
      ll1p = check();
    }
    return ll1p;
  }

  void set_verbose(bool x = true) {
    verbose = x;
    if (verbose) {
      quiet = false;
    }
    return;
  }

  void set_quiet(bool x = true) {
    quiet = x;
    if (quiet) {
      verbose = false;
    }
    return;
  }

  void set_table(bool x = true) {
    table = x;
    return;
  }
};

int main(int argc, char** argv) {
  auto c = std::make_unique<context>();
  std::ifstream file{};

  const char* filename = nullptr;
  bool verbose = false;
  bool quiet = false;
  bool table = false;
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == 'v') {
        verbose = true;
        quiet = false;
      } else if (argv[i][1] == 'q') {
        verbose = false;
        quiet = true;
      } else if (argv[i][1] == 't') {
        table = true;
      }
    } else {
      filename = argv[i];
    }
  }

  if (filename == nullptr) {
    c->set_input(std::cin);
  } else {
    file.open(filename, std::ifstream::in);
    if (!file.is_open()) {
      std::cout << BOLD RED FATAL RESET "failed to open '" << filename << "'" << std::endl;
      return 1;
    }
    c->set_input(file);
  }

  if (verbose) {
    c->set_verbose();
  } else if (quiet) {
    c->set_quiet();
  }

  if (table) {
    c->set_table();
  }

  if (c->is_ll1()) {
    return 0;
  } else {
    return 1;
  }
}
