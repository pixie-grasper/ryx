// codegen.h -- header file for codegen.cc
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

#ifndef CODEGEN_H_
#define CODEGEN_H_

#include "ryx.h"

extern void generate_code(std::ostream* header_,
                          std::ostream* ccfile_,
                          const token_set_type& terminate_symbols,
                          const token_set_type& non_terminate_symbols,
                          const id_to_token_type& id_to_token,
                          const token_to_id_type& token_to_id,
                          const rules_type& rules,
                          const table_type& table);

#endif  // CODEGEN_H_
