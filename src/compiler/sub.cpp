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

#include "compiler/compiler.hpp"
#include "compiler/codegen.hpp"
#include "compiler/frame.hpp"
#include "common/utils.hpp"
#include "compiler/asttools.hpp"
#include <unordered_set>
#include <unordered_map>
#include <sstream>

#include <iostream> // DEBUG


namespace arane {
  
  void
  compiler::compile_return (ast_return *ast)
  {
    auto expr = ast->get_expr ();
    if (expr)
      {
        this->compile_expr (expr);
        this->enforce_return_type (expr);
      }
    else
      {
        ast_undef undef {};
        this->compile_expr (&undef);
        this->enforce_return_type (&undef);
      }
    
    this->cgen->emit_return ();
  }
  
  void
  compiler::enforce_return_type (ast_expr *expr)
  {
    // get the subroutine we are currently in
    frame *frm = &this->top_frame ();
    while (frm->get_type () != FT_SUBROUTINE)
      frm = frm->get_parent ();
    
    auto& ti = frm->sub->get_return_type ();
    if (!ti.is_none ())
      {
        auto dt = this->deduce_type (expr);
        if (dt.is_none ())
          {
            this->cgen->emit_to_compatible (ti);
          }
        else
          {
            auto tc = dt.check_compatibility (ti);
            if (tc == TC_INCOMPATIBLE)
              {
                this->errs.error (ES_COMPILER,
                  "attempting to return a value of type `" + dt.str () + "'"
                  " when subroutine is expected to return `" + ti.str () + "'",
                  expr->get_line (), expr->get_column ());
                return;
              }
            else if (tc == TC_CASTABLE)
              {
                this->cgen->emit_to_compatible (ti);
              }
          }
      }
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
  
  
  
  void
  compiler::compile_sub_checkpoint (ast_sub_call *ast)
  {
    // 
    // DEBUG
    //
    
    ast_integer *n = static_cast<ast_integer *> (ast->get_params ()->get_elems ()[0]);
    this->cgen->emit_checkpoint (n->get_value ()); 
  }
  
//------------------------------------------------------------------------------
  
  
  
  /* 
   * Subroutine calls:
   */
//------------------------------------------------------------------------------
  
  namespace {
    
    struct sc_extra_t {
      package *pack;
      std::string name;
    };
  }
  
  
  static bool
  _is_builtin (const std::string& name)
  {
    static const std::unordered_set<std::string> _set {
      "print", "say",
      
      "elems", "push", "pop", "shift",
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
    };
    
    if (name == "checkpoint")
      {
        this->compile_sub_checkpoint (ast);
        return;
      }
    
    auto itr = _ssub_map.find (name);
    if (itr != _ssub_map.end ())
      {
        // special function
        (this->* itr->second) (ast);
      }
    else if (_is_builtin (name))
      {
        // parameters (push in reverse order)
        auto& params = ast->get_params ()->get_elems ();
        for (auto itr = params.rbegin (); itr != params.rend (); ++itr)
          {
            auto param = *itr;
            this->compile_expr (param);
          }
        
        // builtin
        this->cgen->emit_call_builtin (name, params.size ());
      }
    else
      {
        // find the package the subroutine's in, starting with the topmost
        // one, going down.
        
        package *pack = &this->top_package ();
        while (pack)
          {
            subroutine_info* s = pack->find_sub (name);
            if (!s)
              pack = pack->get_parent ();
            else
              {
                // turn relative name into absolute path.
                std::string abs_path;
                package *sp = pack->get_subpackage_containing (name);
                abs_path = sp->get_path ();
                if (!abs_path.empty ())
                  abs_path.append ("::");
                abs_path.append (utils::strip_packages (s->name));
                name = abs_path;
                break;
              }
          }
        
        auto sig = this->sigs.find_sub (name);
        if (!sig)
          {
            this->errs.error (ES_COMPILER, "call to undeclared subroutine `" + name + "'",
              ast->get_line (), ast->get_column ());
            return;
          }
        
        auto& params = ast->get_params ()->get_elems ();
        if (params.size () < sig->params.size ())
          {
            std::stringstream ss;
            ss << "subroutine `" << sig->name << "' expects at least " << sig->params.size ()
              << " required parameter(s), " << params.size () << " given.";
            this->errs.error (ES_COMPILER, ss.str (), ast->get_line (),
              ast->get_column ());
            return;
          }
        
        // compile parameters in reverse order
        for (int i = params.size () - 1; i >= 0; --i)
          {
            auto param = params[i];
            this->compile_expr (param);
            
            if (i < (int)sig->params.size ())
              {
                if (sig->params[i].is_copy)
                  this->cgen->emit_copy ();
                
                // if the type is not known beforehand, defer the checking
                // to runtime.
                auto& ti = sig->params[i].ti;
                if (!ti.is_none ())
                  {
                    auto dt = this->deduce_type (param);
                    if (dt.is_none ())  // deduction failed
                      {
                        // cast to compatible type, or die.
                        this->cgen->emit_to_compatible (ti);
                      }
                    else
                      {
                        // check for incompatible types
                        auto tc = dt.check_compatibility (ti);
                        if (tc == TC_INCOMPATIBLE)
                          {
                            this->errs.error (ES_COMPILER,
                              "attempting to pass a parameter of an incompatible"
                              " type `" + dt.str () + "' where `" + ti.str () + "'"
                              " is expected", param->get_line (), param->get_column ());
                            return;
                          }
                        else if (tc == TC_CASTABLE)
                          {
                            // an cast is still needed
                            this->cgen->emit_to_compatible (ti);
                          }
                      }
                  }
              }
          }
        
        // if @_ is used in the subroutine, create it
        if (sig->uses_def_arr)
          this->cgen->emit_make_arg_array (params.size ());
        
        // call instruction
        if (pack)
          {
            subroutine_info& sub = this->global_package ().get_sub (name);
            
            int call_lbl = this->cgen->create_and_mark_label ();
            this->cgen->emit_call (sub.lbl, params.size () + sig->uses_def_arr);
            
            this->sub_uses.push_back ({
              .name = name,
              .ast  = ast,
              .pos  = call_lbl,
            });
          }
        else
          {
            // Most likely an imported sub.
            // If so, just leave an empty call instruction which will be updated
            // by the linker.
            int call_lbl = this->cgen->create_and_mark_label ();
            {
              auto& buf = cgen->get_buffer ();
              buf.put_byte (0x71);
              buf.put_int (0);
              buf.put_byte (params.size () + sig->uses_def_arr);
            }
            
            this->sub_uses.push_back ({
              .name = name,
              .ast  = ast,
              .pos  = call_lbl,
            });
          }
      }
  }
  
//------------------------------------------------------------------------------
  
  
  
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
    frm.sub = ast;
    
    // jump over the subroutine.
    int lbl_over = this->cgen->create_label ();
    this->cgen->emit_jmp (lbl_over);
    
    // mark the subroutine as generated, and handle redeclarations.
    package& pack = this->top_package ();
    auto& sub = pack.get_sub (name);
    {
      if (sub.marked)
        {
          this->errs.error (ES_COMPILER, "redeclaration of subroutine `" +
            full_name + "'", ast->get_line (), ast->get_column ());
          return;
        }
      
      this->cgen->mark_label (sub.lbl);
      sub.marked = true;
    }
    
    unsigned int loc_count = ast::count_locals_needed (body);
    this->cgen->emit_push_frame (loc_count);
    
    // set up arguments
    auto& params = ast->get_params ();
    for (unsigned int i = 0; i < params.size (); ++i)
      {
        auto& param = params[i];
        auto expr = param.expr;
        switch (expr->get_type ())
          {
          case AST_IDENT:
            frm.add_arg ((static_cast<ast_ident *> (expr))->get_name ());
            break;
          
          case AST_OF_TYPE:
            {
              ast_of_type *tn = static_cast<ast_of_type *> (expr);
              if (tn->get_expr ()->get_type () != AST_IDENT)
                {
                  this->errs.error (ES_COMPILER, "expected an identifier after "
                    "type name in subroutine parameter list", tn->get_line (),
                    tn->get_column ());
                  return;
                }
              
              ast_ident *ident = static_cast<ast_ident *> (tn->get_expr ());
              frm.add_arg (ident->get_name (), tn->get_typeinfo ());
            }
            break;
          
          default:
            throw std::runtime_error ("invalid parameter type");
          }
      }
    
    // compile the body
    this->cgen->emit_push_microframe ();
    auto& stmts = body->get_stmts ();
    for (unsigned int i = 0; i < stmts.size (); ++i)
      {
        auto stmt = stmts[i];
        if (i != stmts.size () - 1)
          this->compile_stmt (stmt);
        else
          {
            // last statement
            if (stmt->get_type () == AST_EXPR_STMT)
              {
                auto expr = (static_cast<ast_expr_stmt *> (stmt))->get_expr ();
                this->compile_expr (expr);
                this->enforce_return_type (expr);
                this->cgen->emit_return ();
              }
            else
              this->compile_stmt (stmt);
          }
      }
    
    // add an implicit return statement
    ast_return ret {};
    this->compile_return (&ret);
    
    this->cgen->mark_label (lbl_over);
    this->pop_frame ();
    
    // handle traits
    for (auto& trait : ast->get_traits ())
      {
        if (trait == "export")
          {
            // must be inside module
            package *pack = &this->top_package ();
            if (pack && pack->get_type () != PT_MODULE)
              pack = pack->get_parent ();
            if (!pack || pack->get_type () != PT_MODULE)
              {
                this->errs.error (ES_COMPILER,
                  "trait `export' can only be used inside a module",
                  ast->get_line (), ast->get_column ());
                return;
              }
            
            if (name[0] != '#')
              {
                // TODO: use label instead of raw position!!!
                unsigned int sub_pos = this->cgen->get_label_pos (sub.lbl);
                this->mod->export_sub (full_name, sub_pos);
              }
          }
        else
          {
            this->errs.error (ES_COMPILER, "use of unknown trait `" + trait + "'",
              ast->get_line (), ast->get_column ());
            return;
          }
      }
  }
}

