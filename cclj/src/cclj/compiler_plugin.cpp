//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/plugins/compiler_plugin.h"
#include "cclj/llvm_base_numeric_type_helper.h"
#include "cclj/variant.h"
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


using namespace cclj;
using namespace cclj::lisp;
using namespace llvm;

compiler_context::compiler_context( type_library_ptr tl, type_ast_node_map_ptr _type_node_map
					, llvm::Module& m,  llvm::FunctionPassManager& fpm )
	: _module( m )
	, _fpm( fpm )
	, _type_library( tl )
	, _symbol_map( _type_node_map )
	, _builder( getGlobalContext() )
{
}


llvm_type_ptr compiler_context::type_ref_type( type_ref& type )
{
	pair<type_llvm_type_map::iterator,bool> inserter = _type_map.insert( make_pair( &type, nullptr ) );
	if ( inserter.second == false )
		return inserter.first->second;
	//else we inserted something, so we need to ensure it is valid.
	base_numeric_types::_enum val = _type_library->to_base_numeric_type( type );
	llvm_type_ptr base_type = nullptr;
	switch( val )
	{
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name )					\
	case base_numeric_types::name: base_type					\
		= llvm_helper::llvm_constant_map<base_numeric_types::name>::type(); break;
		CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
	default:
		throw runtime_error( "unable to find type" );
	}
	if ( base_type == nullptr ) throw runtime_error( "unable to find type" );
	inserter.first->second = base_type;
	return base_type;
}


reader_context::reader_context( allocator_ptr alloc, lisp::factory_ptr f, type_library_ptr l
					, string_table_ptr st, type_check_function tc
						, string_plugin_map_ptr special_forms
						, string_plugin_map_ptr top_level_special_forms
						, type_ast_node_map_ptr top_level_symbols
						, slab_allocator_ptr ast_alloc )
	: _factory( f )
	, _type_library( l )
	, _string_table( st )
	, _ast_allocator( ast_alloc )
	, _symbol_map( top_level_symbols )
	, _type_checker( tc )
	, _special_forms( special_forms )
	, _top_level_special_forms( top_level_special_forms )
{
}



namespace cclj
{
	typedef value_accessor* value_accessor_ptr;
	template<> struct variant_destruct<value_accessor*> { 
		void operator()( value_accessor_ptr)
		{
		}
	};
}

namespace
{
	typedef vector<uint32_t> uint32_list;

	struct sym_res_types
	{
		enum _enum
		{
			unknown_sym_res_type = 0,
			gep,
			accessor,
		};
	};

	template<typename dtype>
	struct sym_res_type_map {};
	template<> struct sym_res_type_map<uint32_list> 
	{ static sym_res_types::_enum type() { return sym_res_types::gep; } };
	
	template<> struct sym_res_type_map<value_accessor_ptr> 
	{ static sym_res_types::_enum type() { return sym_res_types::accessor; } };

	struct sym_res_var_traits
	{
		typedef sym_res_types::_enum id_type;
		enum {
			buffer_size = sizeof(uint32_list),
		};
		static sym_res_types::_enum empty_type() { return sym_res_types::unknown_sym_res_type; }
		template<typename dtype>
		static sym_res_types::_enum typeof() { return sym_res_type_map<dtype>::type(); }

		template<typename rettype, typename visitor>
		static rettype do_visit( char* data, sym_res_types::_enum type, visitor v )
		{
			switch( type )
			{
			case sym_res_types::gep: return v( *reinterpret_cast<uint32_list*>( data ) );
			case sym_res_types::accessor: return v( *reinterpret_cast<value_accessor_ptr*>( data ) );
			}
			throw runtime_error( "failed to visit type" );
		}
	};

	typedef variant_traits_impl<sym_res_var_traits> sym_res_traits_type;



	typedef variant<sym_res_traits_type> sym_res_var;

	typedef vector<sym_res_var> sym_res_var_list;

	struct symbol_resolution_context_impl : public symbol_resolution_context
	{
		sym_res_var_list _data;
		type_ref_ptr	 _final_type;
		string_table_str _initial_symbol;

		symbol_resolution_context_impl( string_table_str sym )
			: _initial_symbol( sym )
		{
		}

		virtual string_table_str initial_symbol(){ return _initial_symbol; }
		
		//provided by the AST plugins
		virtual void add_GEP_index( uint32_t idx, type_ref& type )
		{
			_final_type = &type;
			if ( _data.empty() || _data.back().type() != sym_res_types::gep )
			{
				_data.push_back( sym_res_var( uint32_list() ) );
			}
			_data.back().data<uint32_list>().push_back( idx );
		}

		//obviously the accessor will need to last the lifetime of the compiler system.
		virtual void add_value_accessor( value_accessor& accessor )
		{
			_final_type = &accessor.value_type();
			_data.push_back( sym_res_var( &accessor ) );
		}

		//used by the system.
		virtual type_ref& resolved_type() 
		{ 
			if ( _final_type == nullptr ) throw runtime_error( "failed to resolve type" );
			return *_final_type;
		}

		virtual llvm_value_ptr load( compiler_context& context )
		{
			if ( _data.empty() ) throw runtime_error( "invalid variable reference" );
			if ( _data.size() > 1 || _data.back().type() != sym_res_types::gep )
				throw runtime_error( "value accessors not supported yet" );

			auto sym_iter = context._variables.find( _initial_symbol );
			if ( sym_iter == context._variables.end() ) throw runtime_error( "failed to resolve initial symbol" );

			llvm_value_ptr start_addr = sym_iter->second.first;
			uint32_list& lookup_chain( _data.back().data<uint32_list>() );
			vector<llvm::Value*> lookups;
			llvm_type_ptr int_type = llvm::Type::getInt32Ty( getGlobalContext() );
			for_each( lookup_chain.begin(), lookup_chain.end(), [&]
			( uint32_t idx )
			{
				lookups.push_back( llvm::ConstantInt::get( int_type, idx ) );
			} );
			llvm_value_ptr addr = context._builder.CreateGEP( start_addr, lookups, "gep lookup" );
			return context._builder.CreateLoad( addr );
		}
		virtual void store( compiler_context& /*context*/, llvm_value_ptr /*val*/ )
		{
			if ( _data.empty() ) throw runtime_error( "invalid variable reference" );
			if ( _data.size() > 1 || _data.back().type() != sym_res_types::gep )
				throw runtime_error( "value accessors not supported yet" );
		}
	};
}


symbol_resolution_context_ptr symbol_resolution_context::create(string_table_str initial_symbol)
{
	return make_shared<symbol_resolution_context_impl>( initial_symbol );
}