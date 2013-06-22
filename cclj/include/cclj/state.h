//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_STATE_H
#define CCLJ_STATE_H
#include "cclj/cclj.h"
#include "cclj/garbage_collector.h"

namespace cclj
{
	class connection
	{
	protected:
		virtual ~connection(){}
	public:
		friend class shared_ptr<connection>;
	};

	typedef shared_ptr<connection> connection_ptr;


	class state;
	typedef shared_ptr<state> state_ptr;


	class state
	{
	protected:
		virtual ~state(){}
	public:
		friend class shared_ptr<state>;

		/*
		virtual gc_obj_ptr eval( const char* script
								, const char* file
								, lang_type_ptr<context> script_context = lang_type_ptr<context>() ) = 0;
		virtual lang_type_ptr<context> global_context() = 0;
		virtual void set_global_context( lang_type_ptr<context> ctx ) = 0;
		virtual garbage_collector_ptr gc() = 0;
		
		//Only functions are callable as they combine context with body.
		//Thus the return value of register_function needs to be combined with a function
		//with context in order to have something callable.
		virtual pair<connection_ptr,lang_type_ptr<user_fn> > register_function( const user_function& fn ) = 0;
		*/

		static shared_ptr<state> create(allocator_ptr alloc);
	};

	typedef shared_ptr<state> state_ptr;
}
#endif