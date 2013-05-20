//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_VIRTUAL_MACHINE_H
#define CCLJ_VIRTUAL_MACHINE_H
#include "cclj/cclj.h"
#include "cclj/garbage_collector.h"
#include "cclj/variant.h"
#include "cclj/data_buffer.h"

namespace cclj
{

	//our vm has 64 bit registers, I guess.
	struct vm_register
	{
		//64 bits or under fits in a data register.
		uint64_t _data;
		vm_register(uint64_t input = 0 )
			: _data( input )
		{
		}
	};

	class register_file;
	typedef shared_ptr<register_file> register_file_ptr;
	
	//from my very limited experience with llvm, I know its registers are constant values
	//so what I want to do is to compile down to something close to that.
	class register_file
	{
	protected:
		virtual ~register_file(){}

	public:
		static uint32_t invalid_register() { return numeric_limits<uint32_t>::max(); }
		virtual uint32_t allocate_register(const vm_register& inData) = 0;
		virtual vm_register get_register( uint32_t regIndex ) = 0;
		virtual void call_function( data_buffer<uint32_t> args, uint32_t retval_reg = invalid_register() ) = 0;
		virtual vm_register get_argument( uint32_t argIndex ) = 0;
		//valid only after a function returns
		virtual vm_register get_return_value() = 0;
		virtual void function_return( uint32_t retval ) = 0;

		//Release all registers and get back to a base state.
		virtual void reset() = 0;

		friend class shared_ptr<register_file>;

		static register_file_ptr create();
	};

}

#endif