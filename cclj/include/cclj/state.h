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

namespace cclj
{
	class state
	{
	protected:
		virtual ~state(){}
	public:

		friend class shared_ptr<state>;

		//parse, compile, and execute this script returning a float value.
		virtual float execute( const string& data ) = 0;


		static shared_ptr<state> create_state();
	};

	typedef shared_ptr<state> state_ptr;
}

#endif