/*
 * P6 - A Perl 6 interpreter.
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
 * You should have received a copy of the GNwU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "compiler/compiler.hpp"
#include "compiler/codegen.hpp"


namespace p6 {
  
  void
  compiler::compile_expr_stmt (ast_expr_stmt *ast)
  {
    ast_expr *inner = ast->get_expr ();
    this->compile_expr (inner);
    this->cgen->emit_pop ();
  }
  
  
  
  void
  compiler::compile_if (ast_if *ast)
  {
    int lbl_mpart_false = this->cgen->create_label ();
    int lbl_done = this->cgen->create_label ();
    
    // 
    // main part
    // 
    
    auto main_part = ast->get_main_part ();
    this->compile_expr (main_part.cond);
    this->cgen->emit_is_false ();
    this->cgen->emit_push_int (1);
    this->cgen->emit_je (lbl_mpart_false);
    
    // main part true
    this->compile_block (main_part.body);
    this->cgen->emit_jmp (lbl_done);
    
    // main part false
    this->cgen->mark_label (lbl_mpart_false);
    
    // 
    // elsifs.
    // 
    auto& elsifs = ast->get_elsif_parts ();
    for (auto elsif : elsifs)
      {
        int lbl_part_false = this->cgen->create_label ();
        
        this->compile_expr (elsif.cond);
        this->cgen->emit_is_false ();
        this->cgen->emit_push_int (1);
        this->cgen->emit_je (lbl_part_false);
        
        // part true
        this->compile_block (elsif.body);
        this->cgen->emit_jmp (lbl_done);
        
        // part false
        this->cgen->mark_label (lbl_part_false);
      }
    
    // 
    // else
    // 
    if (ast->get_else_part ())
      {
        this->compile_block (ast->get_else_part ());
      }
    
    this->cgen->mark_label (lbl_done);
  }
  
  
  
  void
  compiler::compile_while (ast_while *ast)
  {
    int lbl_done = this->cgen->create_label ();
    int lbl_loop = this->cgen->create_label ();
    
    this->push_frame (FT_LOOP);
    frame& frm = this->top_frame ();
    frm.extra["subtype"] = FST_WHILE;
    frm.extra["last"] = lbl_done;
    frm.extra["next"] = lbl_loop;
    
    // test
    this->cgen->mark_label (lbl_loop);
    this->compile_expr (ast->get_cond ());
    this->cgen->emit_is_false ();
    this->cgen->emit_push_int (1);
    this->cgen->emit_je (lbl_done);
    
    // body
    this->compile_block (ast->get_body (), false);
    this->cgen->emit_jmp (lbl_loop);
    
    this->cgen->mark_label (lbl_done);
    
    this->pop_frame ();
  }
  
  
  
  void
  compiler::compile_for (ast_for *ast)
  {
    int lbl_done = this->cgen->create_label ();
    int lbl_loop = this->cgen->create_label ();
    
    this->push_frame (FT_LOOP);
    frame& frm = this->top_frame ();
    frm.add_local (ast->get_var ()->get_name ());
    int loop_var = frm.get_local (ast->get_var ()->get_name ())->index;
    
    // setup index variable
    int index_var = frm.alloc_local ();
    this->cgen->emit_push_int (0);
    this->cgen->emit_store (index_var);
    
    
    frm.extra["subtype"] = FST_FOR;
    frm.extra["last"] = lbl_done;
    frm.extra["next"] = lbl_loop;
    frm.extra["loop_var"] = loop_var;
    frm.extra["index_var"] = index_var;
    
    
    // list
    this->compile_expr (ast->get_arg ());
    
    // list length
    this->cgen->emit_dup ();
    this->cgen->emit_box_array (1);
    this->cgen->emit_call_builtin ("elems", 1);
    
    // test
    this->cgen->mark_label (lbl_loop);
    this->cgen->emit_load (index_var);
    this->cgen->emit_dupn (1);  // list length
    this->cgen->emit_jge (lbl_done);
    
    // body
    this->cgen->emit_dupn (1);    // list
    this->cgen->emit_load (index_var);
    this->cgen->emit_array_get ();
    this->cgen->emit_store (loop_var);
    this->compile_block (ast->get_body (), false);
    
    // increment index variable
    this->cgen->emit_load (index_var);
    this->cgen->emit_push_int (1);
    this->cgen->emit_add ();
    this->cgen->emit_store (index_var);
    this->cgen->emit_jmp (lbl_loop);
    
    this->cgen->mark_label (lbl_done);
    this->cgen->emit_pop ();  // list length
    this->cgen->emit_pop ();  // list
    
    this->pop_frame ();
  }
  
  
  
  void
  compiler::compile_loop (ast_loop *ast)
  {
    int lbl_done = this->cgen->create_label ();
    int lbl_loop = this->cgen->create_label ();
    
    this->push_frame (FT_LOOP);
    frame& frm = this->top_frame ();
    frm.extra["subtype"] = FST_LOOP;
    frm.extra["last"] = lbl_done;
    frm.extra["next"] = lbl_loop;
    
    // init
    if (ast->get_init ())
      {
        this->compile_expr (ast->get_init ());
        this->cgen->emit_pop ();
      }
    
    // cond
    this->cgen->mark_label (lbl_loop);
    if (ast->get_cond ())
      {
        this->compile_expr (ast->get_cond ());
        this->cgen->emit_is_false ();
        this->cgen->emit_push_int (1);
        this->cgen->emit_je (lbl_done);
      }
    
    // body
    this->compile_block (ast->get_body ());
    
    // step
    if (ast->get_step ())
      {
        this->compile_expr (ast->get_step ());
        this->cgen->emit_pop ();
      }
    
    this->cgen->emit_jmp (lbl_loop);
    
    this->cgen->mark_label (lbl_done);
    
    this->pop_frame ();
  }
  
  
  
  void
  compiler::compile_block (ast_block *ast, bool create_frame)
  {
    if (create_frame)
      this->push_frame (FT_BLOCK);
    for (ast_stmt *stmt : ast->get_stmts ())
      {
        this->compile_stmt (stmt);
      }
    if (create_frame)
      this->pop_frame ();
  }
  
  
  
  void
  compiler::compile_use (ast_use *ast)
  {
    this->mod->add_dependency (ast->get_value ());
  }
  
  
  
  void
  compiler::compile_package (ast_package *ast)
  {
    this->push_package (PT_PACKAGE, ast->get_name ());
    this->compile_block (ast->get_body ());
    this->pop_package ();
  }
  
  
  
  void
  compiler::compile_module (ast_module *ast)
  {
    this->push_package (PT_MODULE, ast->get_name ());
    this->compile_block (ast->get_body ());
    this->pop_package ();
  }
  
  
  
  void
  compiler::compile_stmt (ast_stmt *ast)
  {
    switch (ast->get_type ())
      {
      case AST_EXPR_STMT:
        this->compile_expr_stmt (static_cast<ast_expr_stmt *> (ast));
        break;
        
      case AST_BLOCK:
        this->compile_block (static_cast<ast_block *> (ast));
        break;
        
      case AST_SUB:
        this->compile_sub (static_cast<ast_sub *> (ast));
        break;
      
      case AST_RETURN:
        this->compile_return (static_cast<ast_return *> (ast));
        break;
      
      case AST_IF:
        this->compile_if (static_cast<ast_if *> (ast));
        break;
        
      case AST_WHILE:
        this->compile_while (static_cast<ast_while *> (ast));
        break;
        
      case AST_FOR:
        this->compile_for (static_cast<ast_for *> (ast));
        break;
      
      case AST_LOOP:
        this->compile_loop (static_cast<ast_loop *> (ast));
        break;
      
      case AST_USE:
        this->compile_use (static_cast<ast_use *> (ast));
        break;
      
      case AST_MODULE:
        this->compile_module (static_cast<ast_module *> (ast));
        break;
      
      case AST_PACKAGE:
        this->compile_package (static_cast<ast_package *> (ast));
        break;
        
      default:
        throw std::runtime_error ("invalid statement type");
      }
  }
}
