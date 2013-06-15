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

using namespace cclj;

TEST(garbage_collector_tests, basic_collection) {
  
	auto str_table = string_table::create();
	auto cls_system = class_system::create( str_table );
	auto allocator = allocator::create_checking_allocator();
	auto gc = garbage_collector::create_mark_sweep( allocator, reference_tracker_ptr(), str_table, cls_system ); 
	vector<property_definition> prop_defs;
	auto test_cls_name = str_table->register_str( "simple" );
	prop_defs.push_back( property_definition( str_table->register_str( "value" ), str_table->register_str( "uint32_t" ) ) );
	prop_defs.push_back( property_definition( str_table->register_str( "next" ), str_table->register_str( "variable_size_objref_t" ) ) );
	auto cls = cls_system->create_definition( 
		test_cls_name
		, class_definition_ptr_const_buffer()
		, prop_defs );
	gc_obj_ptr root_obj( gc, gc->allocate( test_cls_name, cls->instance_size(), __FILE__, __LINE__ ) );
	auto value_prop = *cls->find_instance_property( str_table->register_str( "value" ) );
	auto next_prop = *cls->find_instance_property( str_table->register_str( "next" ) );
	uint8_t* data = reinterpret_cast<uint8_t*>( root_obj.data().first );
	uint32_t* value_ptr = reinterpret_cast<uint32_t*>( data + value_prop.offset );
	gc_object** next_ptr = reinterpret_cast<gc_object**>( data + next_prop.offset );
	*value_ptr = 5;
	{
		gc_obj_ptr next_obj( gc, gc->allocate( test_cls_name, cls->instance_size(), __FILE__, __LINE__ ) );
		*next_ptr = next_obj.object();
		uint8_t* next_data = reinterpret_cast<uint8_t*>( next_obj.data().first );
		uint32_t* next_value_ptr = reinterpret_cast< uint32_t*>( next_data + value_prop.offset );
		*next_value_ptr = 6;
	}
	gc->perform_gc();
	auto all_objects = gc->all_live_objects();
	ASSERT_EQ( all_objects.size(), (size_t)2 );

	gc->perform_gc();
	all_objects = gc->all_live_objects();
	ASSERT_EQ( all_objects.size(), (size_t)2 );


	*next_ptr = nullptr;
	//Ensure the gc can follow pointers and correctly handles class types.
	gc->perform_gc();
	all_objects = gc->all_live_objects();
	ASSERT_EQ( all_objects.size(), 1 );
	{
		gc_obj_ptr test_ptr( gc, *all_objects[0] );
		data = reinterpret_cast<uint8_t*>( test_ptr.data().first );
		uint32_t* test_value_ptr = reinterpret_cast<uint32_t*>( data + value_prop.offset );
		ASSERT_EQ( (uint32_t)5, *test_value_ptr );
	}
}