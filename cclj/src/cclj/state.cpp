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


namespace
{
	struct state_impl : public state
	{
		virtual float execute( const string& /*data*/ )
		{
			throw runtime_error( "unimplemented" );
		}
	};
}

state_ptr state::create_state() { return make_shared<state_impl>(); }


