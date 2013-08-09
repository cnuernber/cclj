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

#ifdef _WIN32
#pragma warning(push,2)
#endif
#include "llvm/IR/IRBuilder.h"
#ifdef _WIN32
#pragma warning(pop)
#endif


namespace llvm
{
	class Value;
	class AllocaInst;
	class Type;
	class Module;
	class FunctionPassManager;
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

	struct variable_context
	{
		string_alloca_type_map&													_variables;
		vector<pair<string_table_str, pair<llvm_value_ptr_opt, type_ref_ptr> > >	_added_vars;
		variable_context( string_alloca_type_map& vars ) : _variables( vars ) {}
		~variable_context()
		{
			for_each( _variables.rbegin(), _variables.rend(), [this]
			( const pair<string_table_str, pair<llvm_value_ptr_opt, type_ref_ptr> >& var )
			{
				if ( var.second.first )
					_variables[var.first] = var.second;
				else
					_variables.erase( var.first );
			});
		}
		
		void add_variable( string_table_str name, llvm_alloca_ptr alloca, type_ref& type )
		{
			auto inserter = _variables.insert( make_pair( name, make_pair( alloca, &type ) ) );
			pair<llvm_value_ptr_opt, type_ref_ptr> old_value( nullptr, nullptr );
			if ( inserter.second == false )
			{
				old_value = inserter.first->second;
				inserter.first->second = make_pair( alloca, &type );
			}
			_added_vars.push_back( make_pair( name, old_value ) );
		}
	};

	struct compiler_scope
	{
		vector<llvm::BasicBlock*> _blocks;
		void add_block( llvm::BasicBlock& block )
		{
			_blocks.push_back( &block );
		}
	};


	typedef vector<compiler_scope> compiler_scope_list;

	struct compiler_context
	{
		llvm::Module&				_module;
		llvm::FunctionPassManager&	_fpm;
		llvm::ExecutionEngine&		_eng;
		type_library_ptr			_type_library;
		type_ast_node_map_ptr		_symbol_map;
		llvm_builder				_builder;
		string_alloca_type_map		_variables;
		type_llvm_type_map			_type_map;
		compiler_scope_list			_scopes;

		compiler_context( type_library_ptr tl, type_ast_node_map_ptr _type_node_map
							, llvm::Module& m,  llvm::FunctionPassManager& fpm
							, llvm::ExecutionEngine& eng );


		llvm_type_ptr_opt type_ref_type( type_ref& type );
		void enter_scope();
		void add_exit_block( llvm::BasicBlock& block );
		void exit_scope();
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

	//intermediary to allow arbitrary symbol resolution via the ast plugin system.
	struct symbol_resolution_context
	{
	protected:
		virtual ~symbol_resolution_context(){}
	public:
		friend class shared_ptr<symbol_resolution_context>;

		virtual string_table_str initial_symbol() = 0;
		//provided by the AST plugins
		virtual void add_GEP_index( uint32_t idx, type_ref& type ) = 0;
		//obviously the accessor will need to last the lifetime of the compiler system.
		virtual void add_value_accessor( value_accessor& accessor ) = 0; 


		//used by the system.
		virtual type_ref& resolved_type() = 0;
		virtual llvm_value_ptr_opt load( compiler_context& context, data_buffer<llvm_value_ptr> indexes ) = 0;
		virtual void store( compiler_context& context, data_buffer<llvm_value_ptr> indexes, llvm_value_ptr_opt val ) = 0;

		static shared_ptr<symbol_resolution_context> create(string_table_str initial_symbol, type_ref& initial_type);
	};

	typedef shared_ptr<symbol_resolution_context> symbol_resolution_context_ptr;

	CCLJ_DEFINE_INVASIVE_SINGLE_LIST(ast_node); 
	//AST nodes are allocated with the slab allocator.  This means they do not need
	//to be manually deallocated.
	class ast_node : noncopyable
	{
		string_table_str		_node_type;
		ast_node_ptr			_next_node;
		type_ref&				_type;
		ast_node_list			_children;
	protected:
		virtual ~ast_node(){}
	public:
		ast_node(string_table_str nt, const type_ref& t ) : _node_type( nt ), _next_node( nullptr )
			, _type( const_cast<type_ref&> ( t ) )
		{}
		virtual string_table_str& node_type() { return _node_type; }
		virtual void set_next_node( ast_node* n ) { _next_node = n; }
		virtual ast_node_ptr next_node() { return _next_node; }
		virtual ast_node_ptr next_node() const { return _next_node; }
		ast_node_list& children() { return _children; }
		const ast_node_list& children() const { return _children; }
		virtual type_ref& type() { return _type; }
		//return true if you can be executed at the top level.
		virtual bool executable_statement() const { return false; }

		 
		//Not all nodes are callback, but if your node is registered as a global symbol in the type
		//table then it needs to be able to handle this call.
		virtual ast_node& apply( reader_context& /*context*/, data_buffer<ast_node_ptr> /*args*/ )
		{
			throw runtime_error( "ast node cannot handle apply" );
		}

		//Called to allow the ast node to resolve the rest of a symbol when the symbol's first item pointed
		//to a variable if this node type.  Used for chained lookups of the type a.b.c.d regardless of if
		//getting or setting.  This allows AST nodes to provide custom get/set code for properties.
		virtual void resolve_symbol( reader_context& /*context*/
											, string_table_str /*split_symbol*/
											, symbol_resolution_context& /*resolution_context*/ )
		{
			throw runtime_error( "ast node cannot resolve symbol" );
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
		symbol_type_ref_map			_context_symbol_types;
		type_ast_node_map_ptr		_symbol_map;
		type_check_function			_type_checker;
		type_eval_function			_type_evaluator;
		string_plugin_map_ptr		_special_forms;
		string_plugin_map_ptr		_top_level_special_forms;
		string_obj_ptr_map			_preprocessor_symbols;
		vector<ast_node_ptr>		_additional_top_level_nodes;
		string_lisp_evaluator_map	_preprocessor_evaluators;

		reader_context( allocator_ptr alloc, lisp::factory_ptr f, type_library_ptr l
							, string_table_ptr st, type_check_function tc
							, type_eval_function te
							, string_plugin_map_ptr special_forms
							, string_plugin_map_ptr top_level_special_forms
							, type_ast_node_map_ptr top_level_symbols
							, slab_allocator_ptr ast_alloc
							, string_lisp_evaluator_map& lisp_evals );

		type_ref& symbol_type( lisp::symbol& symbol );
	};

	//Compiler plugins process special forms.
	class compiler_plugin
	{
		string_table_str _plugin_name;
	protected:
		virtual ~compiler_plugin(){}

	public:
		compiler_plugin( string_table_str pname ) : _plugin_name( pname ) {}
		friend class shared_ptr<compiler_plugin>;
		virtual string_table_str plugin_name() { return _plugin_name; }
		//vast majority of compiler plugins operate at this level, transforming the lisp
		//ast into the compiler ast.
		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell ) = 0;
	};

	typedef shared_ptr<compiler_plugin> compiler_plugin_ptr;

	
	struct global_function_entry
	{
		void*			_fn_entry;
		type_ref_ptr	_ret_type;
		type_ref_ptr	_fn_type;
		llvm::Function*		_function;
		global_function_entry()
			: _fn_entry( nullptr )
			, _ret_type( nullptr )
			, _fn_type( nullptr )
			, _function( nullptr )
		{
		}

		global_function_entry( void* fn, type_ref& ret_type, type_ref& fn_type )
			: _fn_entry( fn )
			, _ret_type( &ret_type )
			, _fn_type( &fn_type )
			, _function( nullptr )
		{
		}
	};
}

#endif
