//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_OPTION_H
#define CCLJ_OPTION_H
#include "cclj.h"

namespace cclj
{
	template<typename dtype>
	class option
	{
		dtype	_value;
		bool	has_value;

	public:
		option() : has_value( false ) {}
		option( const dtype& other ) : _value( other ), has_value( true ) {}
		option( const option<dtype>& other ) : _value( other._value ), has_value( other.has_value ) {}
		option& operator=( const option<dtype>& other )
		{
			if ( this != &other )
			{
				_value = other._value;
				has_value = other.has_value;
			}
			return *this;
		}
		option& operator=( const dtype& other )
		{
			has_value = true;
			_value = other;
			return *this;
		}
		bool empty() const { return !has_value; }
		//more common than empty
		bool valid() const { return !empty(); }

		dtype& value() { if( !has_value ) throw runtime_error( "value requested of empty option" ); return _value; }
		dtype& value() const { if( !has_value ) throw runtime_error( "value requested of empty option" ); return _value; }

		dtype& operator->() { return value(); }
		dtype& operator->() const { return value(); }
		dtype& operator*() { return value(); }
		dtype& operator*() const { return value(); }
	};
}

#endif