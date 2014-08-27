/*
 * Arane - A Perl 6 interpreter.
 * Copyright (C) 2014 Jacob Zhitomirsky
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ARANE__COMPILER__SUB__H_
#define _ARANE__COMPILER__SUB__H_

#include "parser/ast.hpp"
#include "common/types.hpp"
#include <vector>


namespace arane {
  
  // forward dec:
  class compilation_context;
  

  struct subroutine_param
  {
    std::string name;
    
    // the type of parameter (inferred statically by the compiler).
    // should it be known at compile time, a runtime check can be avoided.
    type_info type;
  };
  
  /* 
   * Holds subroutine information.
   */
  struct subroutine_info
  {
    // Marks whether the subroutine has been generated within the code section.
    bool marked;
    
    // A label pointing to the subroutine's position within the code section.
    int lbl;
    
    // The name of the subroutine.
    std::string name;
    
    std::vector<subroutine_param> params;
    
    // The return type
    type_info ret_ti;
  };
  
  
  struct subroutine_use
  {
    // The name of the subroutine being used.
    std::string name;
    
    // The ast node of the call.
    ast_node *ast;
    
    // A label to the position of the call instruction.
    int pos;
  };
}

#endif

