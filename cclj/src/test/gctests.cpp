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
#include "cclj/gc_array.h"

using namespace cclj;

typed_property_definition<uint32_t> g_value_def( "value" );
typed_property_definition<objref_t> g_next_def( "next" );

garbage_collector_ptr create_gc()
{
	auto str_table = string_table::create();
	auto cls_system = class_system::create( str_table );
	auto allocator = allocator::create_checking_allocator();
	auto gc = garbage_collector::create_mark_sweep( allocator, reference_tracker_ptr(), str_table, cls_system ); 
	auto test_cls_name = str_table->register_str( "simple" );
	vector<property_definition> prop_defs;
	prop_defs.push_back( g_value_def.def( str_table ) );
	prop_defs.push_back( g_next_def.def( str_table ) );
	auto cls = cls_system->create_definition( 
		test_cls_name
		, class_definition_ptr_const_buffer()
		, prop_defs );
	return gc;
}

TEST(garbage_collector_tests, basic_collection) 
{
	auto gc = create_gc();
	auto str_table = gc->string_table();
	auto cls_system = gc->class_system();
	auto test_cls_name = str_table->register_str( "simple" );
	auto cls = cls_system->find_definition( test_cls_name );
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

TEST(garbage_collector_tests, basic_dynamic_array)
{
	auto gc = create_gc();
	auto str_table = gc->string_table();
	auto cls_system = gc->class_system();
	auto test_cls_name = str_table->register_str( "simple" );
	auto cls = cls_system->find_definition( test_cls_name );
	auto value_prop( g_value_def.to_entry( *cls->find_instance_property( str_table->register_str( "value" ) ) ) );
	auto next_prop( g_next_def.to_entry( *cls->find_instance_property( str_table->register_str( "next" ) ) ) );

	gc_array test_array( gc, test_cls_name );
	test_array.insert( test_array.end(), 5 );
	ASSERT_EQ( test_array.size(), 5 );

	uint32_t index = 0;
	for ( auto iter = test_array.begin(), end = test_array.end(); iter != end; ++iter, ++index )
	{
		ASSERT_EQ( value_prop.get( *iter ), 0 );
		value_prop.set( *iter, index );
	}

	{
		gc_array temp_array( test_array );
		index = 0;
		for ( auto iter = temp_array.begin()
			, end = temp_array.end()
			, second_iter = test_array.begin(); iter != end; ++iter, ++index, ++second_iter )
		{
			ASSERT_EQ( value_prop.get( *iter ), index );
			value_prop.set( *iter, index * 2 );
			ASSERT_EQ( value_prop.get( *second_iter ), index );
		}
		next_prop.set( *(test_array.begin() + 4), temp_array.object() );
	}
	gc->perform_gc();
	ASSERT_EQ( gc->all_live_objects().size(), 2 );
	next_prop.set( *(test_array.begin() + 4), nullptr );
	gc->perform_gc();
	ASSERT_EQ( gc->all_live_objects().size(), 1 );
	test_array.erase( test_array.begin() + 2, test_array.begin() + 3 );
	ASSERT_EQ( test_array.size(), 4 );
	
	index = 0;
	for ( auto iter = test_array.begin(), end = test_array.end(); iter != end; ++iter, ++index )
	{
		if ( index < 2 )
		{
			ASSERT_EQ( value_prop.get( *iter ), index );
		}
		else
		{
			ASSERT_EQ( value_prop.get( *iter ), index + 1 );
		}
	}

	test_array.insert( test_array.begin() + 2, 2 );
	ASSERT_EQ( test_array.size(), 6 );
	
	ASSERT_EQ( value_prop.get( *(test_array.begin() + 2) ), 0 );


	//force creation of actual new data
	test_array.insert( test_array.end(), 10 );
	ASSERT_EQ( test_array.size(), 16 );
	value_prop.set( *(test_array.begin() + 15), 15 );
	ASSERT_EQ( value_prop.get( *(test_array.begin() + 15) ), 15 );
	//Now we see if cleanup cleans up the object correctly because it should have 
	//non-contiguous data.
}