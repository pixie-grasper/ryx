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

#ifndef RYX_H_
#define RYX_H_

#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using token_id = std::size_t;
using rule_id = std::size_t;
using id_to_token_type = std::unordered_map<token_id, std::string>;
using token_to_id_type = std::unordered_map<std::string, token_id>;
using rules_type = std::unordered_map<rule_id,
                                      std::pair<token_id, std::vector<token_id>>>;
using table_type = std::unordered_map<token_id,
                                      std::unordered_map<token_id, rule_id>>;
using token_set_type = std::unordered_set<token_id>;

char itoh(int x);

#endif  // RYX_H_
