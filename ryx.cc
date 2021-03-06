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

#include "ryx.h"
#include "codegen.h"

#include <array>
#include <fstream>
#include <list>
#include <iomanip>
#include <memory>
#include <set>

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

char itoh(int x) {
  if (x < 10) {
    return '0' + static_cast<char>(x);
  } else {
    return 'A' + (static_cast<char>(x) - 10);
  }
}

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
  enum class token_kind {
    /* internal symbols */
    end_of_file = 0,
    begin_rule,
    end_of_body,
    invalid,

    /* non-terminate symbols */
    // input = syntax
    //       ;
    input,

    // syntax = syntax_ syntax
    //        |
    //        ;
    syntax,

    // syntax_ = id comma_ eq body_list semicolon
    //         | percent id_ semicolon
    //         ;
    syntax_,

    // body_list = body_internal body_list_
    //           ;
    body_list,

    // body_list_ = bar body_internal body_list_
    //            |
    //            ;
    body_list_,

    // body_internal = comma_ body body_internal
    //               |
    //               ;
    body_internal,

    // body = '(' body_list ')' body_opt
    //      | id_or_regexp body_opt
    //      | '@' number_
    //      ;
    body,

    // body_opt = body_opt_ body_opt
    //          |
    //          ;
    body_opt,

    // body_opt_ = '?'
    //           | '+'
    //           | '*'
    //           | '{' range '}'
    //           ;
    body_opt_,

    // range = NUM range_
    //       ;
    range,

    // range_ = ',' NUM
    //        |
    //        ;
    range_,

    // id_ = id id_
    //     |
    //     ;
    id_,

    // comma_ = commma
    //        |
    //        ;
    comma_,

    // id_or_regexp = ID
    //              | REGEXP
    //              ;
    id_or_regexp,

    // number_ = NUM
    //         |
    //         ;
    number_,

    /* terminate symbols */
    id,
    num,
    regexp,
    eq,
    bar,
    semicolon,
    percent,
    lparen,
    rparen,
    lcurl,
    rcurl,
    question,
    plus,
    star,
    comma,
    period,
    at,
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

  struct continuation {
    token_id head_id, base_id;
    std::shared_ptr<std::vector<token_id>> rule;
    shared_syntax_tree body_internal, body_list_;

    continuation() = default;
  };

  using shared_continuation = std::shared_ptr<continuation>;

  struct working_memory {
    rules_type rules;
    std::unordered_map<token_id, std::unordered_set<rule_id>> rules_of_nts;
    token_set_type ts, nts;
    std::unordered_map<rule_id, std::unordered_set<token_id>> first;
    std::unordered_map<token_id, std::unordered_set<token_id>> follow;
    table_type table;
  };

  using shared_working_memory = std::shared_ptr<working_memory>;

  token_to_id_type token_to_id;
  id_to_token_type id_to_token;
  id_to_token_type id_to_regexp_body;
  shared_syntax_tree parsed_input;
  shared_working_memory work;

  std::istream* is;
  bool verbose, quiet, table, sure_partial_book, width_limited;
  bool parsed, checked, ll1p;
  int lr, ln;
  int genid;
  char current_quote;

  std::ostream *header, *ccfile;

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

  token_id get_id_regexp(const std::string& token_string) {
    token_id id = get_id("/" + token_string + "/");
    id_to_regexp_body[id] = token_string;
    return id;
  }

  token_id gen_id(const std::string& token_string) {
    ++genid;
    return get_id(token_string + "[" + std::to_string(genid) + "]");
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
      if (current_quote != '\0') {
        if (ch == EOF) {
          return token(token_kind::invalid);
        } else if (ch == current_quote) {
          current_quote = '\0';
          continue;
        } else {
          std::string token_string{};
          if (0x20 <= ch && ch <= 0x7E && ch != '\\') {
            token_string.push_back('\'');
            token_string.push_back(static_cast<char>(ch));
            token_string.push_back('\'');
          } else if (ch == '\\') {
            ch = is->get();
            switch (ch) {
              case 'n':
                token_string = "0x0A";
                break;

              case 'r':
                token_string = "0x0D";
                break;

              case 's':
                token_string = "' '";
                break;

              case 't':
                token_string = "0x09";
                break;

              default:
                put_error_while_get_token();
                return token(token_kind::invalid);
            }
          } else {
            token_string = "0x";
            token_string.push_back(itoh((ch & 0xF0) >> 4));
            token_string.push_back(itoh(ch & 0x0F));
          }
          return token(token_kind::id, get_id(std::move(token_string)));
        }
      }
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

        case '=':
          return token(token_kind::eq);

        case '|':
          return token(token_kind::bar);

        case ';':
          return token(token_kind::semicolon);

        case '%':
          return token(token_kind::percent);

        case '(':
          return token(token_kind::lparen);

        case ')':
          return token(token_kind::rparen);

        case '{':
          return token(token_kind::lcurl);

        case '}':
          return token(token_kind::rcurl);

        case '?':
          return token(token_kind::question);

        case '+':
          return token(token_kind::plus);

        case '*':
          return token(token_kind::star);

        case ',':
          return token(token_kind::comma);

        case '.':
          return token(token_kind::period);

        case '@':
          return token(token_kind::at);

        case '/': {
          std::string token_string{};

          ch = is->get();
          while (ch != EOF && ch != '/') {
            token_string.push_back(static_cast<char>(ch));
            if (ch == '\\') {
              ch = is->get();
              if (ch == EOF) {
                put_error_while_get_token();
                return token(token_kind::invalid);
              }
              token_string.push_back(static_cast<char>(ch));
            } else if (ch == '[') {
              ch = is->get();
              if (ch == EOF) {
                put_error_while_get_token();
                return token(token_kind::invalid);
              } else {
                token_string.push_back(static_cast<char>(ch));
                if (ch == '\\' || ch == '^') {
                  ch = is->get();
                  if (ch == EOF) {
                    put_error_while_get_token();
                    return token(token_kind::invalid);
                  }
                  token_string.push_back(static_cast<char>(ch));
                }
              }
              ch = is->get();
              while (ch != EOF && ch != ']') {
                token_string.push_back(static_cast<char>(ch));
                if (ch == '\\') {
                  ch = is->get();
                  if (ch == EOF) {
                    put_error_while_get_token();
                    return token(token_kind::invalid);
                  }
                  token_string.push_back(static_cast<char>(ch));
                }
                ch = is->get();
              }
              if (ch == EOF) {
                put_error_while_get_token();
                return token(token_kind::invalid);
              }
              token_string.push_back(static_cast<char>(ch));
            }
            ch = is->get();
          }
          if (ch == EOF) {
            put_error_while_get_token();
            return token(token_kind::invalid);
          } else if (token_string.size() == 0) {
            continue;
          } else {
            return token(token_kind::regexp, get_id_regexp(token_string));
          }
        }

        case '\'':
        case '"':
          current_quote = static_cast<char>(ch);
          continue;

        case ':': {
          std::string token_string{};
          bool number = true;
          token_string.push_back(static_cast<char>(is->get()));

          ch = is->peek();
          while (ch == '_' || ('0' <= ch && ch <= '9') ||
                 ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z')) {
            token_string.push_back(static_cast<char>(is->get()));
            if (number) {
              if (!('0' <= ch && ch <= '9')) {
                number = false;
              } else if (token_string[0] == '0' && token_string.size() != 1) {
                number = false;
              }
            }
            ch = is->peek();
          }
          if (ch == ':') {
            token_string.push_back(static_cast<char>(is->get()));
          } else {
            put_error_while_get_token();
            return token(token_kind::invalid);
          }

          if (token_string.size() == 0) {
            put_error_while_get_token();
            return token(token_kind::invalid);
          } else if (number) {
            return token(token_kind::num, get_id(token_string));
          } else {
            return token(token_kind::id, get_id(token_string));
          }
        }

        default: {
          std::string token_string{};
          bool number = true;

          is->unget();
          while (ch == '_' || ('0' <= ch && ch <= '9') ||
                 ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z')) {
            token_string.push_back(static_cast<char>(is->get()));
            if (number) {
              if (!('0' <= ch && ch <= '9')) {
                number = false;
              } else if (token_string[0] == '0' && token_string.size() != 1) {
                number = false;
              }
            }
            ch = is->peek();
          }

          if (token_string.size() == 0) {
            put_error_while_get_token();
            return token(token_kind::invalid);
          } else if (number) {
            return token(token_kind::num, get_id(token_string));
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

          case token_kind::end_of_file:     std::cout << "$";                 break;
          case token_kind::input:           std::cout << "input";             break;
          case token_kind::syntax:          std::cout << "syntax";            break;
          case token_kind::syntax_:         std::cout << "syntax~";           break;
          case token_kind::body_list:       std::cout << "body-list";         break;
          case token_kind::body_list_:      std::cout << "body-list~";        break;
          case token_kind::body_internal:   std::cout << "body-internal";     break;
          case token_kind::body:            std::cout << "body";              break;
          case token_kind::body_opt:        std::cout << "body-opt";          break;
          case token_kind::body_opt_:       std::cout << "body-opt~";         break;
          case token_kind::range:           std::cout << "range";             break;
          case token_kind::range_:          std::cout << "range~";            break;
          case token_kind::id_:             std::cout << "id~";               break;
          case token_kind::comma_:          std::cout << "comma~";            break;
          case token_kind::id_or_regexp:    std::cout << "id-or-regexp";      break;
          case token_kind::number_:         std::cout << "number~";           break;
          case token_kind::id:              std::cout << "ID";                break;
          case token_kind::regexp:          std::cout << "REGEXP";            break;
          case token_kind::num:             std::cout << "NUM";               break;
          case token_kind::eq:              std::cout << "=";                 break;
          case token_kind::bar:             std::cout << "|";                 break;
          case token_kind::semicolon:       std::cout << ";";                 break;
          case token_kind::percent:         std::cout << "%";                 break;
          case token_kind::lparen:          std::cout << "(";                 break;
          case token_kind::rparen:          std::cout << ")";                 break;
          case token_kind::lcurl:           std::cout << "{";                 break;
          case token_kind::rcurl:           std::cout << "}";                 break;
          case token_kind::question:        std::cout << "?";                 break;
          case token_kind::plus:            std::cout << "+";                 break;
          case token_kind::star:            std::cout << "*";                 break;
          case token_kind::comma:           std::cout << ",";                 break;
          case token_kind::period:          std::cout << ".";                 break;
          case token_kind::at:              std::cout << "@";                 break;
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
          case token_kind::num:         std::cout << id_to_token[t.id]; break;
          case token_kind::regexp:      std::cout << id_to_token[t.id]; break;
          case token_kind::end_of_file: std::cout << "$"; break;
          case token_kind::eq:          std::cout << "="; break;
          case token_kind::bar:         std::cout << "|"; break;
          case token_kind::semicolon:   std::cout << ";"; break;
          case token_kind::percent:     std::cout << "%"; break;
          case token_kind::lparen:      std::cout << "("; break;
          case token_kind::rparen:      std::cout << ")"; break;
          case token_kind::lcurl:       std::cout << "{"; break;
          case token_kind::rcurl:       std::cout << "}"; break;
          case token_kind::question:    std::cout << "?"; break;
          case token_kind::plus:        std::cout << "+"; break;
          case token_kind::star:        std::cout << "*"; break;
          case token_kind::comma:       std::cout << ","; break;
          case token_kind::period:      std::cout << "."; break;
          case token_kind::at:          std::cout << "@"; break;
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
            case token_kind::percent:
            case token_kind::end_of_file:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::syntax);
              node = node->subtree.back();
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
            case token_kind::percent:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::syntax);
              stack.push_back(token_kind::syntax_);
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

        case token_kind::syntax_:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::syntax_);
          switch (t.kind) {
            case token_kind::id:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::semicolon);
              stack.push_back(token_kind::body_list);
              stack.push_back(token_kind::eq);
              stack.push_back(token_kind::comma_);
              stack.push_back(token_kind::id);
              node = node->subtree.back();
              break;

            case token_kind::percent:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::semicolon);
              stack.push_back(token_kind::id_);
              stack.push_back(token_kind::percent);
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
            case token_kind::regexp:
            case token_kind::semicolon:
            case token_kind::bar:
            case token_kind::lparen:
            case token_kind::rparen:
            case token_kind::comma:
            case token_kind::at:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::body_list_);
              stack.push_back(token_kind::body_internal);
              node = node->subtree.back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::body_list_:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::body_list_);
          switch (t.kind) {
            case token_kind::bar:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::body_list_);
              stack.push_back(token_kind::body_internal);
              stack.push_back(token_kind::bar);
              node = node->subtree.back();
              break;

            case token_kind::semicolon:
            case token_kind::rparen:
              stack.pop_back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::body_internal:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::body_internal);
          switch (t.kind) {
            case token_kind::id:
            case token_kind::regexp:
            case token_kind::lparen:
            case token_kind::comma:
            case token_kind::at:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::body_internal);
              stack.push_back(token_kind::body);
              stack.push_back(token_kind::comma_);
              node = node->subtree.back();
              break;

            case token_kind::bar:
            case token_kind::semicolon:
            case token_kind::rparen:
              stack.pop_back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::body:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::body);
          switch (t.kind) {
            case token_kind::id:
            case token_kind::regexp:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::body_opt);
              stack.push_back(token_kind::id_or_regexp);
              node = node->subtree.back();
              break;

            case token_kind::lparen:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::body_opt);
              stack.push_back(token_kind::rparen);
              stack.push_back(token_kind::body_list);
              stack.push_back(token_kind::lparen);
              node = node->subtree.back();
              break;

            case token_kind::at:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::number_);
              stack.push_back(token_kind::at);
              node = node->subtree.back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::body_opt:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::body_opt);
          switch (t.kind) {
            case token_kind::question:
            case token_kind::plus:
            case token_kind::star:
            case token_kind::lcurl:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::body_opt);
              stack.push_back(token_kind::body_opt_);
              node = node->subtree.back();
              break;

            case token_kind::id:
            case token_kind::regexp:
            case token_kind::semicolon:
            case token_kind::lparen:
            case token_kind::rparen:
            case token_kind::bar:
            case token_kind::comma:
            case token_kind::at:
              stack.pop_back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::body_opt_:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::body_opt_);
          switch (t.kind) {
            case token_kind::question:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::question);
              node = node->subtree.back();
              break;

            case token_kind::plus:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::plus);
              node = node->subtree.back();
              break;

            case token_kind::star:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::star);
              node = node->subtree.back();
              break;

            case token_kind::lcurl:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::rcurl);
              stack.push_back(token_kind::range);
              stack.push_back(token_kind::lcurl);
              node = node->subtree.back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::range:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::range);
          switch (t.kind) {
            case token_kind::num:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::range_);
              stack.push_back(token_kind::num);
              node = node->subtree.back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::range_:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::range_);
          switch (t.kind) {
            case token_kind::rcurl:
              stack.pop_back();
              break;

            case token_kind::comma:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::num);
              stack.push_back(token_kind::comma);
              node = node->subtree.back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::id_:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::id_);
          switch (t.kind) {
            case token_kind::id:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::id_);
              stack.push_back(token_kind::id);
              node = node->subtree.back();
              break;

            case token_kind::semicolon:
              stack.pop_back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::comma_:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::comma_);
          switch (t.kind) {
            case token_kind::comma:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::comma);
              node = node->subtree.back();
              break;

            case token_kind::id:
            case token_kind::regexp:
            case token_kind::lparen:
            case token_kind::eq:
            case token_kind::at:
              stack.pop_back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::id_or_regexp:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::id_or_regexp);
          switch (t.kind) {
            case token_kind::id:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::id);
              node = node->subtree.back();
              break;

            case token_kind::regexp:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::regexp);
              node = node->subtree.back();
              break;

            default:
              put_error_while_parse(stack, t);
              ret = nullptr;
              end = true;
              break;
          }
          break;

        case token_kind::number_:
          node->subtree.push_back(std::make_shared<syntax_tree>(node));
          node->subtree.back()->token = token(token_kind::number_);
          switch (t.kind) {
            case token_kind::id:
            case token_kind::regexp:
            case token_kind::semicolon:
            case token_kind::lparen:
            case token_kind::rparen:
            case token_kind::bar:
            case token_kind::comma:
            case token_kind::at:
              stack.pop_back();
              break;

            case token_kind::num:
              stack.pop_back();
              stack.push_back(token_kind::end_of_body);
              stack.push_back(token_kind::num);
              node = node->subtree.back();
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

  void put_error_while_parse_regexp(const std::string& regexp) {
    put_error();
    std::cout << "invalid regexp /" << regexp << "/" << std::endl;
    return;
  }

  void add_rule(const shared_working_memory& current_work,
                token_id head_id,
                std::vector<token_id>&& rule) {
    rule_id rule_id = current_work->rules.size();
    current_work->rules.insert(std::make_pair(rule_id,
                                              std::make_pair(head_id, std::move(rule))));
    current_work->rules_of_nts[head_id].insert(rule_id);
    return;
  }

  std::vector<std::shared_ptr<std::string>> regexp_split(const std::string& regexp) {
    bool errored = false;
    std::vector<std::shared_ptr<std::string>> regexp_tokens{};
    std::size_t i = 0;
    while (i < regexp.size() && !errored) {
      std::shared_ptr<std::string> regexp_token = std::make_shared<std::string>();
      if (regexp[i] == '[') {
        regexp_token->push_back(regexp[i]);
        ++i;
        if (regexp[i] == '^') {
          regexp_token->push_back(regexp[i]);
          ++i;
        }
        if (regexp[i] == ']') {
          regexp_token->push_back(regexp[i]);
          ++i;
        }
        errored = true;
        while (i < regexp.size()) {
          if (regexp[i] == '\\') {
            regexp_token->push_back(regexp[i]);
            ++i;
            if (!(i < regexp.size())) {
              break;
            }
          } else if (regexp[i] == ']') {
            regexp_token->push_back(regexp[i]);
            ++i;
            errored = false;
            break;
          }
          regexp_token->push_back(regexp[i]);
          ++i;
        }
      } else if (regexp[i] == '\\') {
        regexp_token->push_back(regexp[i]);
        ++i;
        regexp_token->push_back(regexp[i]);
        ++i;
      } else {
        regexp_token->push_back(regexp[i]);
        ++i;
      }
      regexp_tokens.emplace_back(std::move(regexp_token));
    }

    return regexp_tokens;
  }

  shared_working_memory rule_list_from_syntax_tree(const shared_syntax_tree& tree) {
    shared_syntax_tree syntax = tree->subtree[0]->subtree[0];
    shared_working_memory ret = std::make_shared<working_memory>();
    bool errored = false;

    // split rule-definitions and terminate-symbol-definitions
    std::vector<shared_syntax_tree> define_rule{};
    std::vector<shared_syntax_tree> define_ts{};
    while (!syntax->subtree.empty()) {
      shared_syntax_tree syntax_ = syntax->subtree[0];
      syntax = syntax->subtree[1];
      if (syntax_->subtree[0]->token.kind == token_kind::id) {
        define_rule.emplace_back(std::move(syntax_));
      } else {
        define_ts.emplace_back(std::move(syntax_));
      }
    }

    // register symbols
    std::unordered_set<token_id> ts{}, nts{}, unknown{};
    for (std::size_t i = 0; i < define_rule.size(); ++i) {
      token_id head_id = define_rule[i]->subtree[0]->token.id;
      if (nts.find(head_id) == nts.end()) {
        nts.insert(head_id);
      }
    }
    for (std::size_t i = 0; i < define_ts.size(); ++i) {
      shared_syntax_tree id_ = define_ts[i]->subtree[1];
      while (!id_->subtree.empty()) {
        token_id ts_id = id_->subtree[0]->token.id;
        id_ = id_->subtree[1];
        if (nts.find(ts_id) != nts.end()) {
          put_error();
          std::cout << "token '"
                    << id_to_token[ts_id]
                    << "' is already registered as a NON-TERMINATE symbol."
                    << std::endl;
          errored = true;
        } else if (ts.find(ts_id) != ts.end()) {
          if (!quiet) {
            put_warning();
            std::cout << "token '"
                      << id_to_token[ts_id]
                      << "' is already registered as a terminate symbol."
                      << std::endl;
          }
        } else {
          ts.insert(ts_id);
        }
      }
    }
    if (errored) {
      return nullptr;
    }

    // add extra rule
    token_id start_symbol_id = get_id("^");
    if (nts.find(start_symbol_id) == nts.end()) {
      nts.insert(start_symbol_id);
      std::vector<token_id> rule{};
      rule.push_back(get_id("input"));
      add_rule(ret, start_symbol_id, std::move(rule));
    }
    token_id space_token_id = get_id(":ws:");
    if (nts.find(space_token_id) == nts.end()) {
      nts.insert(space_token_id);
      std::vector<token_id> rule{};
      token_id id;
      id = get_id("' '");
      if (ts.find(id) == ts.end()) {
        ts.insert(id);
      }
      rule.push_back(id);
      add_rule(ret, space_token_id, std::move(rule));
      rule.clear();
      id = get_id("0x09");
      if (ts.find(id) == ts.end()) {
        ts.insert(id);
      }
      rule.push_back(id);
      add_rule(ret, space_token_id, std::move(rule));
      rule.clear();
      id = get_id("0x0A");
      if (ts.find(id) == ts.end()) {
        ts.insert(id);
      }
      rule.push_back(id);
      add_rule(ret, space_token_id, std::move(rule));
      rule.clear();
      id = get_id("0x0D");
      if (ts.find(id) == ts.end()) {
        ts.insert(id);
      }
      rule.push_back(id);
      add_rule(ret, space_token_id, std::move(rule));
    }
    token_id spaces_opt_token_id = get_id(":ws*:");
    if (nts.find(spaces_opt_token_id) == nts.end()) {
      nts.insert(spaces_opt_token_id);
      std::vector<token_id> rule{};
      rule.push_back(space_token_id);
      rule.push_back(spaces_opt_token_id);
      add_rule(ret, spaces_opt_token_id, std::move(rule));
      add_rule(ret, spaces_opt_token_id, std::vector<token_id>());
    }
    token_id atmark_token_id = get_id(":@:");
    if (nts.find(atmark_token_id) == nts.end()) {
      nts.insert(atmark_token_id);
      add_rule(ret, atmark_token_id, std::vector<token_id>());
    }

    // stack continuations
    std::list<shared_continuation> conts{};
    for (std::size_t i = 0; i < define_rule.size(); ++i) {
      shared_continuation cont = std::make_shared<continuation>();
      cont->base_id = define_rule[i]->subtree[0]->token.id;
      cont->head_id = define_rule[i]->subtree[0]->token.id;
      cont->rule = nullptr;
      shared_syntax_tree body_list = define_rule[i]->subtree[3];
      if (!define_rule[i]->subtree[2]->subtree.empty()) {
        shared_syntax_tree body = std::make_shared<syntax_tree>();
        body->token = token(token_kind::body);
        shared_syntax_tree lparen = std::make_shared<syntax_tree>();
        lparen->token = token(token_kind::lparen);
        shared_syntax_tree rparen = std::make_shared<syntax_tree>();
        rparen->token = token(token_kind::rparen);
        shared_syntax_tree body_opt = std::make_shared<syntax_tree>();
        body_opt->token = token(token_kind::body_opt);
        body->subtree.push_back(lparen);
        body->subtree.push_back(body_list);
        body->subtree.push_back(rparen);
        body->subtree.push_back(body_opt);
        shared_syntax_tree body_internal_1 = std::make_shared<syntax_tree>();
        body_internal_1->token = token(token_kind::body_internal);
        shared_syntax_tree comma_ = std::make_shared<syntax_tree>();
        comma_->token = token(token_kind::comma_);
        shared_syntax_tree comma = std::make_shared<syntax_tree>();
        comma->token = token(token_kind::id, get_id(","));
        comma_->subtree.push_back(comma);
        shared_syntax_tree body_internal_2 = std::make_shared<syntax_tree>();
        body_internal_2->token = token(token_kind::body_internal);
        body_internal_1->subtree.push_back(comma_);
        body_internal_1->subtree.push_back(body);
        body_internal_1->subtree.push_back(body_internal_2);
        body_list = std::make_shared<syntax_tree>();
        body_list->token = token(token_kind::body_list);
        shared_syntax_tree body_list_ = std::make_shared<syntax_tree>();
        body_list_->token = token(token_kind::body_list_);
        body_list->subtree.push_back(body_internal_1);
        body_list->subtree.push_back(body_list_);
      }
      cont->body_internal = body_list->subtree[0];
      cont->body_list_ = body_list->subtree[1];
      conts.emplace_front(std::move(cont));
    }

    // evaluate rules
    std::vector<std::pair<token_id, std::shared_ptr<std::vector<token_id>>>> rules{};
    while (!conts.empty()) {
      shared_continuation cont = std::move(conts.back());
      conts.pop_back();
      if (cont->rule == nullptr) {
        cont->rule = std::make_shared<std::vector<token_id>>();
        rules.push_back(std::make_pair(cont->head_id, cont->rule));
      }
      if (cont->body_internal->subtree.empty()) {
        cont->rule = nullptr;
        if (!cont->body_list_->subtree.empty()) {
          cont->body_internal = cont->body_list_->subtree[1];
          cont->body_list_ = cont->body_list_->subtree[2];
          conts.emplace_back(std::move(cont));
        }
        continue;
      }
      token_id base_id = cont->base_id;
      shared_syntax_tree comma_ = cont->body_internal->subtree[0];
      shared_syntax_tree body = cont->body_internal->subtree[1];
      std::shared_ptr<std::vector<token_id>> rule = cont->rule;
      cont->body_internal = cont->body_internal->subtree[2];
      conts.emplace_back(std::move(cont));

      if (!comma_->subtree.empty()) {
        rule->push_back(spaces_opt_token_id);
      }

      token_id target_id;
      bool generated = false, regexp = false;
      shared_syntax_tree body_opt = nullptr;
      if (body->subtree[0]->token.kind == token_kind::lparen) {
        target_id = gen_id(id_to_token[base_id]);
        generated = true;
        nts.insert(target_id);
        body_opt = body->subtree[3];
        cont = std::make_shared<continuation>();
        cont->base_id = base_id;
        cont->head_id = target_id;
        cont->rule = nullptr;
        cont->body_internal = body->subtree[1]->subtree[0];
        cont->body_list_ = body->subtree[1]->subtree[1];
        conts.emplace_back(std::move(cont));
      } else if (body->subtree[0]->token.kind == token_kind::at) {
        target_id = atmark_token_id;
        body_opt = std::make_shared<syntax_tree>();
      } else {
        target_id = body->subtree[0]->subtree[0]->token.id;
        if (body->subtree[0]->subtree[0]->token.kind == token_kind::regexp) {
          regexp = true;
          if (nts.find(target_id) == nts.end()) {
            nts.insert(target_id);
          } else {
            regexp = false;
          }
        }
        body_opt = body->subtree[1];
      }

      bool nullable = false;
      bool infinitable = false;
      std::set<int> combination{};
      combination.insert(1);
      while (!body_opt->subtree.empty()) {
        shared_syntax_tree body_opt_ = body_opt->subtree[0];
        body_opt = body_opt->subtree[1];
        switch (body_opt_->subtree[0]->token.kind) {
          case token_kind::question:
            nullable = true;
            break;

          case token_kind::star:
            nullable = true;
            infinitable = true;
            break;

          case token_kind::plus:
            infinitable = true;
            break;

          case token_kind::lcurl: {
            shared_syntax_tree range = body_opt_->subtree[1];
            shared_syntax_tree range_ = range->subtree[1];
            int min, max;
            if (range_->subtree.empty()) {
              min = max = std::stoi(id_to_token[range->subtree[0]->token.id]);
            } else {
              min = std::stoi(id_to_token[range->subtree[0]->token.id]);
              max = std::stoi(id_to_token[range_->subtree[1]->token.id]);
            }
            std::set<int> new_combination{};
            for (int times = min; times <= max; ++times) {
              for (auto&& it = combination.begin();
                          it != combination.end();
                          ++it) {
                int value = *it * times;
                if (new_combination.find(value) == new_combination.end()) {
                  new_combination.insert(value);
                }
              }
            }
            combination = std::move(new_combination);
            break;
          }

          default:
            break;
        }
      }
      if (combination.find(0) != combination.end()) {
        nullable = true;
        combination.erase(0);

        if (combination.size() == 0) {
          continue;
        }
      }

      token_id original_target_id = target_id;
      if (combination.size() != 1 || combination.find(1) == combination.end()) {
        target_id = gen_id(id_to_token[base_id]);
        nts.insert(target_id);
      }

      if (nullable) {
        std::shared_ptr<std::vector<token_id>> dummy_rule = nullptr;
        token_id dummy_target_id = gen_id(id_to_token[base_id]);
        nts.insert(dummy_target_id);
        rule->push_back(dummy_target_id);

        dummy_rule = std::make_shared<std::vector<token_id>>();
        rules.push_back(std::make_pair(dummy_target_id, dummy_rule));
        dummy_rule->push_back(target_id);
        if (infinitable) {
          dummy_rule->push_back(dummy_target_id);
        }

        dummy_rule = std::make_shared<std::vector<token_id>>();
        rules.push_back(std::make_pair(dummy_target_id, dummy_rule));
      } else if (infinitable) {
        std::shared_ptr<std::vector<token_id>> dummy_rule = nullptr;
        token_id dummy_target_id = gen_id(id_to_token[base_id]);
        nts.insert(dummy_target_id);
        rule->push_back(target_id);
        rule->push_back(dummy_target_id);

        dummy_rule = std::make_shared<std::vector<token_id>>();
        rules.push_back(std::make_pair(dummy_target_id, dummy_rule));
        dummy_rule->push_back(target_id);
        dummy_rule->push_back(dummy_target_id);

        dummy_rule = std::make_shared<std::vector<token_id>>();
        rules.push_back(std::make_pair(dummy_target_id, dummy_rule));
      }

      if (combination.size() != 1 || combination.find(1) == combination.end()) {
        std::shared_ptr<std::vector<token_id>> dummy_rule = nullptr;
        token_id dummy_target_id;
        if (nullable || infinitable) {
          dummy_target_id = target_id;
        } else {
          dummy_target_id = gen_id(id_to_token[base_id]);
          nts.insert(dummy_target_id);
          rule->push_back(dummy_target_id);
        }

        int count = 0;
        for (auto&& it = combination.begin(); it != combination.end(); ++it) {
          dummy_rule = std::make_shared<std::vector<token_id>>();
          rules.push_back(std::make_pair(dummy_target_id, dummy_rule));
          while (count < *it) {
            dummy_rule->push_back(original_target_id);
            ++count;
          }
          dummy_target_id = gen_id(id_to_token[base_id]);
          nts.insert(dummy_target_id);
          dummy_rule->push_back(dummy_target_id);
          dummy_rule = std::make_shared<std::vector<token_id>>();
          rules.push_back(std::make_pair(dummy_target_id, dummy_rule));
        }

        target_id = original_target_id;
      } else if (!nullable && !infinitable) {
        rule->push_back(target_id);
      }

      if (generated) {
        continue;
      }

      if (regexp) {
        std::vector<std::shared_ptr<std::string>> tokens =
          regexp_split(id_to_regexp_body[target_id]);
        std::list<std::pair<token_id, std::vector<std::shared_ptr<std::string>>>> queue{};
        queue.emplace_back(std::make_pair(target_id, std::move(tokens)));
        while (!queue.empty()) {
          target_id = queue.back().first;
          tokens = std::move(queue.back().second);
          queue.pop_back();
          rule = std::make_shared<std::vector<token_id>>();
          rules.push_back(std::make_pair(target_id, rule));
          std::size_t i = 0;
          while (i < tokens.size() && !errored) {
            char ch = tokens[i]->at(0);
            token_id symbol_id;
            switch (ch) {
              case '(': {
                int nest = 1;
                std::vector<std::shared_ptr<std::string>> subtokens{};
                ++i;
                while (i < tokens.size()) {
                  if (tokens[i]->at(0) == ')') {
                    --nest;
                    if (nest == 0) {
                      break;
                    }
                  } else if (tokens[i]->at(0) == '(') {
                    ++nest;
                  }
                  subtokens.push_back(tokens[i]);
                  ++i;
                }
                if (nest != 0) {
                  errored = true;
                }
                symbol_id = gen_id(id_to_token[base_id]);
                nts.insert(symbol_id);
                queue.push_back(std::make_pair(symbol_id, std::move(subtokens)));
                break;
              }

              case '|':
                rule = std::make_shared<std::vector<token_id>>();
                rules.push_back(std::make_pair(target_id, rule));
                ++i;
                continue;

              case '[': {
                std::size_t j = 1;
                symbol_id = gen_id(id_to_token[base_id]);
                nts.insert(symbol_id);
                bool reversed = false;
                std::vector<bool> chars{};
                for (std::size_t c = 0; c < 256; ++c) {
                  chars.push_back(false);
                }
                if (tokens[i]->at(j) == '^') {
                  reversed = true;
                  ++j;
                }
                std::vector<std::size_t> char_string{};
                for (std::size_t k = j; k < tokens[i]->size() - 1; ++k) {
                  if (tokens[i]->at(k) == '\\') {
                    ++k;
                    switch (tokens[i]->at(k)) {
                      case 'n':
                        char_string.push_back('\n');
                        break;

                      case 't':
                        char_string.push_back('\t');
                        break;

                      case 'r':
                        char_string.push_back('\r');
                        break;

                      default:
                        char_string.push_back(static_cast<unsigned char>(tokens[i]->at(k)));
                        break;
                    }
                  } else {
                    char_string.push_back(static_cast<unsigned char>(tokens[i]->at(k)));
                  }
                }
                std::vector<int> ranged{};
                for (std::size_t k = 0; k < char_string.size(); ++k) {
                  ranged.push_back(0);
                }
                for (std::size_t k = 1; k < char_string.size() - 1; ++k) {
                  if (ranged[k] != 0) {
                    continue;
                  }
                  if (char_string[k] == '-') {
                    ranged[k - 1] = 1;
                    ranged[k] = 2;
                    ranged[k + 1] = 1;
                  }
                }
                for (std::size_t k = 0; k < char_string.size(); ++k) {
                  if (ranged[k] == 0) {
                    chars[char_string[k]] = true;
                  } else if (ranged[k] == 2) {
                    for (std::size_t c = char_string[k - 1]; c <= char_string[k + 1]; ++c) {
                      chars[c] = true;
                    }
                  }
                }
                if (reversed) {
                  for (std::size_t c = 0; c < 256; ++c) {
                    chars[c] = !chars[c];
                  }
                }
                for (std::size_t c = 0; c < 256; ++c) {
                  std::string token_string{};
                  if (chars[c]) {
                    if (0x20 <= c && c <= 0x7E) {
                      token_string.push_back('\'');
                      token_string.push_back(static_cast<char>(c));
                      if (c == '\\') {
                        token_string.push_back(static_cast<char>(c));
                      }
                      token_string.push_back('\'');
                    } else {
                      token_string = "0x";
                      token_string.push_back(itoh((c & 0xF0) >> 4));
                      token_string.push_back(itoh(c & 0x0F));
                    }
                    std::shared_ptr<std::vector<token_id>> dummy_rule =
                      std::make_shared<std::vector<token_id>>();
                    token_id ts_id = get_id(token_string);
                    if (ts.find(ts_id) == ts.end()) {
                      ts.insert(ts_id);
                    }
                    dummy_rule->push_back(ts_id);
                    rules.push_back(std::make_pair(symbol_id, dummy_rule));
                  }
                }
                break;
              }

              case '.': {
                symbol_id = gen_id(id_to_token[base_id]);
                nts.insert(symbol_id);
                for (int c = 0; c < 256; ++c) {
                  std::string token_string{};
                  if (0x20 <= c && c <= 0x7E) {
                    token_string.push_back('\'');
                    token_string.push_back(static_cast<char>(c));
                    if (c == '\\') {
                      token_string.push_back(static_cast<char>(c));
                    }
                    token_string.push_back('\'');
                  } else {
                    token_string = "0x";
                    token_string.push_back(itoh((c & 0xF0) >> 4));
                    token_string.push_back(itoh(c & 0x0F));
                  }
                  std::shared_ptr<std::vector<token_id>> dummy_rule =
                    std::make_shared<std::vector<token_id>>();
                  token_id ts_id = get_id(token_string);
                  if (ts.find(ts_id) == ts.end()) {
                    ts.insert(ts_id);
                  }
                  dummy_rule->push_back(ts_id);
                  rules.push_back(std::make_pair(symbol_id, dummy_rule));
                }
                break;
              }

              default: {
                std::string token_string{};
                if (0x20 <= ch && ch <= 0x7E) {
                  token_string.push_back('\'');
                  token_string.push_back(ch);
                  if (ch == '\\') {
                    token_string.push_back(tokens[i]->at(1));
                  }
                  token_string.push_back('\'');
                } else {
                  token_string = "0x";
                  token_string.push_back(itoh((ch & 0xF0) >> 4));
                  token_string.push_back(itoh(ch & 0x0F));
                }
                symbol_id = get_id(token_string);
                break;
              }
            }
            ++i;

            nullable = false;
            infinitable = false;
            combination.clear();
            combination.insert(1);
            while (i < tokens.size()) {
              ch = tokens[i]->at(0);
              if (ch == '?') {
                nullable = true;
              } else if (ch == '*') {
                nullable = true;
                infinitable = true;
              } else if (ch == '+') {
                infinitable = true;
              } else if (ch == '{') {
                int min = -1, max = -1;
                ++i;
                while (i < tokens.size()) {
                  ch = tokens[i]->at(0);
                  if (ch == '}') {
                    break;
                  } else if ('0' <= ch && ch <= '9') {
                    if (max == -1) {
                      max = ch - '0';
                    } else if (max == 0) {
                      errored = true;
                      break;
                    } else {
                      max = max * 10 + (ch - '0');
                    }
                  } else if (ch == ',') {
                    if (min != -1) {
                      errored = true;
                      break;
                    }
                    min = max;
                    max = -1;
                  }
                  ++i;
                }
                if (errored) {
                  break;
                }
                if (min == -1) {
                  min = max;
                }
                if (max == -1) {
                  errored = true;
                  break;
                }
                std::set<int> new_combination{};
                for (int times = min; times <= max; ++times) {
                  for (auto&& it = combination.begin();
                              it != combination.end();
                              ++it) {
                    int value = *it * times;
                    if (new_combination.find(value) == new_combination.end()) {
                      new_combination.insert(value);
                    }
                  }
                }
                combination = std::move(new_combination);
              } else {
                break;
              }
              ++i;
            }

            token_id original_symbol_id = symbol_id;
            if (combination.size() != 1 || combination.find(1) == combination.end()) {
              symbol_id = gen_id(id_to_token[base_id]);
              nts.insert(symbol_id);
            }

            if (nullable) {
              std::shared_ptr<std::vector<token_id>> dummy_rule = nullptr;
              token_id dummy_symbol_id = gen_id(id_to_token[base_id]);
              nts.insert(dummy_symbol_id);
              rule->push_back(dummy_symbol_id);

              dummy_rule = std::make_shared<std::vector<token_id>>();
              rules.push_back(std::make_pair(dummy_symbol_id, dummy_rule));
              dummy_rule->push_back(symbol_id);
              if (infinitable) {
                dummy_rule->push_back(dummy_symbol_id);
              }

              dummy_rule = std::make_shared<std::vector<token_id>>();
              rules.push_back(std::make_pair(dummy_symbol_id, dummy_rule));
            } else if (infinitable) {
              std::shared_ptr<std::vector<token_id>> dummy_rule = nullptr;
              token_id dummy_symbol_id = gen_id(id_to_token[base_id]);
              nts.insert(dummy_symbol_id);
              rule->push_back(symbol_id);
              rule->push_back(dummy_symbol_id);

              dummy_rule = std::make_shared<std::vector<token_id>>();
              rules.push_back(std::make_pair(dummy_symbol_id, dummy_rule));
              dummy_rule->push_back(symbol_id);
              dummy_rule->push_back(dummy_symbol_id);

              dummy_rule = std::make_shared<std::vector<token_id>>();
              rules.push_back(std::make_pair(dummy_symbol_id, dummy_rule));
            }

            if (combination.size() != 1 || combination.find(1) == combination.end()) {
              std::shared_ptr<std::vector<token_id>> dummy_rule = nullptr;
              token_id dummy_symbol_id;
              if (nullable || infinitable) {
                dummy_symbol_id = symbol_id;
              } else {
                dummy_symbol_id = gen_id(id_to_token[base_id]);
                nts.insert(dummy_symbol_id);
                rule->push_back(dummy_symbol_id);
              }

              int count = 0;
              for (auto&& it = combination.begin(); it != combination.end(); ++it) {
                dummy_rule = std::make_shared<std::vector<token_id>>();
                rules.push_back(std::make_pair(dummy_symbol_id, dummy_rule));
                while (count < *it) {
                  dummy_rule->push_back(original_symbol_id);
                  ++count;
                }
                dummy_symbol_id = gen_id(id_to_token[base_id]);
                nts.insert(dummy_symbol_id);
                dummy_rule->push_back(dummy_symbol_id);
                dummy_rule = std::make_shared<std::vector<token_id>>();
                rules.push_back(std::make_pair(dummy_symbol_id, dummy_rule));
              }
            } else if (!nullable && !infinitable) {
              rule->push_back(symbol_id);
            }
          }
        }
      } else {
        std::string token_string = id_to_token[target_id];
        if (token_string.size() >= 3 &&
            (token_string[0] == '\'' || token_string[0] == '0')) {
          if (ts.find(target_id) == ts.end()) {
            ts.insert(target_id);
          }
        } else if (nts.find(target_id) == nts.end() &&
                   ts.find(target_id) == ts.end()) {
          if (unknown.find(target_id) == unknown.end()) {
            unknown.insert(target_id);
          }
        }
      }
    }

    for (std::size_t i = 0; i < rules.size(); ++i) {
      add_rule(ret, rules[i].first, std::move(*rules[i].second));
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

  bool build_first_set(void) {
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

  bool build_follow_set(void) {
    token_id eid = get_id("<epsilon>");
    token_id did = get_id("$");

    std::unordered_map<token_id, bool> complete_to_build{};
    std::unordered_map<token_id, bool> need_complete{};
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
        need_complete[target_token_id] = true;
      } else {
        need_complete[target_token_id] = false;
      }
    }

    token_id start_symbol_id = get_id("^");
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
          updated = true;
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
        if (!complete_to_build[id] && need_complete[id]) {
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
      if (!complete_to_build[*it] && need_complete[*it]) {
        return false;
      }
    }

    return true;
  }

  bool build_table(void) {
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
    bool partial_booked = false;
    for (auto&& it = work->first.begin(); it != work->first.end(); ++it) {
      rule_id rid = it->first;
      token_id stack_token_id = work->rules[rid].first;

      if (work->ts.find(stack_token_id) == work->ts.end()) {
        bool has_epsilon = false;
        for (auto&& input_token = it->second.begin();
                    input_token != it->second.end();
                    ++input_token) {
          token_id input_token_id = *input_token;
          if (input_token_id == eid) {
            has_epsilon = true;
          } else if (work->table[stack_token_id][input_token_id] == empty_rule_id) {
            work->table[stack_token_id][input_token_id] = rid;
          } else {
            bool booked_now = true;
            rule_id old_rule_id = work->table[stack_token_id][input_token_id];
            work->table[stack_token_id][input_token_id] = booked_rule_id;
            if (sure_partial_book) {
              if (work->first[rid].find(eid) != work->first[rid].end()) {
                booked_now = false;
                partial_booked = true;
                work->table[stack_token_id][input_token_id] = old_rule_id;
              } else if (work->first[old_rule_id].find(eid) !=
                         work->first[old_rule_id].end()) {
                booked_now = false;
                partial_booked = true;
                work->table[stack_token_id][input_token_id] = rid;
              }
            }
            if (booked_now && verbose) {
              put_warning();
              std::cout << "booked on state "
                        << id_to_token[stack_token_id]
                        << " (rule "
                        << static_cast<int>(rid)
                        << " and rule "
                        << static_cast<int>(old_rule_id)
                        << ") with token "
                        << id_to_token[input_token_id]
                        << std::endl;
            }
            booked |= booked_now;
          }
        }

        if (has_epsilon) {
          for (auto&& input_token = work->follow[stack_token_id].begin();
                      input_token != work->follow[stack_token_id].end();
                      ++input_token) {
            token_id input_token_id = *input_token;
            if (work->table[stack_token_id][input_token_id] == empty_rule_id) {
              work->table[stack_token_id][input_token_id] = rid;
            } else if (work->table[stack_token_id][input_token_id] != rid) {
              bool booked_now = true;
              rule_id old_rule_id = work->table[stack_token_id][input_token_id];
              work->table[stack_token_id][input_token_id] = booked_rule_id;
              if (sure_partial_book) {
                if (work->first[rid].find(eid) != work->first[rid].end()) {
                  booked_now = false;
                  partial_booked = true;
                  work->table[stack_token_id][input_token_id] = old_rule_id;
                } else if (work->first[old_rule_id].find(eid) !=
                           work->first[old_rule_id].end()) {
                  booked_now = false;
                  partial_booked = true;
                  work->table[stack_token_id][input_token_id] = rid;
                }
              }
              if (booked_now && verbose) {
                put_warning();
                std::cout << "booked on state "
                          << id_to_token[stack_token_id]
                          << " (rule "
                          << static_cast<int>(rid)
                          << " and rule "
                          << static_cast<int>(old_rule_id)
                          << ") with token "
                          << id_to_token[input_token_id]
                          << std::endl;
              }
              booked |= booked_now;
            }
          }
        }
      }
    }

    if (verbose || table) {
      work->ts.insert(did);

      std::vector<std::vector<std::string>> table_raw{};
      std::vector<std::vector<bool>> table_colored{};
      std::vector<token_id> table_header_id{};

      // first row
      table_raw.emplace_back(std::vector<std::string>());
      table_raw.back().emplace_back("");
      table_colored.emplace_back(std::vector<bool>());
      table_colored.back().push_back(false);
      table_header_id.emplace_back(get_id("<invalid>"));
      for (auto&& stack_token = work->nts.begin();
                  stack_token != work->nts.end();
                  ++stack_token) {
        token_id stack_token_id = *stack_token;
        table_raw.back().push_back(id_to_token[stack_token_id]);
        table_colored.back().push_back(false);
        table_header_id.push_back(stack_token_id);
      }

      // rest rows
      std::set<std::string> ts{};
      for (auto&& input_token = work->ts.begin();
                  input_token != work->ts.end();
                  ++input_token) {
        ts.insert(id_to_token[*input_token]);
      }
      for (auto&& input_token = ts.begin(); input_token != ts.end(); ++input_token) {
        token_id input_token_id = token_to_id[*input_token];
        table_raw.emplace_back(std::vector<std::string>());
        table_raw.back().push_back(*input_token);
        table_colored.emplace_back(std::vector<bool>());
        table_colored.back().push_back(false);
        for (std::size_t column = 1; column < table_header_id.size(); ++column) {
          token_id stack_token_id = table_header_id[column];
          rule_id rule_id = work->table[stack_token_id][input_token_id];
          if (rule_id == empty_rule_id) {
            table_raw.back().emplace_back("-");
            table_colored.back().push_back(false);
          } else if (rule_id == booked_rule_id) {
            table_raw.back().emplace_back("*");
            table_colored.back().push_back(true);
          } else {
            table_raw.back().emplace_back(std::to_string(rule_id));
            table_colored.back().push_back(false);
          }
        }
      }

      std::size_t table_rows = table_raw.size();
      std::size_t table_columns = table_raw[0].size();

      // calculate width of the columns
      std::vector<std::size_t> column_width{};
      for (std::size_t column = 0; column < table_columns; ++column) {
        column_width.push_back(table_raw[0][column].size());
      }
      for (std::size_t row = 1; row < table_rows; ++row) {
        for (std::size_t column = 0; column < table_columns; ++column) {
          if (column_width[column] < table_raw[row][column].size()) {
            column_width[column] = table_raw[row][column].size();
          }
        }
      }

      // generate table to output
      std::vector<std::vector<std::string>> table_view{};
      for (std::size_t row = 0; row < table_rows; ++row) {
        table_view.emplace_back(std::vector<std::string>());
        for (std::size_t column = 0; column < table_columns; ++column) {
          table_view[row].emplace_back("");
          if (table_colored[row][column]) {
            table_view[row].back() += BOLD YELLOW;
          }

          // calculate margines
          std::size_t cell_width = column_width[column];
          std::size_t text_width = table_raw[row][column].size();
          std::size_t total_margin = cell_width - text_width;
          std::size_t right_margin = total_margin / 2;
          std::size_t left_margin = total_margin - right_margin;
          if ((cell_width & 1) == 1 && (total_margin & 1) == 1) {
            right_margin++;
            left_margin--;
          }

          table_view[row].back() += std::string(left_margin, ' ');
          table_view[row].back() += table_raw[row][column];
          table_view[row].back() += std::string(right_margin, ' ');
          if (table_colored[row][column]) {
            table_view[row].back() += RESET;
          }
        }
      }

      // output
      std::string table_indent = "  ";
      if (!width_limited) {
        std::cout << "table:" << std::endl;
        for (std::size_t row = 0; row < table_rows; ++row) {
          std::cout << table_indent;
          for (std::size_t column = 0; column < table_columns; ++column) {
            if (column != 0) {
              std::cout << " ";
            }
            std::cout << table_view[row][column];
          }
          std::cout << std::endl;
        }
      } else {
        std::size_t current_column = 1;
        std::vector<std::pair<std::size_t, std::size_t>> column_pairs{};
        while (current_column < table_columns) {
          std::size_t total_width = table_indent.size() + column_width[0];
          std::size_t first_column = current_column;
          std::size_t last_column = current_column;
          for (std::size_t column = first_column; column < table_columns; ++column) {
            std::size_t additional_width = 1 + column_width[column];
            if (total_width + additional_width < 100) {
              total_width += additional_width;
              last_column = column;
            } else {
              break;
            }
          }
          column_pairs.emplace_back(std::make_pair(first_column, last_column));
          current_column = last_column + 1;
        }

        if (column_pairs.size() != 1) {
          for (std::size_t pair_index = 0; pair_index < column_pairs.size(); ++pair_index) {
            std::size_t first_column = column_pairs[pair_index].first;
            std::size_t last_column = column_pairs[pair_index].second;

            if (pair_index != 0) {
              std::cout << std::endl;
            }

            std::cout << "table ("
                      << (pair_index + 1)
                      << "/"
                      << column_pairs.size()
                      << "):"
                      << std::endl;

            for (std::size_t row = 0; row < table_rows; ++row) {
              std::cout << table_indent << table_view[row][0];
              for (std::size_t column = first_column; column <= last_column; ++column) {
                std::cout << " " << table_view[row][column];
              }
              std::cout << std::endl;
            }
          }
        } else {
          std::cout << "table:" << std::endl;
          for (std::size_t row = 0; row < table_rows; ++row) {
            std::cout << table_indent;
            for (std::size_t column = 0; column < table_columns; ++column) {
              if (column != 0) {
                std::cout << " ";
              }
              std::cout << table_view[row][column];
            }
            std::cout << std::endl;
          }
        }
      }
    }

    if (!quiet && partial_booked) {
      put_warning();
      std::cout << "partial booked." << std::endl;
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

    work = rule_list_from_syntax_tree(parsed_input);
    if (work == nullptr) {
      checked = true;
      return false;
    }

    if (!build_first_set()) {
      put_error();
      std::cout << "building FIRST set failed." << std::endl;
      checked = true;
      return false;
    }

    if (!build_follow_set()) {
      put_error();
      std::cout << "building FOLLOW set failed." << std::endl;
      checked = true;
      return false;
    }

    if (!build_table()) {
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
    id_to_regexp_body.clear();
    is = nullptr;
    parsed_input = nullptr;
    work = nullptr;
    parsed = false;
    checked = false;
    verbose = false;
    quiet = false;
    table = false;
    sure_partial_book = false;
    width_limited = false;
    ll1p = false;
    lr = 0;
    ln = 0;
    genid = 0;
    current_quote = '\0';
    return;
  }

  void set_input(std::istream& is_) {
    clear();
    is = &is_;
    return;
  }

  void set_output(std::ostream& header_, std::ostream& ccfile_) {
    header = &header_;
    ccfile = &ccfile_;
    return;
  }

  bool is_ll1(void) {
    if (!checked) {
      ll1p = check();
    }
    return ll1p;
  }

  void generate_code(void) {
    if (!checked) {
      ll1p = is_ll1();
    }
    if (ll1p) {
      ::generate_code(header,
                      ccfile,
                      get_id("^"),
                      get_id("$"),
                      get_id(":@:"),
                      get_id("<end-of-body>"),
                      work->ts,
                      work->nts,
                      id_to_token,
                      token_to_id,
                      work->rules,
                      work->table);
    }
    return;
  }

  void set_verbose(void) {
    verbose = true;
    return;
  }

  void set_quiet(void) {
    quiet = true;
    return;
  }

  void set_table(void) {
    table = true;
    return;
  }

  void ensure_partial_book(void) {
    sure_partial_book = true;
    return;
  }

  void set_width_limited(void) {
    width_limited = true;
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
  bool sure_partial_book = false;
  bool width_limited = false;
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      for (std::size_t j = 1; argv[i][j] != '\0'; ++j) {
        if (argv[i][j] == 'v') {
          verbose = true;
        } else if (argv[i][j] == 'q') {
          quiet = true;
        } else if (argv[i][j] == 't') {
          table = true;
        } else if (argv[i][j] == 'p') {
          sure_partial_book = true;
        } else if (argv[i][j] == 'w') {
          width_limited = true;
        }
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
  }
  if (quiet) {
    c->set_quiet();
  }
  if (table) {
    c->set_table();
  }
  if (sure_partial_book) {
    c->ensure_partial_book();
  }
  if (width_limited) {
    c->set_width_limited();
  }

  if (c->is_ll1()) {
    std::ofstream header{};
    header.open("ryx_parse.h", std::ofstream::out);
    if (!header.is_open()) {
      std::cout << BOLD RED FATAL RESET "failed to open '"
                << "ryx_parse.h"
                << "'"
                << std::endl;
      return 1;
    }
    std::ofstream ccfile{};
    ccfile.open("ryx_parse.cc", std::ofstream::out);
    if (!ccfile.is_open()) {
      std::cout << BOLD RED FATAL RESET "failed to open '"
                << "ryx_parse.cc"
                << "'"
                << std::endl;
      return 1;
    }
    c->set_output(header, ccfile);
    c->generate_code();
    return 0;
  } else {
    return 1;
  }
}
