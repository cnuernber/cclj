//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_CLASS_SYSTEM_PREDEFINED_TYPES_H
#define CCLJ_CLASS_SYSTEM_PREDEFINED_TYPES_H
#include "cclj.h"
#include "string_table.h"

namespace cclj
{
	//note that the object ref changes size on 32 bit vs. 64 bit systems.
	class variable_size_objref_t
	{
	public:
		void* data;
	};

	//the fixed size ref obviously does not change size on 32 bit vs. 64 bit systems.
	//when in doubt, it is probably much wiser to use the fixed size ref.
	class objref_t
	{
	public:
		union
		{
			uint64_t size_adjust;
			void* data;
		};
	};

	typedef float	float32_t;
	typedef double	float64_t;

	//simple set of macros used to predefine a set of types
#define ITERATE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPES						\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( uint8_t )					\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( int8_t )					\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( uint16_t )				\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( int16_t )					\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( uint32_t )				\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( int32_t )					\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( uint64_t )				\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( int64_t )					\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( float32_t )				\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( float64_t )				\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( objref_t )				\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( variable_size_objref_t )	\
	HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( string_table_str )	

}


#endif