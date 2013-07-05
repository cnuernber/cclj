//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_AST_H
#define CCLJ_AST_H
#pragma once
#include "cclj/cclj.h"
#include "cclj/lisp.h"

namespace cclj { namespace ast {
	using namespace lisp;

	struct ast_node_types
	{
		enum _enum
		{
			unknown_type = 0,
			function_def,
			function_call,
		};
	};

	class ast_node
	{
	public:
		virtual ~ast_node(){}
		virtual ast_node_types::_enum type() = 0;
	};

	typedef shared_ptr<ast_node> ast_node_ptr;

	class function_def : public ast_node
	{
	public:
		enum { ast_type = ast_node_types::function_def };
		virtual ast_node_types::_enum type() { return ast_node_types::function_def; }

		symbol*				_name;
		object_ptr_buffer	_arguments;
		object_ptr_buffer	_body;
		void*				_compiled_code;
	};

	class function_call : public ast_node
	{
	public:
		enum { ast_type = ast_node_types::function_call };
		virtual ast_node_types::_enum type() { return ast_node_types::function_call; }

		symbol*				_name;
		object_ptr_buffer	_arguments;
		function_def*		_function;
	};

	typedef unordered_map<string_table_str, shared_ptr<ast_node> > context_map;
	typedef vector<shared_ptr<ast_node> > ast_node_ptr_list;
}}

#endif