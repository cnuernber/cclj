//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_GC_H
#define CCLJ_GC_H
#pragma once

#include "cclj/cclj.h"
#include "cclj/allocator.h"
#include "cclj/string_table.h"
#include "cclj/class_system.h"
#include "cclj/noncopyable.h"
#include "cclj/class_system_predefined_types.h"

namespace cclj
{
	using std::shared_ptr;
	using std::pair;

	class gc_object;
	
	template<typename enumType, typename storage>
	struct flags
	{
		storage data;
		flags() : data( 0 ) {}
		bool has_value( enumType val ) const
		{
			return (data & ((storage)val)) == (storage)val;
		}

		void set( enumType flag, bool value )
		{
			storage newVal = (storage)flag;
			if ( value ) {
				data = data | newVal;
			}
			else {
				storage opposite = ~newVal;
				data = data & opposite;
			}
		}
		template<typename data_type, uint8_t offset, uint32_t mask>
		data_type bitset() const
		{
			uint32_t item = (((uint32_t)data) >> offset) & mask;
			return static_cast<data_type>( item );
		}
		
		template<typename data_type, uint8_t offset, uint32_t mask>
		void set_bitset( data_type item )
		{
			uint32_t new_item = item << offset;
			uint32_t offset_mask = mask << offset;
			offset_mask = ~offset_mask;
			data = data & ((storage)offset_mask);
			data = data | ((storage)new_item);
		}

	};

	struct gc_object_types
	{
		enum val
		{
			object				= 0,
			array_object		= 1,
			hash_table_object	= 2,
		};
	};

	struct gc_object_flag_values
	{
		enum val
		{
			no_value =			0,
			root =				1 << 0,
			locked =			1 << 1,
			mark_left =			1 << 2,
			mark_right =		1 << 3,
			object_type_start = 4,
			object_type_stop  = 6,
		};
	};

	class gc_object_flags : public flags<gc_object_flag_values::val,uint16_t>
	{
	public:
		bool is_root() const { return has_value( gc_object_flag_values::root ); }
		void set_root( bool val ) { set( gc_object_flag_values::root, val ); }

		bool is_locked() const { return has_value( gc_object_flag_values::locked ); }
		void set_locked( bool val ) { set( gc_object_flag_values::locked, val ); }

		bool is_marked_left() const { return has_value( gc_object_flag_values::mark_left ); }
		void set_marked_left( bool val ) { set( gc_object_flag_values::mark_left, val ); }
		
		bool is_marked_right() const { return has_value( gc_object_flag_values::mark_right ); }
		void set_marked_right( bool val ) { set( gc_object_flag_values::mark_right, val ); }

		gc_object_types::val object_type() const
		{
			return static_cast<gc_object_types::val>( bitset<uint8_t,gc_object_flag_values::object_type_start, 7>() );
		}

		void set_object_type( gc_object_types::val type )
		{
			set_bitset<uint8_t,gc_object_flag_values::object_type_start, 7>( static_cast<uint8_t>( type ) );
		}
	};



	class gc_object_base : noncopyable
	{
	protected:
		gc_object_base(gc_object_types::val type ) 
		{
			flags.set_object_type( type );
		}
	public:

		//do not create these yourself; create them with the garbage collector
		//member data may or may not follow this object immediately, depends on if the collector
		//is a copying or noncopying collector.
		gc_object_flags		flags; 
		uint16_t			user_flags; //16 bits for you!
	};

	class gc_object : public gc_object_base
	{
	public:
		class_definition_ptr type;
		gc_object( class_definition_ptr def, gc_object_types::val type = gc_object_types::object )
			: gc_object_base( type )
			, type( def )
		{
		}
		static bool is_specific_object( gc_object_base& base )
		{
			return base.flags.object_type() == gc_object_types::object;
		}
	};

	class gc_reallocatable_object : public gc_object_base
	{
	public:
		void*		data_ptr;
		gc_reallocatable_object( gc_object_types::val type )
			: gc_object_base( type )
			, data_ptr( nullptr )
		{
		}
		static bool is_specific_object( gc_object_base& base )
		{
			auto type = base.flags.object_type();
			if ( type == gc_object_types::array_object
				|| type == gc_object_types::hash_table_object )
				return true;
			return false;
		}
	};

	class gc_array_object : public gc_reallocatable_object
	{
	public:
		class_definition_ptr	type;
		uint32_t				count;
		gc_array_object( class_definition_ptr def )
			: gc_reallocatable_object( gc_object_types::array_object )
			, type( def )
			, count( 0 )
		{
		}
		static bool is_specific_object( gc_object_base& base )
		{
			return base.flags.object_type() == gc_object_types::array_object;
		}

	};

	class gc_hash_table_object : public gc_reallocatable_object
	{
	public:
		class_definition_ptr	key_type;
		class_definition_ptr	value_type;
		gc_hash_table_object( class_definition_ptr _key_type, class_definition_ptr _value_type )
			: gc_reallocatable_object( gc_object_types::hash_table_object )
			, key_type( _key_type )
			, value_type( _value_type )
		{
		}
		static bool is_hash_table_object( gc_object_base& base )
		{
			return base.flags.object_type() == gc_object_types::hash_table_object;
		}
	};

	struct gc_object_traits
	{
		template<typename obj_type>
		static obj_type& cast( gc_object_base& base )
		{
			if ( typename obj_type::is_specific_object( base ) ) return static_cast<obj_type&>( base );
			throw runtime_error( "bad object cast" );
		}

		static size_t obj_size( gc_object_base& base )
		{
			switch( base.flags.object_type() )
			{
			case gc_object_types::object: return sizeof( gc_object );
			case gc_object_types::array_object: return sizeof( gc_array_object );
			case gc_object_types::hash_table_object: return sizeof( gc_hash_table_object );
			}
			throw runtime_error( "unrecognized gc object type" );
		}

	};


	typedef const_data_buffer<gc_object_base*>  const_gc_object_base_raw_ptr_buffer;

	//It should be noted that the collector could be copying.  
	class garbage_collector
	{
	protected:
		virtual ~garbage_collector(){}
	public:

		friend class shared_ptr<garbage_collector>;
		
		//If new_size_in_bytes isn't a multiple of type-instance_size() error is thrown.
		virtual gc_object& allocate_object( class_definition_ptr type, const char* file, int line ) = 0;
		virtual gc_array_object& allocate_array( class_definition_ptr type, size_t initial_num_items
																		, const char* file, int line ) = 0;
		/*
		virtual gc_hash_table_object& allocate_hashtable( class_definition& key_type, class_definition& value_type
																					, const char* file, int line ) = 0;
																					*/
		virtual pair<void*,size_t> reallocate( gc_reallocatable_object& in_object, size_t new_size_in_bytes
																		, const char* file, int line ) = 0;

		virtual void mark_root( gc_object_base& object ) = 0;
		virtual void unmark_root( gc_object_base& object ) = 0;
		virtual bool is_root( gc_object_base& object ) = 0;
		virtual pair<void*,size_t> lock( gc_object_base& obj ) = 0;
		virtual void unlock( gc_object_base& obj ) = 0;
		virtual void perform_gc() = 0;
		//very transient data, useful for unit testing.
		virtual const_gc_object_base_raw_ptr_buffer roots_and_locked_objects() = 0;
		virtual const_gc_object_base_raw_ptr_buffer all_live_objects() = 0;

		virtual string_table_str hash_table_type() const = 0;

		virtual allocator_ptr allocator() = 0;
		virtual string_table_ptr string_table() = 0;
		virtual class_system_ptr class_system() = 0;

		static shared_ptr<garbage_collector> create_mark_sweep( allocator_ptr alloc
																, string_table_ptr strtable
																, class_system_ptr cls_system );
	};

	typedef shared_ptr<garbage_collector> garbage_collector_ptr;

	
	//safe-ish public access to the gc object.
	class gc_obj_ptr
	{
		garbage_collector_ptr	_gc;
		gc_object_base*			_object;
		pair<void*,size_t>		_data;
		void acquire()
		{
			_data = pair<void*,size_t>( nullptr, 0 );
			if ( _object && _gc )
				_data = _gc->lock( *_object );
		}
		void release()
		{
			if ( _object && _gc )
			{
				_gc->unlock( *_object );
				_data.first = nullptr;
				_data.second = 0;
			}
			_object = nullptr;
		}
	public:

		gc_obj_ptr() : _object( nullptr ), _data( nullptr, 0 ) {}
		

		gc_obj_ptr(garbage_collector_ptr gc )
			: _gc( gc )
			, _object( nullptr )
			, _data( nullptr, 0 )
		{
		}

		gc_obj_ptr(garbage_collector_ptr gc, gc_object_base& obj )
			: _gc( gc )
			, _object( &obj )
		{
			acquire();
		}

		gc_obj_ptr( const gc_obj_ptr& obj )
			: _gc( obj._gc )
			, _object( obj._object )
		{
			acquire();
		}

		~gc_obj_ptr()
		{
			release();
		}

		gc_obj_ptr& operator=( const gc_obj_ptr& other )
		{
			if ( _object != other._object )
			{
				release();
				_gc = other._gc;
				_object = other._object;
				acquire();
			}
			return *this;
		}
		
		gc_obj_ptr& operator=( gc_object* other )
		{
			if ( _object != other )
			{
				release();
				_object = other;
				acquire();
			}
			return *this;
		}

		operator bool () const { return _object != nullptr; }

		garbage_collector_ptr gc() const { return _gc; }
		gc_object_base* operator->() const { assert(_object); return _object; }
		gc_object_base& operator*() const { assert(_object); return *_object; }

		pair<void*,size_t> data() const { return _data; }

		gc_object_base* object() const { return _object; }

		//resize the data section associated with this gc object.
		pair<void*,size_t> reallocate(size_t new_size, const char* file, int line)
		{
			if ( _object == nullptr )
				return pair<void*,size_t>(nullptr,0);
			
			_data.first = nullptr;
			_data.second = 0;
			_data = _gc->reallocate( gc_object_traits::cast<gc_reallocatable_object>( *_object ), new_size, file, line );
			return _data;
		}
	};

}

#endif