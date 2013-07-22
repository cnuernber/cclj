//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/plugins/compiler_plugin.h"

using namespace cclj;
using namespace cclj::lisp;


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