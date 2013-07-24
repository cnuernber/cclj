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

