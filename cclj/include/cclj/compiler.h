//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_COMPILER_H
#define CCLJ_COMPILER_H
#pragma once
#include "cclj/cclj.h"
#include "cclj/lisp_types.h"

namespace cclj
{
	class ast_node;
	class module;

	class compiler
	{
	protected:
		virtual ~compiler(){}
	public:
		friend class shared_ptr<compiler>;

		//A compiler always writes to a single module.
		virtual shared_ptr<module> module() = 0;

		//transform text into the lisp datastructures.
		virtual vector<lisp::object_ptr> read( const string& text ) = 0;

		//Transform lisp datastructures into type-checked ast.
		virtual void type_check( data_buffer<lisp::object_ptr> preprocess_result ) = 0;

		//compile module to binary.
		virtual pair<void*,type_ref_ptr> compile() = 0;

		//Create a compiler and execute this text return the last value if it is a float else exception.
		virtual float execute( const string& text ) = 0;

		static shared_ptr<compiler> create();
	};

	typedef shared_ptr<compiler> compiler_ptr;
}

#endif