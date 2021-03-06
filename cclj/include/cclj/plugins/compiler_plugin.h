//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_COMPILER_PLUGIN_H
#define CCLJ_COMPILER_PLUGIN_H
#pragma once
#include "cclj/cclj.h"
#include "cclj/noncopyable.h"
#include "cclj/string_table.h"
#include "cclj/type_library.h"
#include "cclj/lisp_types.h"
#include "cclj/slab_allocator.h"
#include "cclj/invasive_list.h"
#include "cclj/option.h"
#include "cclj/qualified_name_table.h"

#ifdef _WIN32
#pragma warning(push,2)
#endif
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/GlobalValue.h"
#ifdef _WIN32
#pragma warning(pop)
#endif


namespace llvm
{
	namespace legacy
	{
		class FunctionPassManager;
	}
	class Value;
	class AllocaInst;
	class Type;
	class Module;
	class ExecutionEngine;
	class BasicBlock;
}

namespace cclj
{
	typedef llvm::Value* llvm_value_ptr;
	typedef ptr_option<llvm::Value> llvm_value_ptr_opt;
	typedef llvm::AllocaInst* llvm_alloca_ptr;
	typedef llvm::IRBuilder<> llvm_builder;
	typedef llvm::Type* llvm_type_ptr;
	typedef ptr_option<llvm::Type> llvm_type_ptr_opt;
	typedef unordered_map<string_table_str, type_ref_ptr> symbol_type_ref_map;
	typedef unordered_map<type_ref_ptr, llvm_type_ptr> type_llvm_type_map;
	//tool used during type checking.
#ifdef _WIN32
#pragma warning(disable:4512)
#endif


	struct visibility
	{
		enum _enum
		{
			internal_visiblity = 0,
			external_visibility,
		};
	};

	struct symbol_type_context : noncopyable
	{
		symbol_type_ref_map&							_context_symbol_types;
		vector<pair<string_table_str, type_ref_ptr> >	_created_symbols;
		symbol_type_context( symbol_type_ref_map& cst )
			: _context_symbol_types( cst )
		{
		}
		~symbol_type_context()
		{
			for_each( _created_symbols.rbegin(), _created_symbols.rend(), [this]
			( const pair<string_table_str, type_ref_ptr>& symbol )
			{
				if ( symbol.second )
					_context_symbol_types[symbol.first] = symbol.second;
				else
					_context_symbol_types.erase( symbol.first );
			});
		}

		void add_symbol( string_table_str name, type_ref& type )
		{
			pair<symbol_type_ref_map::iterator,bool> inserter 
				= _context_symbol_types.insert( make_pair( name, &type ) );
			type_ref_ptr old_type = nullptr;
			if ( inserter.second == false )
			{
				//record the old type before overwriting
				old_type = inserter.first->second;
				inserter.first->second = &type;
			}
			//Record the old type to restore when this context is destroyed.
			_created_symbols.push_back( make_pair( name, old_type ) );
		}
	};

	class ast_node;
	typedef ast_node* ast_node_ptr;
	class compiler_plugin;
	typedef shared_ptr<compiler_plugin> compiler_plugin_ptr;
	
	typedef unordered_map<string_table_str, pair<llvm_value_ptr_opt,type_ref_ptr> > string_alloca_type_map;
	typedef unordered_map<type_ref_ptr, ast_node_ptr> type_ast_node_map;
	typedef shared_ptr<type_ast_node_map> type_ast_node_map_ptr;

	struct compiler_scope
	{
		vector<llvm::BasicBlock*> _blocks;
		void add_block( llvm::BasicBlock& block )
		{
			_blocks.push_back( &block );
		}
	};


	typedef vector<compiler_scope> compiler_scope_list;

	class user_compiler_data
	{
	protected:
		virtual ~user_compiler_data(){}
	public:
		friend class shared_ptr<user_compiler_data>;
	};

	typedef shared_ptr<user_compiler_data> user_compiler_data_ptr;

	typedef unordered_map<string_table_str, user_compiler_data_ptr> string_compiler_data_map;

	class module;


	struct compiler_context
	{
		llvm::Module&				_llvm_module;
		llvm::legacy::FunctionPassManager&	_fpm;
		llvm::ExecutionEngine&		_eng;
		type_library_ptr			_type_library;
		llvm_builder				_builder;
		type_llvm_type_map			_type_map;
		qualified_name_table_ptr	_name_table;
		shared_ptr<module>			_module;
		compiler_scope_list			_scopes;
		string_compiler_data_map	_user_compiler_data;
		stringstream				_name_buffer;

		compiler_context( type_library_ptr tl
							, qualified_name_table_ptr name_table
							, shared_ptr<module> module
							, llvm::Module& llvm_m,  llvm::legacy::FunctionPassManager& fpm
							, llvm::ExecutionEngine& eng );


		llvm_type_ptr_opt type_ref_type( type_ref& type );
		void enter_scope();
		void add_exit_block( llvm::BasicBlock& block );
		void exit_scope();
		//uses the name buffer, so this is not safe to call in a reentrant context.

		string qualified_name_to_llvm_name(qualified_name nm);
		string qualified_name_to_llvm_name(qualified_name nm, type_ref_ptr_buffer specializations);
		llvm::GlobalValue::LinkageTypes visibility_to_linkage(visibility::_enum vis);
	};
	
	struct compiler_scope_watcher
	{
		compiler_context& _context;
		compiler_scope_watcher( compiler_context& ctx )
			: _context( ctx )
		{
			_context.enter_scope();
		}
		~compiler_scope_watcher()
		{
			_context.exit_scope();
		}
	};

	struct reader_context;

	struct value_accessor
	{
	protected:
		virtual ~value_accessor(){}
	public:
		virtual type_ref& value_type() = 0;
		virtual llvm_value_ptr_opt get_value( compiler_context& context ) = 0;
		virtual void set_value( compiler_context& context, llvm_value_ptr_opt value ) = 0;
	};


	CCLJ_DEFINE_INVASIVE_SINGLE_LIST(ast_node); 
	//AST nodes are allocated with the slab allocator.  This means they do not need
	//to be manually deallocated.
	class ast_node : noncopyable
	{
		ast_node_ptr			_next_node;
		type_ref&				_type;
		ast_node_list			_children;
	protected:
		virtual ~ast_node(){}
	public:
		ast_node(const type_ref& t ) :  _next_node( nullptr )
			, _type( const_cast<type_ref&> ( t ) )
		{}
		virtual void set_next_node( ast_node* n ) { _next_node = n; }
		virtual ast_node_ptr next_node() { return _next_node; }
		virtual ast_node_ptr next_node() const { return _next_node; }
		ast_node_list& children() { return _children; }
		const ast_node_list& children() const { return _children; }
		virtual type_ref& type() { return _type; }
		virtual const type_ref& type() const { return _type; }
		//return true if you can be executed at the top level.
		virtual bool executable_statement() const { return false; }

		virtual uint32_t eval_to_uint32(type_library& ) const 
		{ 
			throw runtime_error("ast node not capable of evaluation to uint32_t"); 
		}
		 
		//Not all nodes are callback, but if your node is registered as a global symbol in the type
		//table then it needs to be able to handle this call.
		virtual ast_node& apply( reader_context& /*context*/, data_buffer<ast_node_ptr> /*args*/ )
		{
			throw runtime_error( "ast node cannot handle apply" );
		}

		virtual void compile_first_pass(compiler_context& /*context*/) {}
		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context) = 0;
	};

	inline ast_node_ptr ast_node_next_op::get( ast_node& s ) { return s.next_node(); }
	inline const ast_node* ast_node_next_op::get( const ast_node& s ) const { return s.next_node(); }
	inline void ast_node_next_op::set( ast_node& item, ast_node_ptr next ) { item.set_next_node( next ); }

	typedef vector<ast_node_ptr> ast_node_ptr_list;

	typedef shared_ptr<slab_allocator<> > slab_allocator_ptr;

	typedef function<ast_node& (lisp::object_ptr)> type_check_function;
	typedef function<type_ref& (lisp::cons_cell&)> type_eval_function;
	class compiler_plugin;
	typedef shared_ptr<compiler_plugin> compiler_plugin_ptr;

	typedef unordered_map<string_table_str, compiler_plugin_ptr> string_plugin_map;
	typedef shared_ptr<string_plugin_map> string_plugin_map_ptr;

	

	typedef unordered_map<string_table_str, lisp::object_ptr> string_obj_ptr_map;

	struct preprocess_symbol_context
	{
		string_obj_ptr_map& _symbols;
		vector<pair<string_table_str, lisp::object_ptr> > _added_symbols;
		preprocess_symbol_context( string_obj_ptr_map& s )
			: _symbols( s )
		{
		}
		~preprocess_symbol_context()
		{
			for_each( _added_symbols.rbegin(), _added_symbols.rend(), [&]
			( pair<string_table_str, lisp::object_ptr> item )
			{
				if ( item.second )
					_symbols[item.first] = item.second;
				else
					_symbols.erase( item.first );
			} );
		}
		void add_symbol( string_table_str name, lisp::object& val )
		{
			pair<string_obj_ptr_map::iterator,bool> inserter = _symbols.insert( make_pair( name, &val ) );
			if ( inserter.second == false )
			{
				_added_symbols.push_back( *inserter.first );
				inserter.first->second = &val;
			}
		}
	};

	class lisp_evaluator
	{
	protected:
		virtual ~lisp_evaluator(){}
	public:
		friend class shared_ptr<lisp_evaluator>;
		virtual lisp::object_ptr eval( reader_context& context, lisp::cons_cell& callsite ) = 0;
	};

	typedef shared_ptr<lisp_evaluator> lisp_evaluator_ptr;

	typedef unordered_map<string_table_str,lisp_evaluator_ptr> string_lisp_evaluator_map; 
	 
	struct reader_context
	{
		lisp::factory_ptr			_factory;
		type_library_ptr			_type_library;
		string_table_ptr			_string_table;
		slab_allocator_ptr			_ast_allocator;
		type_check_function			_type_checker;
		type_eval_function			_type_evaluator;
		string_plugin_map_ptr		_special_forms;
		string_plugin_map_ptr		_top_level_special_forms;
		string_obj_ptr_map			_preprocessor_symbols;
		string_lisp_evaluator_map	_preprocessor_evaluators;
		qualified_name_table_ptr	_name_table;
		shared_ptr<module>			_module;

		reader_context( allocator_ptr alloc, lisp::factory_ptr f, type_library_ptr l
							, string_table_ptr st, type_check_function tc
							, type_eval_function te
							, string_plugin_map_ptr special_forms
							, string_plugin_map_ptr top_level_special_forms
							, slab_allocator_ptr ast_alloc
							, string_lisp_evaluator_map& lisp_evals
							, qualified_name_table_ptr name_table
							, shared_ptr<module> module);

		type_ref& symbol_type( lisp::symbol& symbol );
	};

	//Compiler plugins process special forms.
	class compiler_plugin
	{
	protected:
		virtual ~compiler_plugin(){}

	public:
		compiler_plugin() {}
		friend class shared_ptr<compiler_plugin>;
		//vast majority of compiler plugins operate at this level, transforming the lisp
		//ast into the compiler ast.
		virtual ast_node* type_check( reader_context& context, lisp::cons_cell& cell ) = 0;
	};

	typedef shared_ptr<compiler_plugin> compiler_plugin_ptr;
}

#endif
