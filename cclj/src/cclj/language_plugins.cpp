//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/plugins/base_plugins.h"
#include "cclj/plugins/language_plugins.h"
#include "cclj/module.h"

#ifdef _WIN32
#pragma warning(push,2)
#endif
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/Scalar.h"
#ifdef _WIN32
#pragma warning(pop)
#endif

#include "cclj/plugins/plugin_impl_helper.h"

using namespace cclj;
using namespace cclj::lisp;
using namespace cclj::plugins;
using namespace llvm;



namespace {


	struct if_ast_node : public ast_node
	{
		if_ast_node(const type_ref& type)
		: ast_node(type)
		{
		}

		virtual bool executable_statement() const { return true; }

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			ast_node& cond_node = children().front();
			ast_node& true_node = *cond_node.next_node();
			ast_node& false_node = *true_node.next_node();
			auto cond_result = cond_node.compile_second_pass(context);

			Function *theFunction = context._builder.GetInsertBlock()->getParent();

			BasicBlock *ThenBB = BasicBlock::Create(getGlobalContext(), "then", theFunction);

			BasicBlock *ElseBB = BasicBlock::Create(getGlobalContext(), "else");

			BasicBlock *MergeBB = BasicBlock::Create(getGlobalContext(), "ifcont");
			context._builder.CreateCondBr(cond_result.first.get(), ThenBB, ElseBB);
			context._builder.SetInsertPoint(ThenBB);

			auto true_result = true_node.compile_second_pass(context);
			context._builder.CreateBr(MergeBB);

			//note that evaluating the node can change the builder's insert block (just as we are doing now).
			ThenBB = context._builder.GetInsertBlock();

			theFunction->getBasicBlockList().push_back(ElseBB);
			context._builder.SetInsertPoint(ElseBB);

			auto false_result = false_node.compile_second_pass(context);

			//should have been caught in the type check phase.
			if (true_result.second != false_result.second)
				throw runtime_error("Invalid if statement, expressions do not match type");

			context._builder.CreateBr(MergeBB);

			//update else basic block
			ElseBB = context._builder.GetInsertBlock();


			// Emit merge block.
			theFunction->getBasicBlockList().push_back(MergeBB);
			context._builder.SetInsertPoint(MergeBB);
			if (true_result.second != &context._type_library->get_void_type())
			{
				PHINode *PN = context._builder.CreatePHI(context.type_ref_type(*true_result.second).get(), 2,
					"iftmp");

				PN->addIncoming(true_result.first.get(), ThenBB);
				PN->addIncoming(false_result.first.get(), ElseBB);
				return make_pair(PN, true_result.second);
			}
			return make_pair(llvm_value_ptr_opt(), true_result.second);
		}
	};

	struct if_compiler_plugin : public compiler_plugin
	{
		if_compiler_plugin()
		: compiler_plugin()
		{
		}

		virtual ast_node* type_check(reader_context& context, lisp::cons_cell& cell)
		{
			cons_cell& cond_cell = object_traits::cast_ref<cons_cell>(cell._next);
			cons_cell& true_cell = object_traits::cast_ref<cons_cell>(cond_cell._next);
			cons_cell& false_cell = object_traits::cast_ref<cons_cell>(true_cell._next);
			if (false_cell._next) throw runtime_error("Invalid if state, must have only 3 sub lists");
			ast_node& cond_node = context._type_checker(cond_cell._value);
			ast_node& true_node = context._type_checker(true_cell._value);
			ast_node& false_node = context._type_checker(false_cell._value);
			if (context._type_library->to_base_numeric_type(cond_node.type()) != base_numeric_types::i1)
				throw runtime_error("Invalid if condition type, must be boolean");
			if (&true_node.type() != &false_node.type())
				throw runtime_error("Invalid if statement, true and false branches have different types");

			ast_node* retval = context._ast_allocator->construct<if_ast_node>(true_node.type());
			retval->children().push_back(cond_node);
			retval->children().push_back(true_node);
			retval->children().push_back(false_node);
			return retval;
		}
	};

	struct let_ast_node : public ast_node
	{
		vector<pair<symbol*, ast_node*> > _let_vars;
		let_ast_node(string_table_ptr st, const type_ref& type)
			: ast_node(type)
		{
		}

		virtual bool executable_statement() const { return true; }

		static void initialize_assign_block(compiler_context& context
			, vector<pair<symbol*, ast_node*> >& vars )
		{
			Function* theFunction = context._builder.GetInsertBlock()->getParent();
			IRBuilder<> entryBuilder(&theFunction->getEntryBlock(), theFunction->getEntryBlock().begin());

			for_each(vars.begin(), vars.end(), [&]
				(pair<symbol*, ast_node*>& var_dec)
			{
				auto var_eval = var_dec.second->compile_second_pass(context);
				if (var_eval.first)
				{
					auto alloca = entryBuilder.CreateAlloca(context.type_ref_type(*var_eval.second).get()
						, 0, var_dec.first->_name.c_str());
					context._builder.CreateStore(var_eval.first.get(), alloca);
					context._module->add_local_variable(var_dec.first->_name, *var_eval.second, *alloca);
				}
				else
				{
					context._module->add_void_local_variable(var_dec.first->_name);
				}	
			});
		}

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			compiler_scope_watcher _let_scoping(context);
			module::compilation_variable_scope __let_scope(context._module);
			initialize_assign_block(context, _let_vars);
			pair<llvm_value_ptr_opt, type_ref_ptr> retval;
			for (auto iter = children().begin(), end = children().end(); iter != end; ++iter)
			{
				retval = iter->compile_second_pass(context);
			}
			return retval;
		}
	};

	CCLJ_BASE_PLUGINS_DESTRUCT_AST_NODE(let_ast_node);

	struct let_compiler_plugin : public compiler_plugin
	{
		let_compiler_plugin()
		: compiler_plugin()
		{
		}

		virtual ast_node* type_check(reader_context& context, lisp::cons_cell& cell)
		{
			cons_cell& array_cell = object_traits::cast_ref<cons_cell>(cell._next);
			cons_cell& body_start = object_traits::cast_ref<cons_cell>(array_cell._next);
			object_ptr_buffer assign = object_traits::cast_ref<array>(array_cell._value)._data;
			vector<pair<symbol*, ast_node*> > let_vars;
			
			module::type_check_variable_scope __let_scope(context._module);
			for (size_t idx = 0, end = assign.size(); idx < end; idx = idx + 2)
			{
				symbol& var_name = object_traits::cast_ref<symbol>(assign[idx]);
				ast_node& var_expr = context._type_checker(assign[idx + 1]);
				let_vars.push_back(make_pair(&var_name, &var_expr));
				context._module->add_local_variable_type(var_name._name, var_expr.type());
			}
			vector<ast_node*> body_nodes;
			for (cons_cell* body_cell = &body_start; body_cell
				; body_cell = object_traits::cast<cons_cell>(body_cell->_next))
			{
				body_nodes.push_back(&context._type_checker(body_cell->_value));
			}
			//TODO change this to null so the system understand null or nil.
			if (body_nodes.empty())
				throw runtime_error("invalid let statement");
			let_ast_node* new_node
				= context._ast_allocator->construct<let_ast_node>(context._string_table, body_nodes.back()->type());
			new_node->_let_vars = let_vars;
			for_each(body_nodes.begin(), body_nodes.end(), [&]
				(ast_node* node)
			{
				new_node->children().push_back(*node);
			});
			return new_node;
		}
	};


	struct for_loop_ast_node : public ast_node
	{
		vector<pair<symbol*, ast_node*> >	_for_vars;
		ast_node_ptr						_cond_node;

		for_loop_ast_node(string_table_ptr st, type_library_ptr lt)
			: ast_node(lt->get_void_type())
		{
		}


		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			compiler_scope_watcher _for_scoping(context);
			module::compilation_variable_scope _for_var_scope(context._module);
			let_ast_node::initialize_assign_block(context, _for_vars);
			Function* theFunction = context._builder.GetInsertBlock()->getParent();

			BasicBlock* loop_update_block = BasicBlock::Create(getGlobalContext(), "loop update", theFunction);
			BasicBlock* cond_block = BasicBlock::Create(getGlobalContext(), "cond block", theFunction);
			BasicBlock* exit_block = BasicBlock::Create(getGlobalContext(), "exit block", theFunction);
			context._builder.CreateBr(cond_block);
			context._builder.SetInsertPoint(cond_block);
			llvm_value_ptr next_val = _cond_node->compile_second_pass(context).first.get();
			context._builder.CreateCondBr(next_val, loop_update_block, exit_block);

			context._builder.SetInsertPoint(loop_update_block);
			//output looping
			for (auto iter = children().begin(), end = children().end(); iter != end; ++iter)
			{
				iter->compile_second_pass(context);
			}
			context._builder.CreateBr(cond_block);
			context._builder.SetInsertPoint(exit_block);
			return pair<llvm_value_ptr_opt, type_ref_ptr>(nullptr, &context._type_library->get_void_type());
		}
	};

	CCLJ_BASE_PLUGINS_DESTRUCT_AST_NODE(for_loop_ast_node);

	struct for_loop_plugin : public compiler_plugin
	{
		for_loop_plugin()
		{
		}
		virtual ast_node* type_check(reader_context& context, lisp::cons_cell& cell)
		{
			// for loop is an array of init statements
			// a boolean conditional, 
			// an array of update statements
			// and the rest is the for body.
			cons_cell& init_cell = object_traits::cast_ref<cons_cell>(cell._next);
			cons_cell& cond_cell = object_traits::cast_ref<cons_cell>(init_cell._next);
			cons_cell& update_cell = object_traits::cast_ref<cons_cell>(cond_cell._next);
			cons_cell& body_start = object_traits::cast_ref<cons_cell>(update_cell._next);
			object_ptr_buffer init_list = object_traits::cast_ref<array>(init_cell._value)._data;
			object_ptr_buffer update_list = object_traits::cast_ref<array>(update_cell._value)._data;
			vector<pair<symbol*, ast_node*> > init_nodes;
			module::type_check_variable_scope __for_scope(context._module);
			for (size_t idx = 0, end = init_list.size(); idx < end; idx += 2)
			{
				symbol& var_name = object_traits::cast_ref<symbol>(init_list[idx]);
				ast_node& var_expr = context._type_checker(init_list[idx + 1]);
				init_nodes.push_back(make_pair(&var_name, &var_expr));
				context._module->add_local_variable_type(var_name._name, var_expr.type());
			}
			ast_node& cond_node = context._type_checker(cond_cell._value);
			if (context._type_library->to_base_numeric_type(cond_node.type())
				!= base_numeric_types::i1)
				throw runtime_error("invalid for statement; condition not boolean");
			vector<ast_node*> body_nodes;
			for (cons_cell* body_cell = &body_start; body_cell
				; body_cell = object_traits::cast<cons_cell>(body_cell->_next))
			{
				body_nodes.push_back(&context._type_checker(body_cell->_value));
			}
			for_each(update_list.begin(), update_list.end(), [&]
				(object_ptr item)
			{
				body_nodes.push_back(&context._type_checker(item));
			});
			for_loop_ast_node* new_node
				= context._ast_allocator->construct<for_loop_ast_node>(context._string_table, context._type_library);
			new_node->_for_vars = init_nodes;
			new_node->_cond_node = &cond_node;
			for_each(body_nodes.begin(), body_nodes.end(), [&]
				(ast_node_ptr node)
			{
				new_node->children().push_back(*node);
			});
			return new_node;
		}
	};

	struct set_ast_node : public ast_node
	{
		variable_lookup_chain _chain;
		set_ast_node(const type_ref& type, const variable_lookup_chain& c) : ast_node(type), _chain(c) {}

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			pair<llvm_value_ptr_opt, type_ref_ptr> expr_result = children().begin()->compile_second_pass(context);
			if ( expr_result.first.valid() )
				context._module->store_variable(context, _chain, expr_result.first.deref());
			return expr_result;
		}
	};

	CCLJ_BASE_PLUGINS_DESTRUCT_AST_NODE(set_ast_node);

	struct set_plugin : public compiler_plugin
	{
		set_plugin(){}
		//syntax is (set target expr)
		//target->a->b->c = expr;
		virtual ast_node* type_check(reader_context& context, lisp::cons_cell& cell)
		{
			cons_cell& symbol_cell = object_traits::cast_ref<cons_cell>(cell._next);
			symbol& target = object_traits::cast_ref<symbol>(symbol_cell._value);
			vector<cons_cell*> arg_cells;
			for (cons_cell* next_cell = object_traits::cast<cons_cell>(symbol_cell._next);
				next_cell; next_cell = object_traits::cast<cons_cell>(next_cell->_next))
				arg_cells.push_back(next_cell);
			if (arg_cells.size() == 0)
				throw runtime_error("invalid number of arguments to set");

			cons_cell* expr_cell = arg_cells.back();
			arg_cells.pop_back();

			variable_lookup_chain lookup_chain;
			vector<string> split_data = base_language_plugins::split_symbol(target);
			lookup_chain.name = context._name_table->register_name(split_data);
			option<variable_lookup_typecheck_result> results = context._module->type_check_variable_access(lookup_chain);
			if (results.empty() || !results->read)
				throw runtime_error("invalid set");
			ast_node& expr_node = context._type_checker(expr_cell->_value);
			if (&expr_node.type() != results->type)
				throw runtime_error("invalid set");
			ast_node* retval = context._ast_allocator->construct<set_ast_node>(*results->type, lookup_chain);
			retval->children().push_back(expr_node);
			return retval;
		}
	};
}



void language_plugins::register_plugins(qualified_name_table_ptr name_table
	, string_plugin_map_ptr top_level_special_forms
	, string_plugin_map_ptr special_forms)
{
	auto string_table = name_table->string_table();
	special_forms->insert(make_pair(string_table->register_str("if")
		, make_shared<if_compiler_plugin>()));
	special_forms->insert(make_pair(string_table->register_str("let")
		, make_shared<let_compiler_plugin>()));
	special_forms->insert(make_pair(string_table->register_str("for")
		, make_shared<for_loop_plugin>()));
	special_forms->insert(make_pair(string_table->register_str("set")
		, make_shared<set_plugin>()));
}


