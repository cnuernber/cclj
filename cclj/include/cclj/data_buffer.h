//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_DATA_BUFFER_H
#define CCLJ_DATA_BUFFER_H
#include "cclj/cclj.h"

namespace cclj
{
	template<typename TDataType>
	class data_buffer
	{
		TDataType*	_buffer;
		size_t		_size;
	public:
		data_buffer( TDataType* bufData, size_t size )
			: _buffer( bufData )
			, _size( size )
		{
		}
		data_buffer( vector<TDataType>& buf )
			: _buffer( nullptr )
			, _size( buf.size() )
		{
			if ( buf.size() ) _buffer = &(buf[0]);
		}
		data_buffer() : _buffer( nullptr ), _size( 0 ) {}
		size_t size() const { return _size; }
		TDataType* begin() const { return _buffer; }
		TDataType* end() const { return _buffer + size(); }
		TDataType& operator[]( int idx ) const { assert( idx < size ); return _buffer[idx]; }
	};
}

#endif