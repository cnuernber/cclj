//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_NONCOPYABLE_H
#define CCLJ_NONCOPYABLE_H
#pragma once
#include "cclj.h"

namespace cclj
{
	class noncopyable
	{
	private:
		noncopyable& operator=( const noncopyable& );
		noncopyable( const noncopyable& );
	public:
		noncopyable(){}
	};
}

#endif