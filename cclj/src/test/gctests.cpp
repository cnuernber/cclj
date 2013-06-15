//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/string_table.h"
#include "cclj/garbage_collector.h"
#include "cclj/allocator.h"
#include "cclj/typed_properties.h"

using namespace cclj;

typed_property_definition<uint32_t> g_value_def( "value" );
typed_property_definition<objref_t> g_next_def( "next" );


TEST(garbage_collector_tests, basic_collection) {
  
	auto str_table = string_table::create();
	auto cls_system = class_system::create( str_table );
	auto allocator = allocator::create_checking_allocator();
	auto gc = garbage_collector::create_mark_sweep( allocator, reference_tracker_ptr(), str_table, cls_system ); 
	vector<property_definition> prop_defs;
	auto test_cls_name = str_table->register_str( "simple" );
	prop_defs.push_back( g_value_def.def( str_table ) );
	prop_defs.push_back( g_next_def.def( str_table ) );
	auto cls = cls_system->create_definition( 
		test_cls_name
		, class_definition_ptr_const_buffer()
		, prop_defs );
	gc_obj_ptr root_obj( gc, gc->allocate( test_cls_name, cls->instance_size(), __FILE__, __LINE__ ) );
	auto value_prop( g_value_def.to_entry( *cls->find_instance_property( str_table->register_str( "value" ) ) ) );
	auto next_prop( g_next_def.to_entry( *cls->find_instance_property( str_table->register_str( "next" ) ) ) );
	value_prop.set( root_obj, 5 );
	{
		gc_obj_ptr next_obj( gc, gc->allocate( test_cls_name, cls->instance_size(), __FILE__, __LINE__ ) );
		next_prop.set( root_obj, next_obj.object() );
		value_prop.set( next_obj, 6 );
	}

	gc->perform_gc();
	auto all_objects = gc->all_live_objects();
	ASSERT_EQ( all_objects.size(), (size_t)2 );

	gc->perform_gc();
	all_objects = gc->all_live_objects();
	ASSERT_EQ( all_objects.size(), (size_t)2 );


	next_prop.set( root_obj, nullptr );
	//Ensure the gc can follow pointers and correctly handles class types.
	gc->perform_gc();
	all_objects = gc->all_live_objects();
	ASSERT_EQ( all_objects.size(), 1 );
	{
		gc_obj_ptr test_ptr( gc, *all_objects[0] );
		uint32_t test_value = value_prop.get( test_ptr );
		ASSERT_EQ( (uint32_t)5, test_value );
	}
}