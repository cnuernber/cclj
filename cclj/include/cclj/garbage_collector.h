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

	class reference_tracker
	{
	protected:
		virtual ~reference_tracker(){}
	public:
		friend class shared_ptr<reference_tracker>;
		virtual size_t get_outgoing_references( gc_object& object, pair<void*,size_t> data
												, size_t index, gc_object** buffer, size_t bufferLen ) = 0;
		virtual void object_deallocated( gc_object& object, pair<void*,size_t> data ) = 0;
		//It should be noted that the collector could be copying.  
		virtual void object_copied( gc_object& object, pair<void*,size_t>& oldMem, pair<void*,size_t> newMem ) = 0;
	};

	typedef shared_ptr<reference_tracker> reference_tracker_ptr;
	
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
	};



	class gc_object
	{
		gc_object( const gc_object& );
		gc_object& operator=( const gc_object& );
	public:

		//do not create these yourself; create them with the garbage collector
		//member data may or may not follow this object immediately, depends on if the collector
		//is a copying or noncopying collector.
		gc_object() : user_flags( 0 ) {}
		//Type is used by the gc to allow it to track references automatically without
		//calling back to via the ref tracker.  If there is no type, then the GC assumes
		//this is a user-type and will call back to c++.
		string_table_str	type;
		gc_object_flags		flags;
		uint16_t			user_flags;
	};

	//types that the GC *has* to support:
	//their type name is their type.
	struct gc_array
	{
		string_table_str _type;
		uint32_t		 _count;
	};

	//uses std::unordered_map which has size operator.
	struct gc_hash_table
	{
		string_table_str _key_type;
		string_table_str _value_type;
		void*			 _table;
	};

	//It should be noted that the collector could be copying.  
	class garbage_collector
	{
	protected:
		virtual ~garbage_collector(){}
	public:

		friend class shared_ptr<garbage_collector>;

		virtual gc_object& allocate( string_table_str type, const char* file, int line ) = 0;
		virtual gc_object& allocate( size_t size, const char* file, int line ) = 0;
		virtual void mark_root( gc_object& object ) = 0;
		virtual void unmark_root( gc_object& object ) = 0;
		virtual bool is_root( gc_object& object ) = 0;
		virtual void update_reference( gc_object& parent, gc_object* child ) = 0;
		virtual pair<void*,size_t> lock( gc_object& obj ) = 0;
		virtual void unlock( gc_object& obj ) = 0;
		virtual void perform_gc() = 0;

		virtual string_table_str array_type() const = 0;
		virtual string_table_str hash_table_type() const = 0;

		virtual allocator_ptr allocator() = 0;
		virtual reference_tracker_ptr reference_tracker() = 0;
		virtual string_table_ptr string_table() = 0;
		virtual class_system_ptr class_system() = 0;

		static shared_ptr<garbage_collector> create_mark_sweep( allocator_ptr alloc
																, reference_tracker_ptr refTracker
																, string_table_ptr strtable
																, class_system_ptr cls_system );
	};

	typedef shared_ptr<garbage_collector> garbage_collector_ptr;

	
	//safe-ish public access to the gc object.
	class gc_obj_ptr
	{
		garbage_collector_ptr	_gc;
		gc_object*				_object;
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

		gc_obj_ptr(garbage_collector_ptr gc, gc_object& obj )
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

		operator bool () const { return _object != NULL; }

		garbage_collector_ptr gc() const { return _gc; }
		gc_object* operator->() const { assert(_object); return _object; }
		gc_object& operator*() const { assert(_object); return *_object; }

		pair<void*,size_t> data() const { return _data; }

		gc_object* object() const { return _object; }
	};
	
	class data_array_iter_base
	{
		uint32_t	_count;
		int32_t		_index;
	public:
		data_array_iter_base( uint32_t _c = 0, int32_t idx = 0 )
			: _count( _c )
			, _index( idx )
		{
		}
		data_array_iter_base( const data_array_iter_base& other )
			: _count( other._count )
			, _index( other._index )
		{
		}
		data_array_iter_base& operator=( const data_array_iter_base& other )
		{
			_count = other._count;
			_index = other._index;
			return *this;
		}

		data_array_iter_base operator+( int val )
		{
			return data_array_iter_base( _count, _index + val );
		}

		data_array_iter_base operator-( int val )
		{
			return data_array_iter_base( _count, _index - val );
		}

		bool operator==( const data_array_iter_base& other ) const
		{
			return _count == other._count
				&& _index == other._index;
		}
		
		bool operator!=( const data_array_iter_base& other ) const
		{
			return _count == other._count
				&& _index == other._index;
		}

		bool operator > ( const data_array_iter_base& other ) const
		{
			return _index > other._index;
		}
		
		bool operator < ( const data_array_iter_base& other ) const
		{
			return _index < other._index;
		}

		bool operator >= ( const data_array_iter_base& other ) const
		{
			return _index >= other._index;
		}
		
		bool operator <= ( const data_array_iter_base& other ) const
		{
			return _index <= other._index;
		}

		data_array_iter_base& operator++() { ++_index; return *this; }
		data_array_iter_base operator++(int) 
		{ 
			data_array_iter_base retval (*this );
			++_index;
			return retval;
		}
		
		data_array_iter_base& operator--() { --_index; return *this; }
		data_array_iter_base operator--(int) 
		{ 
			data_array_iter_base retval (*this );
			--_index;
			return retval;
		}

		int32_t index() const { return _index; }
		uint32_t count() const { return _count; }
	};

	struct gc_array_const_item_ref
	{
		class_definition*		_definition;
		uint32_t				_instance_size;
		const uint8_t*			_item_data;
		gc_array_const_item_ref( uint32_t is = 0, const uint8_t* id = nullptr, class_definition* cls_def = nullptr )
			: _instance_size( is )
			, _item_data( id )
			, _definition( cls_def )
		{
		}
	};

	struct gc_array_item_ref
	{
		class_definition*		_definition;
		uint32_t				_instance_size;
		uint8_t*				_item_data;
		gc_array_item_ref( uint32_t is = 0, uint8_t* id = nullptr, class_definition* cls_def = nullptr )
			: _instance_size( is )
			, _item_data( id )
			, _definition( cls_def )
		{
		}

		operator gc_array_const_item_ref () const
		{
			return gc_array_const_item_ref( _instance_size, _item_data, _definition );
		}
	};
	

	class gc_array_const_iter : public data_array_iter_base
	{
		class_definition*		_definition;
		uint32_t				_instance_size;
		const uint8_t*			_array_data;
	public:

		gc_array_const_iter() : data_array_iter_base(), _definition( nullptr ), _instance_size( 0 ), _array_data( 0 ) {}
		gc_array_const_iter( const data_array_iter_base& iter_base, class_definition* def
								, uint32_t is, const uint8_t* data )
			: data_array_iter_base( iter_base )
			, _definition( def )
			, _instance_size( is )
			, _array_data( data )
		{
		}
		gc_array_const_iter( const gc_array_const_iter& other )
			: data_array_iter_base( other )
			, _definition( other._definition )
			, _instance_size( other._instance_size )
			, _array_data( other._array_data )
		{
		}
		
		gc_array_const_iter& operator=( const gc_array_const_iter& other )
		{
			if ( this != &other )
			{
				data_array_iter_base::operator=( other );
				_definition = other._definition;
				_instance_size = other._instance_size;
				_array_data = other._array_data;
			}
			return *this;
		}

		gc_array_const_item_ref item_ref()
		{
			if ( index() >= (int32_t)count() 
				|| index() < 0 )
				throw runtime_error( "out of bounds array access" );
			int32_t off = index() * (int32_t)_instance_size;
			return gc_array_const_item_ref( _instance_size, _array_data + off, _definition );
		}
	};

	class gc_array_iter : public data_array_iter_base
	{
		class_definition*		_definition;
		uint32_t				_instance_size;
		uint8_t*				_array_data;
	public:

		gc_array_iter() : data_array_iter_base(), _definition( nullptr ), _instance_size( 0 ), _array_data( 0 ) {}
		gc_array_iter( const data_array_iter_base& iter_base, class_definition* def, uint32_t is, uint8_t* data )
			: data_array_iter_base( iter_base )
			, _definition( def )
			, _instance_size( is )
			, _array_data( data )
		{
		}
		gc_array_iter( const gc_array_iter& other )
			: data_array_iter_base( other )
			, _definition( other._definition )
			, _instance_size( other._instance_size )
			, _array_data( other._array_data )
		{
		}
		
		gc_array_iter& operator=( const gc_array_iter& other )
		{
			if ( this != &other )
			{
				data_array_iter_base::operator=( other );
				_definition = other._definition;
				_instance_size = other._instance_size;
				_array_data = other._array_data;
			}
			return *this;
		}

		gc_array_item_ref item_ref()
		{
			if ( index() >= (int32_t)count() 
				|| index() < 0 )
				throw runtime_error( "out of bounds array access" );
			int32_t off = index() * (int32_t)_instance_size;
			return gc_array_item_ref( _instance_size, _array_data + off, _definition );
		}

		operator gc_array_const_iter () const
		{
			return gc_array_const_iter( *this, _definition, _instance_size, _array_data );
		}
	};

	class gc_array_ptr : public gc_obj_ptr
	{
		class_definition_ptr _definition;
		gc_array*			 _array;
		uint8_t*			 _array_data;
		uint32_t			 _capacity;

		void acquire()
		{
			release();
			if ( object() )
			{
				if ( object()->type != gc()->array_type() )
					throw runtime_error( "array ptr constructed with type not an array" );
				pair<void*,size_t> data_pair = data();
				uint8_t* data_ptr = reinterpret_cast<uint8_t*>( data_pair.first );
				_array = reinterpret_cast<gc_array*>( data_ptr );
				data_ptr += sizeof( gc_array );
				size_t data_section_length = std::max( (size_t)0, data_pair.second - sizeof( gc_array ) );
				_array_data = data_ptr;
				_definition = gc()->class_system()->find_definition( _array->_type );
				if ( !_definition )
					throw runtime_error( "Failed to find class definition for array" );
				
				//check that the array members line up with the data size.
				_capacity = data_section_length / _definition->instance_size();
				uint32_t leftover = (uint32_t)data_section_length % _definition->instance_size();
				if ( leftover )
					throw runtime_error( "leftover data area instance size mismatch" );
				if ( _array->_count > _capacity )
					throw runtime_error( "array count/capacity wrong" );
			}
		}
		void release()
		{
			_array = nullptr;
			_array_data = nullptr;
			_capacity = 0;
			_definition = class_definition_ptr();
		}
	public:
		
		typedef gc_array_iter iterator;
		typedef gc_array_const_iter const_iterator;
		gc_array_ptr() { release(); }

		gc_array_ptr( const gc_obj_ptr& ptr )
			: gc_obj_ptr( ptr )
			, _array( nullptr )
			, _array_data( nullptr )
		{
			acquire();
		}

		gc_array_ptr( const gc_array_ptr& obj )
			: gc_obj_ptr( obj )
			, _array( nullptr )
			, _array_data( nullptr )
		{
			acquire();
		}

		gc_array_ptr& operator=( const gc_array_ptr& obj )
		{
			if ( this != &obj )
			{
				gc_obj_ptr::operator=( obj );
				acquire();
			}
			return *this;
		}

		operator bool () const { return _array != nullptr; }

		void ensure_array() const { if ( !_array ) throw runtime_error( "invalid pointer access" ); }

		size_t size() const { if ( !_array ) return 0; return _array->_count; }
		size_t capacity() const { return _capacity; }
		pair<uint8_t*, size_t> array_data() { return make_pair( _array_data, capacity() ); }
		size_t item_size() const { ensure_array(); return _definition->instance_size(); }
		bool empty() const { return !_array; }

		iterator begin() 
		{
			class_definition* def = nullptr;
			if ( _definition )
				def = _definition.get();

			return iterator( data_array_iter_base( (uint32_t)size(), 0 ), def, item_size(), _array_data );
		}
		iterator end() 
		{
			class_definition* def = nullptr;
			if ( _definition )
				def = _definition.get();

			return iterator( data_array_iter_base( (uint32_t)size(), (int32_t)size() ), def, item_size(), _array_data );
		}
		
		const_iterator begin() const
		{
			return const_cast<gc_array_ptr&>( *this ).begin();
		}

		const_iterator end() const
		{
			return const_cast<gc_array_ptr&>( *this ).end();
		}

		gc_array_item_ref operator[]( int32_t idx ) 
		{ 
			if ( idx >= (int32_t)size() )
				throw runtime_error( "array access out of bounds" );
			int32_t off = idx * (int32_t)item_size();
			return gc_array_item_ref( (uint32_t)item_size(), _array_data + off, _definition.get() );
		}
		
		gc_array_const_item_ref operator[]( int32_t idx ) const
		{
			return const_cast<gc_array_ptr&>( *this ).operator[]( idx );
		}

	};

}

#endif