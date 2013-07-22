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
}

namespace cclj
{
	typedef llvm::Value* llvm_value_ptr;
	typedef llvm::AllocaInst* llvm_alloca_ptr;
	typedef llvm::IRBuilder<> llvm_builder;
	typedef llvm::Type* llvm_type_ptr;
	typedef unordered_map<string_table_str, type_ref_ptr> symbol_type_ref_map;
	typedef unordered_map<type_ref_ptr, llvm_type_ptr> type_llvm_type_map;
	//tool used during type checking.
#pragma warning(disable:4512)
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
			for_each( _created_symbols.begin(), _created_symbols.end(), [this]
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
	
	typedef unordered_map<string_table_str, pair<llvm_alloca_ptr,type_ref_ptr> > string_alloca_type_map;
	typedef unordered_map<type_ref_ptr, ast_node_ptr> type_ast_node_map;
	typedef shared_ptr<type_ast_node_map> type_ast_node_map_ptr;

	struct variable_context
	{
		string_alloca_type_map&													_variables;
		vector<pair<string_table_str, pair<llvm_alloca_ptr, type_ref_ptr> > >	_added_vars;
		variable_context( string_alloca_type_map& vars ) : _variables( vars ) {}
		~variable_context()
		{
			for_each( _variables.begin(), _variables.end(), [this]
			( const pair<string_table_str, pair<llvm_alloca_ptr, type_ref_ptr> >& var )
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
			pair<llvm_alloca_ptr, type_ref_ptr> old_value( nullptr, nullptr );
			if ( inserter.second == false )
			{
				old_value = inserter.first->second;
				inserter.first->second = make_pair( alloca, &type );
			}
			_added_vars.push_back( make_pair( name, old_value ) );
		}
	};

	struct compiler_context
	{
		type_library_ptr			_type_library;
		type_ast_node_map_ptr		_symbol_map;
		llvm_builder				_builder;
		string_alloca_type_map		_variables;
		type_llvm_type_map			_type_map;
		llvm::Module&				_module;
		llvm::FunctionPassManager&	_fpm;
		compiler_context();
		llvm_type_ptr type_ref_type( type_ref& type );
	};

	struct reader_context;

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

		virtual void compile_first_pass(compiler_context& /*context*/) {}
		virtual pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context& context) = 0;
	};

	inline ast_node_ptr ast_node_next_op::get( ast_node& s ) { return s.next_node(); }
	inline const ast_node* ast_node_next_op::get( const ast_node& s ) const { return s.next_node(); }
	inline void ast_node_next_op::set( ast_node& item, ast_node_ptr next ) { item.set_next_node( next ); }

	typedef vector<ast_node_ptr> ast_node_ptr_list;

	typedef shared_ptr<slab_allocator<> > slab_allocator_ptr;

	typedef function<ast_node& (lisp::cons_cell&)> type_check_function;
	class compiler_plugin;
	typedef shared_ptr<compiler_plugin> compiler_plugin_ptr;

	typedef unordered_map<string_table_str, compiler_plugin_ptr> string_plugin_map;
	typedef shared_ptr<string_plugin_map> string_plugin_map_ptr;
	 
	struct reader_context
	{
		lisp::factory_ptr			_factory;
		type_library_ptr			_type_library;
		string_table_ptr			_string_table;
		slab_allocator_ptr			_ast_allocator;
		symbol_type_ref_map			_context_symbol_types;
		type_ast_node_map_ptr		_symbol_map;
		type_check_function			_type_checker;
		string_plugin_map_ptr		_special_forms;
		string_plugin_map_ptr		_top_level_special_forms;
		reader_context( allocator_ptr alloc, lisp::factory_ptr f, type_library_ptr l
							, string_table_ptr st, type_check_function tc
							, string_plugin_map_ptr special_forms
							, string_plugin_map_ptr top_level_special_forms
							, type_ast_node_map_ptr top_level_symbols
							, slab_allocator_ptr ast_alloc );
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
		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell ) = 0;
	};

	typedef shared_ptr<compiler_plugin> compiler_plugin_ptr;

	struct compiler_plugin_traits
	{
		template<typename plugin_type>
		static plugin_type* cast( compiler_plugin* plugin, string_table_ptr str_table )
		{
			if ( !plugin ) return nullptr;
			if ( str_table->register_str( plugin_type::static_plugin_name() ) == plugin->plugin_name() )
				return static_cast<plugin_type*>( plugin );
			return nullptr
		}

		template<typename plugin_type>
		static plugin_type& cast_ref( compiler_plugin* plugin, string_table_ptr str_table )
		{
			plugin_type* retval = cast<plugin_type>( plugin, str_table );
			if ( retval ) return *retval;

			throw runtime_error( "bad cast" );
		}

		template<typename ast_type>
		static ast_type* cast( ast_node_ptr node, string_table_ptr str_table )
		{
			if ( !node ) return nullptr;
			if ( str_table->register_str( ast_type::static_node_type() ) 
				== node->plugin().plugin_name() )
				return static_cast<ast_type*>( node );
			return nullptr;
		}
		
		template<typename ast_type>
		static ast_type& cast_ref( ast_node_ptr node, string_table_ptr str_table )
		{
			ast_type* retval = cast<ast_type>( node, str_table );
			if ( retval ) return *retval;
			throw runtime_error( "bad cast" );
		}
	};
}

#endif