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
		typedef option<dtype> my_type;
		dtype	_value;
		bool	has_value;

	public:
		option() : has_value( false ) {}
		option( const dtype& other ) : _value( other ), has_value( true ) {}
		option( const my_type& other ) : _value( other._value ), has_value( other.has_value ) {}
		my_type& operator=( const my_type& other )
		{
			if ( this != &other )
			{
				_value = other._value;
				has_value = other.has_value;
			}
			return *this;
		}
		my_type& operator=( const dtype& other )
		{
			has_value = true;
			_value = other;
			return *this;
		}
		bool empty() const { return !has_value; }
		//more common than empty
		bool valid() const { return !empty(); }

		operator bool () const { return valid(); }

		dtype& value() { if( !has_value ) throw runtime_error( "value requested of empty option" ); return _value; }
		dtype& value() const { if( !has_value ) throw runtime_error( "value requested of empty option" ); return _value; }

		dtype* operator->() { return &value(); }
		dtype* operator->() const { return &value(); }
		dtype& operator*() { return value(); }
		dtype& operator*() const { return value(); }
	};

	//Used for circumstances where a ptr value may be null and checks *must* be enforced.
	template<typename dtype>
	class ptr_option
	{
		typedef ptr_option<dtype> my_type;
		dtype* _value;
	public:
		ptr_option( dtype& v ) : _value( &v ) {}
		ptr_option( dtype* v = nullptr ) : _value( v ) {}
		ptr_option( const my_type& other ) : _value( other._value ) {}
		my_type& operator=( const my_type& other )
		{
			if ( this != &other )
			{
				_value = other._value;
			}
			return *this;
		}
		my_type& operator=( dtype& other )
		{
			_value = &other;
			return *this;
		}
		my_type& operator=( dtype* other )
		{
			_value = other;
			return *this;
		}
		bool empty() const { return !_value; }
		//more common than empty
		bool valid() const { return !empty(); }

		operator bool () const { return valid(); }

		dtype* get() { if ( !valid() ) throw runtime_error( "invalid get on empty option" ); return _value; }

		dtype* unsafe_get() { return _value; }

		dtype& deref() { return *get(); }
	};
}

#endif