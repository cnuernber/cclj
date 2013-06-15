//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/state.h"

using namespace cclj;

TEST(basic_language, add_two_numbers_and_get_a_number) 
{
	//The language tests are going to fail for a while because
	//I haven't got to implementing the entire language yet.

	/*
	allocator_ptr alloc = allocator::create_checking_allocator();
	state_ptr theState = state::create(alloc);
	gc_obj_ptr obj_ptr = theState->eval( "(+ 1 2)", "test_file.cclj" );
	ASSERT_EQ( obj_ptr->user_flags, type_ids::number );
	cclj_number num = number_from_gc_object( *obj_ptr );
	ASSERT_EQ( num, 3.0f );
	theState->gc()->perform_gc();
	cclj_number test = number_from_gc_object( *obj_ptr );
	ASSERT_EQ( test, 3.0f );
	*/
}