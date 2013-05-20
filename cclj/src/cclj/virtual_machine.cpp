//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/virtual_machine.h"

using namespace cclj;


namespace 
{
	
	typedef vector<vm_register> vm_register_list;
	typedef unordered_map<uint32_t,uint32_t> uint_to_uint_map;
	typedef pair<uint32_t,uint32_t> uint_uint_pair;
	typedef vector<uint_uint_pair> uint_uint_pair_list;
	typedef vector<uint_uint_pair_list> register_stack_type;
	typedef vector<uint32_t> uint_list;


	struct fn_info
	{
		//virtual->actual register mapping
		uint_list			_register_map;

		//actual registers
		uint_list			_allocated_registers;

		//actual registers
		uint_list			_arguments;

		//actual register, where to put the return value from
		uint32_t			_current_return_value;

		//actual register, where the return value from the last function sits.
		uint32_t			_last_return_value;

		fn_info() 
			: _current_return_value( register_file::invalid_register() )
			, _last_return_value( register_file::invalid_register() )
		{}

		uint32_t allocate_register( uint32_t actual_register )
		{
			_allocated_registers.push_back( actual_register );
			uint32_t retval = static_cast<uint32_t>( _register_map.size() );
			_register_map.push_back( actual_register );
			return retval;
		}

		uint32_t transfer_argument_register( fn_info& inParent, uint32_t virtual_reg )
		{
			uint32_t actual_register = inParent[virtual_reg];
			uint32_t retval = static_cast<uint32_t>( _register_map.size() );
			_register_map.push_back( actual_register );
			_arguments.push_back( actual_register );
			return retval;
		}

		uint32_t operator[]( uint32_t virtual_reg_idx )
		{
			if ( virtual_reg_idx < (uint32_t)_register_map.size() )
				return _register_map[virtual_reg_idx];
			throw runtime_error( "invalid register lookup" );
		}

		int32_t get_argument( uint32_t arg_idx )
		{
			if ( arg_idx < _arguments.size() )
				return _arguments[arg_idx];
			throw runtime_error( "invalid argument lookup" );
		}
	};

	typedef vector<fn_info> fn_info_list;

	struct reg_file_impl : public register_file
	{
		vm_register_list		_registers;
		fn_info_list			_register_stack;
		uint_list				_free_registers;

		reg_file_impl()
		{
			reset();
		}

		fn_info& current_fn()
		{
			if ( _register_stack.empty() )
				_register_stack.push_back( fn_info() );
			return _register_stack.back();
		}


		
		virtual uint32_t allocate_register(const vm_register& inData)
		{
			fn_info& fn = current_fn();
			uint32_t register_index = register_file::invalid_register();
			if ( _free_registers.empty() == false )
			{
				register_index = _free_registers.back();
				_free_registers.pop_back();
			}
			else 
			{
				register_index = static_cast<uint32_t>( _registers.size() );
				_registers.push_back( vm_register() );
			}

			_registers[register_index] = inData;
			return fn.allocate_register( register_index );
		}

		virtual vm_register get_register( uint32_t regIndex )
		{
			fn_info& fn = current_fn();
			uint32_t actual_reg = fn[regIndex];
			if ( actual_reg < _registers.size() )
				return _registers[actual_reg];
			throw runtime_error( "failed to lookup register" );
		}

		virtual void call_function( data_buffer<uint32_t> args, uint32_t retval_reg )
		{
			current_fn();
			_register_stack.push_back( fn_info() );
			fn_info& last_fn = _register_stack[_register_stack.size()-1];
			fn_info& fn(current_fn());
			if ( retval_reg != invalid_register() )
				fn._current_return_value = last_fn[retval_reg];

			for_each( args.begin(), args.end()
				, [&] (uint32_t arg) { fn.transfer_argument_register( last_fn, arg ); } );
		}

		virtual vm_register get_argument( uint32_t argIndex )
		{
			fn_info& fn(current_fn());
			return fn[fn.get_argument(argIndex)];
		}

		virtual vm_register get_return_value()
		{
			fn_info& fn(current_fn());
			return _registers[fn._last_return_value];
		}

		virtual void function_return( uint32_t retval )
		{
			uint32_t actual_return_register = invalid_register();
			{
				fn_info& fn(current_fn());

				if ( retval != invalid_register() )
					actual_return_register = fn[retval];
			
				//Free all allocated registers
				for_each( fn._allocated_registers.begin(), fn._allocated_registers.end(),
					[this] (uint32_t actual_reg) 
				{ 
					_free_registers.push_back( actual_reg ); 
				} );

				_register_stack.pop_back();
			}
			{
				fn_info& fn(current_fn());
				fn._current_return_value = actual_return_register;
			}
		}

		
		virtual void reset()
		{
			_free_registers.clear();
			_register_stack.resize(1);
			_register_stack[0] = fn_info();
			for ( uint32_t idx = 0, end = (uint32_t)_registers.size(); idx < end; ++idx )
			{
				_registers[idx] = vm_register();
				_free_registers.push_back( idx );
			}
		}
	};
}

register_file_ptr register_file::create() { return make_shared<reg_file_impl>(); }