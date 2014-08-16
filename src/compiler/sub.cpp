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
#include "compiler/frame.hpp"
#include "common/utils.hpp"
#include <unordered_set>
#include <unordered_map>

#include <iostream> // DEBUG


namespace p6 {
  
  static bool
  _is_declerative_unop (ast_named_unop *unop)
  {
    switch (unop->get_op ())
      {
      case AST_UNOP_MY:
        return true;
      
      default:
        return false;
      }
  }
  
  
  static unsigned int
  _count_locals_needed_imp (ast_node *ast, std::unordered_set<std::string>& vars)
  {
    unsigned int count = 0;
    
    switch (ast->get_type ())
      {
      case AST_NAMED_UNARY:
        {
          ast_named_unop *unop = static_cast<ast_named_unop *> (ast);
          ast_expr *param = unop->get_param ();
          if (param->get_type () == AST_LIST)
            {
              ast_list *lst = static_cast<ast_list *> (param);
              for (ast_expr *elem : lst->get_elems ())
                {
                  if (elem->get_type () == AST_IDENT)
                    {
                      if (_is_declerative_unop (unop))
                        {
                          ast_ident *ident = static_cast<ast_ident *> (elem);
                          if (vars.find (ident->get_name ()) == vars.end ())
                            {
                              vars.insert (ident->get_name ());
                              ++ count;
                            }
                        }
                    }
                  else
                    count += _count_locals_needed_imp (elem, vars);
                  
                  // TODO: handle lists in lists?
                }
            }
          else if (param->get_type () == AST_IDENT)
            {
              if (_is_declerative_unop (unop))
                {
                  ast_ident *ident = static_cast<ast_ident *> (param);
                  if (vars.find (ident->get_name ()) == vars.end ())
                    {
                      vars.insert (ident->get_name ());
                      ++ count;
                    }
                }
            }
          else
            count += _count_locals_needed_imp (param, vars);
        }
        break;
      
      case AST_PROGRAM:
      case AST_BLOCK:
        {
          // new namespace
          std::unordered_set<std::string> nvars;
          
          ast_block *blk = static_cast<ast_block *> (ast);
          for (ast_stmt *stmt : blk->get_stmts ())
            count += _count_locals_needed_imp (stmt, nvars);
        }
        break;
      
      case AST_MODULE:
        {
          ast_module *mod = static_cast<ast_module *> (ast);
          count += _count_locals_needed_imp (mod->get_body (), vars);
        }
        break;
      
      
      case AST_EXPR_STMT:
        count += _count_locals_needed_imp (
          (static_cast<ast_expr_stmt *> (ast))->get_expr (), vars);
        break;
      
      case AST_BINARY:
        {
          ast_binop *bop = static_cast<ast_binop *> (ast);
          
          count += _count_locals_needed_imp (bop->get_lhs (), vars);
          count += _count_locals_needed_imp (bop->get_rhs (), vars);
        }
        break;
      
      case AST_SUB_CALL:
        {
          ast_sub_call *call = static_cast<ast_sub_call *> (ast);
          if (call->get_params ())
            count += _count_locals_needed_imp (call->get_params (), vars);
        }
        break;
      
      case AST_IF:
        {
          ast_if *aif = static_cast<ast_if *> (ast);
          count += _count_locals_needed_imp (aif->get_main_part ().cond, vars);
          count += _count_locals_needed_imp (aif->get_main_part ().body, vars);
          if (aif->get_else_part ())
            count += _count_locals_needed_imp (aif->get_else_part (), vars);
          
          auto& elsifs = aif->get_elsif_parts ();
          for (auto p : elsifs)
            {
              count += _count_locals_needed_imp (p.cond, vars);
              count += _count_locals_needed_imp (p.body, vars);
            }
        }
        break;
      
      case AST_WHILE:
        {
          ast_while *awhile = static_cast<ast_while *> (ast);
          count += _count_locals_needed_imp (awhile->get_cond (), vars);
          count += _count_locals_needed_imp (awhile->get_body (), vars);
        }
        break;
      
      case AST_FOR:
        {
          ast_for *afor = static_cast<ast_for *> (ast);
          count += _count_locals_needed_imp (afor->get_body (), vars);
          ++ count; // var
          ++ count; // anonymous index variable
        }
        break;
      
      case AST_LOOP:
        {
          ast_loop *loop = static_cast<ast_loop *> (ast);
          if (loop->get_init ())
            count += _count_locals_needed_imp (loop->get_init (), vars);
          if (loop->get_cond ())
            count += _count_locals_needed_imp (loop->get_cond (), vars);
          if (loop->get_step ())
            count += _count_locals_needed_imp (loop->get_step (), vars);
          count += _count_locals_needed_imp (loop->get_body (), vars);
        }
        break;
      
      default: ;
      }
    
    return count;
  }
  
  /* 
   * Counts the number of local variables that should allocated in the
   * specified block.
   */
  static unsigned int
  _count_locals_needed (ast_block *ast)
  {
    std::unordered_set<std::string> vars;
    return _count_locals_needed_imp (ast, vars);
  }
  
  
  
  void
  compiler::compile_return (ast_return *ast)
  {
    auto expr = ast->get_expr ();
    if (expr)
      this->compile_expr (expr);
    else
      this->cgen->emit_push_undef ();
    
    this->cgen->emit_return ();
  }
  
  
  
  /* 
   * Special subroutines:
   */
//------------------------------------------------------------------------------
  
  void
  compiler::compile_sub_last (ast_sub_call *ast)
  {
    auto& params = ast->get_params ()->get_elems ();
    if (!params.empty ())
      {
        this->errs.error (ES_COMPILER, "`last' expects 0 arguments",
          ast->get_line (), ast->get_column ());
        return;
      }
    
    // find innermost loop frame
    frame *frm = &this->top_frame ();
    while (frm->get_type () != FT_LOOP && frm->get_parent ())
      frm = frm->get_parent (); 
    if (!frm || frm->get_type () != FT_LOOP)
      {
        this->errs.error (ES_COMPILER, "no loop structure to break from",
          ast->get_line (), ast->get_column ());
        return;
      }
    
    auto itr = frm->extra.find ("last");
    if (itr == frm->extra.end ())
      {
        this->errs.error (ES_COMPILER,
          "cannot break from inner-most loop structure (not supported?)",
          ast->get_line (), ast->get_column ());
        return;
      }
    
    int lbl_done = itr->second;
    this->cgen->emit_jmp (lbl_done);
  }
  
  void
  compiler::compile_sub_next (ast_sub_call *ast)
  {
    auto& params = ast->get_params ()->get_elems ();
    if (!params.empty ())
      {
        this->errs.error (ES_COMPILER, "`next' expects 0 arguments",
          ast->get_line (), ast->get_column ());
        return;
      }
    
    // find innermost loop frame
    frame *frm = &this->top_frame ();
    while (frm->get_type () != FT_LOOP && frm->get_parent ())
      frm = frm->get_parent (); 
    if (!frm || frm->get_type () != FT_LOOP)
      {
        this->errs.error (ES_COMPILER, "no loop structure to break from",
          ast->get_line (), ast->get_column ());
        return;
      }
    
    switch (frm->extra["subtype"])
      {
      case FST_WHILE:
        {
          int lbl_loop = frm->extra["next"];
          this->cgen->emit_jmp (lbl_loop);
        }
        break;
      
      case FST_FOR:
        {
          // increment index variable
          int index_var = frm->extra["index_var"];
          this->cgen->emit_load (index_var);
          this->cgen->emit_push_int (1);
          this->cgen->emit_add ();
          this->cgen->emit_store (index_var);
          
          int lbl_loop = frm->extra["next"];
          this->cgen->emit_jmp (lbl_loop);
        }
        break;
      
      default:
        throw std::runtime_error ("`next' called in unsupported loop type");
      }
  }
  
  
  
  /* 
   * Must be handled as a special function since arrays are flattened out when
   * passed to a subroutine.
   */
  void
  compiler::compile_sub_push (ast_sub_call *ast)
  {
    
  } 
  
//------------------------------------------------------------------------------
  
  
  
  static bool
  _is_builtin (const std::string& name)
  {
    static const std::unordered_set<std::string> _set {
      "print", "elems", "substr", "length", "shift",
    };
    
    auto itr = _set.find (name);
    return (itr != _set.end ());
  }
  
  void
  compiler::compile_sub_call (ast_sub_call *ast)
  {
    std::string name = ast->get_name ();
    
    static std::unordered_map<std::string,
      void (compiler::*)(ast_sub_call *)> _ssub_map {
      { "last", &compiler::compile_sub_last },
      { "next", &compiler::compile_sub_next },
      { "push", &compiler::compile_sub_push },
    };
    
    // parameters
    this->compile_list (ast->get_params ());
    
    auto itr = _ssub_map.find (name);
    if (itr != _ssub_map.end ())
      {
        // special function
        (this->* itr->second) (ast);
      }
    else if (_is_builtin (name))
      {
        // builtin
        this->cgen->emit_call_builtin (name, ast->get_params ()->get_elems ().size ());
      }
    else
      {
        // find the package the subroutine's in, starting with the topmost
        // one, going down.
        
        std::string abs_path; // absolute path
        package *pack = &this->top_package ();
        while (pack)
          {
            subroutine_info* s = pack->find_sub (name);
            if (!s)
              pack = pack->get_parent ();
            else
              {
                // turn relative name into absolute path.
                package *sp = pack->get_subpackage_containing (name);
                abs_path = sp->get_path ();
                if (!abs_path.empty ())
                  abs_path.append ("::");
                abs_path.append (utils::strip_packages (s->name));
                break;
              }
          }
        
        if (pack)
          {
            this->sub_uses.push_back ({
              .name = abs_path,
              .ast  = ast,
              .pos  = this->cgen->get_buffer ().get_pos (),
            });
            
            auto& sub = this->packs.front ()->get_sub (abs_path);
            this->cgen->emit_call (sub.lbl);
          }
        else
          {
            this->sub_uses.push_back ({
              .name = name,
              .ast  = ast,
              .pos  = this->cgen->get_buffer ().get_pos (),
            });
            
            // if it's an import, it will be replaced by the linker.
            this->cgen->emit_call (0);
          }
      }
  }
  
  
  
  void
  compiler::compile_sub (ast_sub *ast)
  {
    ast_block *body = ast->get_body ();
    std::string name = ast->get_name ();
    
    std::string full_name = this->top_package ().get_path ();
    if (!full_name.empty ())
      full_name.append ("::");
    full_name.append (name);
    
    // create new frame
    this->push_frame (FT_SUBROUTINE);
    frame& frm = this->top_frame ();
    
    // jump over the subroutine.
    int lbl_over = this->cgen->create_label ();
    this->cgen->emit_jmp (lbl_over);
    
    // mark the subroutine as generated, and handle redeclarations.
    {
      package& pack = this->top_package ();
      auto& sub = pack.get_sub (name);
      if (sub.marked)
        {
          this->errs.error (ES_COMPILER, "redeclaration of subroutine `" +
            full_name + "'", ast->get_line (), ast->get_column ());
          return;
        }
      
      unsigned int sub_pos = this->cgen->get_buffer ().get_pos ();
      this->cgen->mark_label (sub.lbl);
      sub.marked = true;
      
      if (name[0] != '#')
        {
          this->mod->export_sub (full_name, sub_pos);
        }
    }
    
    unsigned int loc_count = _count_locals_needed (body);
    this->cgen->emit_push_frame (loc_count);
    
    // set up arguments
    auto& params = ast->get_params ();
    for (unsigned int i = 0; i < params.size (); ++i)
      {
        auto& param = params[i];
        frm.add_arg (param.ident->get_name ());
      }
    
    // compile the body
    for (ast_stmt *stmt : body->get_stmts ())
      {
        this->compile_stmt (stmt);
      }
    
    // add an implicit return statement
    ast_return ret {};
    this->compile_return (&ret);
    
    this->cgen->mark_label (lbl_over);
    this->pop_frame ();
  }
}
