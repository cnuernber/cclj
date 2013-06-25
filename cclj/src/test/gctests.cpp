//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/garbage_collector.h"
#include "cclj/allocator.h"
#include "cclj/gc_array.h"

using namespace cclj;

struct simple_struct
{
	float							value;
	gc_refcount_ptr<gc_object>		next;
	simple_struct() : value( 0 ) {}
};


namespace cclj
{
	template<>
	class gc_static_traits<simple_struct>
	{
	public:
		
		static uint32_t object_reference_count() { return 1; }
		static gc_object* mark_references( simple_struct& obj_type, mark_buffer& buffer )
		{
			buffer.mark( obj_type.next );
			return nullptr;
		}
	};

	template<>
	class gc_array_traits<simple_struct> 
		: public default_pod_traits<simple_struct>
		, public gc_static_traits<simple_struct>
	{
	public:
		static uint8_t alignment() { return sizeof( void* ); }
	};
};


struct simple_struct_obj : public gc_object
{
	float									value;
	gc_refcount_ptr<simple_struct_obj>		next;
	simple_struct_obj() : value( 0 ) {}
	//Return the objects referenced my this gc object.  May be called several times in succession.
	//Index will always be linearly incrementing or zero.
	virtual void mark_references( mark_buffer& buffer )
	{
		buffer.mark( next );
	}
	static object_constructor create_constructor()
	{
		return []( uint8_t* mem, size_t ) { 
			simple_struct_obj* retval = new (mem)simple_struct_obj();
			return retval;
		};
	}
};


garbage_collector_ptr create_gc()
{
	auto allocator = allocator::create_checking_allocator();
	auto gc = garbage_collector::create_mark_sweep( allocator ); 
	return gc;
}

TEST(garbage_collector_tests, basic_collection) 
{
	auto gc = create_gc();

	simple_struct_obj& root = static_cast<simple_struct_obj&>( 
		gc->allocate_object( sizeof( simple_struct_obj )
		, 8
		, simple_struct_obj::create_constructor()
		, CCLJ_IMMEDIATE_FILE_INFO() ) );
	
	gc_lock_ptr<simple_struct_obj> __root_lock( gc, root );
	root.value = 5.0f;
	{
		simple_struct_obj& next = static_cast<simple_struct_obj&>( 
		gc->allocate_object( sizeof( simple_struct_obj )
		, 8
		, simple_struct_obj::create_constructor()
		, CCLJ_IMMEDIATE_FILE_INFO() ) );
		next.value = 6;
		root.next = &next;
	}

	gc->perform_gc();
	auto all_objects = gc->all_objects();
	ASSERT_EQ( all_objects.size(), (size_t)2 );

	gc->perform_gc();
	all_objects = gc->all_objects();
	ASSERT_EQ( all_objects.size(), (size_t)2 );

	root.next = nullptr;
	//Ensure the gc can follow pointers and correctly handles class types.
	gc->perform_gc();
	all_objects = gc->all_objects();
	ASSERT_EQ( all_objects.size(), 1 );
	{
		simple_struct_obj* test_ptr( static_cast<simple_struct_obj*>( all_objects[0] ) );
		ASSERT_EQ( (uint32_t)5, test_ptr->value );
	}
}


TEST(garbage_collector_tests, basic_dynamic_array)
{
	auto gc = create_gc();
	typedef gc_array<simple_struct>  simple_struct_array;
	typedef simple_struct_array::this_ptr_type simple_struct_array_ptr;

	simple_struct_array_ptr test_array( simple_struct_array::create( gc, CCLJ_IMMEDIATE_FILE_INFO() ) );
	

	test_array->insert( test_array->end(), 5 );

	ASSERT_EQ( test_array->size(), 5 );


	uint32_t index = 0;
	for ( auto iter = test_array->begin(), end = test_array->end(); iter != end; ++iter, ++index )
	{
		ASSERT_EQ( iter->value, 0 );
		iter->value = (float)index;
	}

	{
		simple_struct_array_ptr temp_array( simple_struct_array::create( gc, CCLJ_IMMEDIATE_FILE_INFO() ) );
		*temp_array = *test_array;
		index = 0;
		for ( auto iter = temp_array->begin()
			, end = temp_array->end()
			, second_iter = test_array->begin(); iter != end; ++iter, ++index, ++second_iter )
		{
			ASSERT_EQ( iter->value, index );
			iter->value = (float)(index * 2);
			ASSERT_EQ( second_iter->value, index );
		}
		(test_array->begin() + 4)->next = temp_array.get();
	}
	gc->perform_gc();
	ASSERT_EQ( gc->all_objects().size(), 2 );
	(test_array->begin() + 4)->next = nullptr;
	gc->perform_gc();
	ASSERT_EQ( gc->all_objects().size(), 1 );
	test_array->erase( test_array->begin() + 2, test_array->begin() + 3 );
	ASSERT_EQ( test_array->size(), 4 );
	
	index = 0;
	for ( auto iter = test_array->begin(), end = test_array->end(); iter != end; ++iter, ++index )
	{
		if ( index < 2 )
		{
			ASSERT_EQ( iter->value, index );
		}
		else
		{
			ASSERT_EQ( iter->value, index + 1 );
		}
	}

	test_array->insert( test_array->begin() + 2, 2 );
	ASSERT_EQ( test_array->size(), 6 );
	
	ASSERT_EQ( (test_array->begin() + 2)->value, 0 );
	
	
	//force creation of actual new data
	test_array->insert( test_array->end(), 10 );
	ASSERT_EQ( test_array->size(), 16 );
	
	/*
	(test_array->begin() + 15)->value = 15;
	ASSERT_EQ( (test_array->begin() + 15)->value, 15 );
	//Now we see if cleanup cleans up the object correctly because it should have 
	//non-contiguous data.
	*/
}