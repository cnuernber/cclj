//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_MODULE_H
#define CCLJ_MODULE_H
#pragma once
#include "cclj/cclj.h"
#include "cclj/data_buffer.h"
#include "cclj/string_table.h"
#include "cclj/type_library.h"
#include "cclj/plugins/compiler_plugin.h"
#include "cclj/variant.h"

namespace cclj
{
	typedef data_buffer<ast_node_ptr> ast_node_buffer;

	class variable_node;
	class function_node;
	class datatype_node;

	class variable_node_factory
	{
	protected:
		virtual ~variable_node_factory(){}
	public:
		virtual void set_visibility(visibility::_enum visibility) = 0;
		virtual variable_node& node() = 0;
		virtual void set_value(void* value) = 0;
	};

	class variable_node
	{
	protected:
		virtual ~variable_node(){}
	public:
		virtual type_ref& type() = 0;
		virtual qualified_name name() = 0;
		virtual visibility::_enum visibility() = 0;
		virtual void compile_first_pass(compiler_context& ctx) = 0;
		virtual llvm::GlobalVariable& llvm_variable() = 0;
	};

	typedef variable_node* variable_node_ptr;

	struct named_type
	{
		string_table_str name;
		type_ref_ptr type;

		named_type(string_table_str nm, type_ref_ptr t)
			: name(nm)
			, type(t)
		{}
		named_type() : type( nullptr ) {}
	};

	typedef data_buffer<named_type> named_type_buffer;
	typedef function<pair<llvm_value_ptr_opt, type_ref_ptr> (compiler_context&)> compile_pass_fn;

	class function_factory
	{
	protected:
		virtual ~function_factory(){}
	public:
		virtual void set_function_body(ast_node_buffer body) = 0;
		virtual void set_function_body(void* fn_ptr) = 0;
		virtual void set_function_override_body(compile_pass_fn) = 0;
		virtual void set_visibility(visibility::_enum visibility) = 0;
		virtual function_node& node() = 0;
	};


	class function_node
	{
	protected:
		virtual ~function_node(){}
	public:
		virtual type_ref& return_type() = 0;
		virtual data_buffer<named_type> arguments() = 0;
		virtual visibility::_enum visibility() = 0;
		//a type composed of the fn name and the types as specializations
		virtual type_ref& type() = 0;
		virtual qualified_name name() = 0;
		//external functions are ones that are defined via external
		//fn pointers, they do not have ast node buffers as bodies.
		virtual bool is_external() const = 0;
		virtual ast_node_buffer get_function_body() = 0;
		virtual void*			get_function_external_body() = 0;
		virtual compile_pass_fn get_function_override_body() = 0;
		virtual void compile_first_pass(compiler_context& ctx) = 0;
		virtual void compile_second_pass(compiler_context& ctx) = 0;
		virtual llvm::Function& llvm() = 0;
	};

	typedef function_node* function_node_ptr;
	typedef data_buffer<function_node_ptr> function_node_buffer;

	struct accessor
	{
		string_table_str name;
		type_ref_ptr type;
		function_node_ptr getter;
		function_node_ptr setter;
		accessor(string_table_str nm, type_ref_ptr t, function_node_ptr g, function_node_ptr s)
			: name(nm)
			, type(t)
			, getter(g)
			, setter(s)
		{}

		accessor()
			: type(nullptr)
			, getter(nullptr)
			, setter(nullptr)
		{}

		void compile_first_pass(compiler_context& ctx);
		void compile_second_pass(compiler_context& ctx);
	};

	typedef data_buffer<accessor> accessor_buffer;

	struct datatype_property_type
	{
		enum _enum
		{
			unknown_property_type = 0,
			field,
			accessor,
		};
	};

	template<typename dtype> struct datatype_property_type_id {};
	template<> struct datatype_property_type_id<named_type> { 
		static datatype_property_type::_enum type() 
		{ 
			return datatype_property_type::field;  
		} 
	};
	template<> struct variant_destruct<named_type> { void operator()(named_type&){}};

	template<> struct datatype_property_type_id<accessor> {
		static datatype_property_type::_enum type()
		{
			return datatype_property_type::accessor;
		}
	};
	template<> struct variant_destruct<accessor> { void operator()(accessor&){} };

	struct datatype_property_traits
	{
		enum _enum
		{
			buffer_size = sizeof(accessor),
		};


		static datatype_property_type::_enum empty_type() { return datatype_property_type::unknown_property_type; }
		typedef datatype_property_type::_enum id_type;
		template<typename dtype> static id_type typeof() { return datatype_property_type_id<dtype>::type(); }
		template<typename trettype, typename tvisitor>
		static trettype do_visit(char* data, id_type data_type, tvisitor visitor)
		{
			switch (data_type)
			{
			case datatype_property_type::field: return visitor(*reinterpret_cast<named_type*>(data));
			case datatype_property_type::accessor: return visitor(*reinterpret_cast<accessor*>(data));
			default: return visitor();
			}
		}
	};

	typedef variant_traits_impl < datatype_property_traits > datatype_property_traits_impl;

	struct datatype_property : public variant<datatype_property_traits_impl>
	{
		typedef variant<datatype_property_traits_impl> base;
		datatype_property(){}
		datatype_property(const datatype_property& other) : base(static_cast<const base&>(other)) {}
		datatype_property& operator=(const datatype_property& other) { base::operator=(other); return *this; }
		datatype_property(named_type& field) : base(field) {}
		datatype_property(accessor& access) : base(access) {}
	};

	struct datatype_predefined_operators
	{
		enum _enum
		{
			unknown_predefined_operator = 0,
			constructor,
			assignment,
			destructor,
			apply,
		};
		static const char* to_string(_enum val)
		{
			switch (val)
			{
			case constructor: return "constructor";
			case assignment: return "assignment";
			case destructor: return "destructor";
			case apply: return "apply";
			}
			throw runtime_error("unknown predefined operator");
		}
	};

	struct datatype_operator_overload_policy
	{
		enum _enum
		{
			allow_infinite_overloads = 0,
			allow_no_overloads,
		};
	};

	//datatype fields can only be defined in one place.
	class datatype_node_factory
	{
	protected:
		virtual ~datatype_node_factory(){}
	public:
		virtual visibility::_enum visibility() = 0;
		virtual void set_visibility(visibility::_enum visibility) = 0;
		virtual void add_field(named_type field) = 0;
		virtual datatype_node& node() = 0;
	};

	//operators aren't needed in general except where the compiler needs to interact directly
	//with the datatype.  Everything else should be possible using a traits-type system, similar to
	//haskell's type classes.  Note that there are explicitly *no* references to member functions;
	//that is because this language doesn't support them in that way.  There are only functions, and
	//when the language gets far enough that I can write a decent IDE in it I will add the ability
	//to help intellisense via compiler information entered so any function can be made to look like
	//a member function, similar to C#.  Things like vtables and virtual functions will be setup
	//quite differently.  Derivation and such I haven't figured out yet.
	class datatype_node
	{
	protected:
		virtual ~datatype_node(){}
	public:

		virtual qualified_name name() const = 0;
		virtual const type_ref& type() const = 0;
		virtual void add_static_field(named_type field) = 0;
		virtual vector<named_type> static_fields() = 0;

		virtual vector<named_type> fields() = 0;

		virtual void add_accessor(accessor access) = 0;

		virtual void add_static_accessor(accessor access) = 0;

		//A property may be either a field or accessor pair.
		virtual vector<datatype_property> properties() = 0;
		virtual datatype_property find_property(string_table_str name) = 0;
		virtual datatype_property get_property_by_index(int64_t idx) = 0;
		virtual int32_t index_of_field(string_table_str name) = 0;
		virtual int32_t index_of_field(int64_t field_index) = 0;


		virtual data_buffer<datatype_property> static_properties() = 0;
		virtual datatype_property find_static_property(string_table_str name) = 0;

		virtual string_table_ptr str_table() = 0;
		virtual function_node_buffer get_operator(string_table_str name) = 0;

		function_node_buffer get_predefined_operator(datatype_predefined_operators::_enum name)
		{
			return get_operator(str_table()->register_str(datatype_predefined_operators::to_string(name)));
		}

		//constructors specifically take a memory buffer as their first argument and return a pointer to the type which
		//may be null.  Essentially they are a type conversion from raw memory to a type.
		function_node_buffer constructors() 
		{ 
			return get_predefined_operator(datatype_predefined_operators::constructor); 
		}
		
		function_node_buffer assignment_operators()
		{
			return get_predefined_operator(datatype_predefined_operators::assignment);
		}
		//take a reference and return raw memory.  They are type conversion from a type to nothing.
		function_node_ptr destructor()
		{
			auto operators = get_predefined_operator(datatype_predefined_operators::destructor);
			if (operators.size())
				return operators[0];
			return nullptr;
		}

		//defaults to allow infinite overloads
		virtual void set_operator_policy(string_table_str name, datatype_operator_overload_policy::_enum overload_policy) = 0;
		virtual datatype_operator_overload_policy::_enum get_operator_policy(string_table_str name) = 0;
		virtual void define_operator(string_table_str operator_name, function_node_ptr fn) = 0;

		virtual void compile_first_pass(compiler_context& ctx) = 0;
		virtual void compile_second_pass(compiler_context& ctx) = 0;
		virtual llvm::StructType& llvm() = 0;
	};

	typedef datatype_node* datatype_node_ptr;

	struct module_symbol_type
	{
		enum _enum
		{
			unknown_symbol_type = 0,
			variable,
			function,
			datatype,
		};
	};

	template<typename dtype> struct module_symbol_type_id{};

	template<> struct module_symbol_type_id<variable_node_ptr> {
		static module_symbol_type::_enum type() {
			return module_symbol_type::variable;
		}
	};

	template<> struct module_symbol_type_id<function_node_buffer> {
		static module_symbol_type::_enum type() {
			return module_symbol_type::function;
		}
	};

	template<> struct module_symbol_type_id<datatype_node_ptr> {
		static module_symbol_type::_enum type() {
			return module_symbol_type::datatype;
		}
	};

	struct module_symbol_traits
	{
		enum _enum
		{
			buffer_size = sizeof(function_node_buffer),
		};

		static module_symbol_type::_enum empty_type() { return module_symbol_type::unknown_symbol_type; }
		typedef module_symbol_type::_enum id_type;
		template<typename dtype> static id_type typeof() { return module_symbol_type_id<dtype>::type(); }
		template<typename trettype, typename tvisitor>
		static trettype do_visit(char* data, id_type data_type, tvisitor visitor)
		{
			switch (data_type)
			{
			case module_symbol_type::variable: return visitor(*reinterpret_cast<variable_node_ptr*>(data));
			case module_symbol_type::function: return visitor(*reinterpret_cast<function_node_buffer*>(data));
			case module_symbol_type::datatype: return visitor(*reinterpret_cast<datatype_node_ptr*>(data));
			default: return visitor();
			}
		}
	};

	typedef variant_traits_impl < module_symbol_traits > module_symbol_traits_impl;

	struct module_symbol : public variant<module_symbol_traits_impl>
	{
		typedef variant<module_symbol_traits_impl> base;
		module_symbol(){}
		module_symbol(const module_symbol& other) : base(static_cast<const base&>(other)) {}
		module_symbol& operator=(const module_symbol& other) { base::operator=(other); return *this; }
		module_symbol(variable_node_ptr data) : base(data) {}
		module_symbol(function_node_buffer data) : base(data) {}
		module_symbol(datatype_node_ptr data) : base(data) {}
	};

	template<> struct variant_destruct<function_node_buffer> {
		void operator()(function_node_buffer&)
		{
		}
	};
	class module;
	typedef shared_ptr<module> module_ptr;


	struct variable_lookup_entry_type
	{
		enum _enum
		{
			unknown_entry_type = 0,
			string_table_str,
			int64,
			value,
		};
	};

	template<typename dtype>
	struct variable_lookup_type_id
	{
	};

	template<> struct variable_lookup_type_id<int64_t> {
		static variable_lookup_entry_type::_enum type() { return variable_lookup_entry_type::int64; }
	};

	template<> struct variable_lookup_type_id<string_table_str> {
		static variable_lookup_entry_type::_enum type() { return variable_lookup_entry_type::string_table_str; }
	};

	template<> struct variable_lookup_type_id<llvm::Value*> {
		static variable_lookup_entry_type::_enum type() { return variable_lookup_entry_type::value; }
	};


	struct variable_lookup_traits
	{
		typedef variable_lookup_entry_type::_enum id_type;
		enum {
			buffer_size = sizeof(int64_t),
		};
		static id_type empty_type() { return variable_lookup_entry_type::unknown_entry_type; }
		template<typename dtype>
		static id_type typeof() { return variable_lookup_type_id<dtype>::type(); }

		template<typename rettype, typename visitor>
		static rettype do_visit(char* data, id_type type, visitor v)
		{
			switch (type)
			{
			case variable_lookup_entry_type::string_table_str: return v(*reinterpret_cast<string_table_str*>(data));
			case variable_lookup_entry_type::int64: return v(*reinterpret_cast<int64_t*>(data));
			case variable_lookup_entry_type::value: return v(*reinterpret_cast<llvm::Value**>(data));
			default: break;
			}
			throw runtime_error("failed to visit type");
		}
	};

	template <> struct variant_destruct<string_table_str>
	{
		void operator()(string_table_str&)
		{
		}
	};

	typedef variant_traits_impl<variable_lookup_traits> variable_lookup_traits_impl;

	struct variable_lookup_entry : public variant<variable_lookup_traits_impl>
	{
		typedef variant<variable_lookup_traits_impl> base;
		variable_lookup_entry(){}
		variable_lookup_entry(int64_t data) : base(data){}
		variable_lookup_entry(string_table_str data) : base(data){}
		variable_lookup_entry(llvm::Value* data) : base(data){}
		variable_lookup_entry(const variable_lookup_entry& other) : base(static_cast<const base&>( other ) ) {}
		variable_lookup_entry& operator=(const variable_lookup_entry& other)
		{
			base::operator=(other);
			return *this;
		}
	};

	typedef vector<variable_lookup_entry> variable_lookup_entry_list;

	//Lookup chain is a chain of arguments passed to the get/set special forms.
	//member fields/accessors may be accessed by ordinal as well as name.
	struct variable_lookup_chain
	{
		qualified_name name;
		variable_lookup_entry_list lookup_chain;
		variable_lookup_chain(qualified_name _name = qualified_name()) : name(_name) {}
	};

	struct variable_lookup_typecheck_result
	{
		type_ref_ptr	type;
		bool			read;
		bool			write;

		variable_lookup_typecheck_result() : type(nullptr), read(false), write(false){}
		variable_lookup_typecheck_result(type_ref& _type, bool r, bool w)
			: type(&_type)
			, read(r)
			, write(w)
		{
		}
	};



	//modules are what are produced from reading lisp files.  They can be compiled down
	//into actual code.
	class module
	{
	protected:
		virtual ~module(){}
	public:
		friend class shared_ptr<module>;

		virtual string_table_ptr string_table() = 0;
		virtual variable_node_factory& define_variable(qualified_name name, type_ref& type) = 0;
		virtual function_factory& define_function(qualified_name name, named_type_buffer arguments, type_ref& rettype) = 0;
		//helper when you don't care about the argument names.
		function_factory& define_function(qualified_name name, type_ref_ptr_buffer arguments, type_ref& rettype);
		
		virtual datatype_node_factory& define_datatype(qualified_name name, type_ref& type) = 0;
		virtual module_symbol find_symbol(qualified_name name) = 0;
		virtual vector<module_symbol> symbols() = 0;
		variable_node_ptr find_variable(qualified_name name)
		{
			auto symbol = find_symbol(name);
			if (symbol.type() == module_symbol_type::variable)
				return symbol.data<variable_node_ptr>();
			return nullptr;
		}

		virtual datatype_node_ptr find_datatype(type_ref& type) = 0;
		datatype_node_ptr find_datatype(qualified_name name)
		{
			auto symbol = find_symbol(name);
			if (symbol.type() == module_symbol_type::datatype)
				return symbol.data<datatype_node_ptr>();
			return nullptr;
		}

		function_node_buffer find_function(qualified_name name)
		{
			auto symbol = find_symbol(name);
			if (symbol.type() == module_symbol_type::function)
				return symbol.data<function_node_buffer>();
			return function_node_buffer();
		}

		function_node_ptr find_function(qualified_name name, type_ref& fn_type)
		{
			auto buffer = find_function(name);
			for (size_t idx = 0, end = buffer.size(); idx < end; ++idx)
			{
				function_node_ptr fn = buffer[idx];
				if (&fn->type() == &fn_type)
					return fn;
			}
			return nullptr;
		}

		//A variable type check scope is anywhere we are created a local scope.  Upon entry to a function,
		//let, for, etc.  All these create scopes where new variables may be added to the record.
		virtual void begin_variable_type_check_scope() = 0;
		virtual void add_local_variable_type(string_table_str name, type_ref& type) = 0;
		virtual option<variable_lookup_typecheck_result> type_check_variable_access(const variable_lookup_chain& lookup_args) = 0;
		virtual void end_variable_type_check_scope() = 0;

		struct type_check_variable_scope
		{
			module_ptr module;
			type_check_variable_scope(module_ptr md)
				: module(md)
			{
				module->begin_variable_type_check_scope();
			}
			~type_check_variable_scope()
			{
				module->end_variable_type_check_scope();
			}
		};

		virtual void begin_variable_compilation_scope() = 0;
		virtual void add_local_variable(string_table_str name, type_ref& type, llvm::Value& value) = 0;
		virtual void add_void_local_variable(string_table_str name) = 0;
		virtual pair<llvm::Value*, type_ref_ptr> load_variable(compiler_context& context, const variable_lookup_chain& lookup_args) = 0;
		virtual void store_variable(compiler_context& context, const variable_lookup_chain& lookup_args, llvm::Value& value) = 0;
		virtual void end_variable_compilation_scope() = 0;

		struct compilation_variable_scope
		{
			module_ptr module;
			compilation_variable_scope(module_ptr md)
				: module(md)
			{
				module->begin_variable_compilation_scope();
			}
			~compilation_variable_scope()
			{
				module->end_variable_compilation_scope();
			}
		};


		virtual void append_init_ast_node(ast_node& node) = 0;
		virtual type_ref& init_return_type() = 0;
		virtual void compile_first_pass(compiler_context& ctx) = 0;
		virtual void compile_second_pass(compiler_context& ctx) = 0;
		//Returns the initialization function
		virtual llvm::Function& llvm() = 0;


		static shared_ptr<module> create_module(string_table_ptr st, type_library_ptr tl, qualified_name_table_ptr nt);
	};

	
}

#endif