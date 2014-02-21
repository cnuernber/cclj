//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/module.h"
#ifdef _WIN32
#pragma warning(push,2)
#endif
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/Scalar.h"
#ifdef _WIN32
#pragma warning(pop)
#endif

using namespace cclj;
using namespace llvm;


void accessor::compile_first_pass(compiler_context& ctx)
{
	if (getter) getter->compile_first_pass(ctx);
	if (setter) setter->compile_first_pass(ctx);
}

void accessor::compile_second_pass(compiler_context& ctx)
{
	if (getter) getter->compile_second_pass(ctx);
	if (setter) setter->compile_second_pass(ctx);
}

namespace
{
	struct variable_node_impl : public variable_node_factory, public variable_node
	{
		qualified_name		_name;
		type_ref&			_type;
		visibility::_enum	_visibility;
		void*				_user_value;


		llvm::GlobalVariable*	_variable;


		variable_node_impl(qualified_name name, type_ref& type)
			: _name(name)
			, _type(type)
			, _visibility(visibility::internal_visiblity)
			, _user_value(nullptr)
			, _variable(nullptr)
		{
		}
		virtual void set_visibility(visibility::_enum visibility)
		{
			_visibility = visibility;
		}

		virtual void set_value(void* value) { _user_value = value; }

		virtual variable_node& node()
		{
			return *this;
		}

		virtual type_ref& type() { return _type; }

		virtual qualified_name name() { return _name; }

		virtual visibility::_enum visibility() { return _visibility; }

		virtual void compile_first_pass(compiler_context& ctx)
		{
			auto llvm_type = ctx.type_ref_type(_type);
			if (!llvm_type)
				throw runtime_error("Failed to compile variable");

			_variable = new GlobalVariable(llvm_type.get()
				, true
				, ctx.visibility_to_linkage(_visibility)
				, NULL
				, ctx.qualified_name_to_llvm_name(_name).c_str());

			ctx._llvm_module.getGlobalList().push_back(_variable);
			ctx._eng.addGlobalMapping(_variable, _user_value);
		}

		virtual llvm::GlobalVariable& llvm_variable()
		{
			if (!_variable)
				throw runtime_error("compiler_first_pass not called or failed");
			return *_variable;
		}
	};



	struct function_node_impl : public function_factory, public function_node
	{
		qualified_name			_name;
		type_ref&				_return_type;
		vector<named_type>		_arguments;
		type_ref&				_function_type;
		vector<ast_node_ptr>	_body;
		void*					_external_body;
		compile_pass_fn			_user_body;
		visibility::_enum		_visibility;

		llvm::Function*			_function;


		function_node_impl(qualified_name nm, type_ref& rettype, named_type_buffer args, type_ref& fn_type)
			: _name(nm)
			, _return_type(rettype)
			, _function_type(fn_type)
			, _visibility(visibility::internal_visiblity)
			, _external_body( nullptr )
			, _function(nullptr)
		{
			_arguments.assign(args.begin(), args.end());
		}

		virtual void set_function_body(ast_node_buffer body) 
		{
			if (_user_body)
				throw runtime_error("functions may either be defined via external pointers or ast nodes but not both");
			if (_external_body)
				throw runtime_error("functions may either be defined via external pointers or ast nodes but not both");
			_body.assign(body.begin(), body.end()); 
		}

		virtual void set_function_body(void* fn_ptr)
		{
			if ( _user_body )
				throw runtime_error("functions may either be defined via external pointers or ast nodes but not both");
			if (_body.empty() == false)
				throw runtime_error("functions may either be defined via external pointers or ast nodes but not both");
			_external_body = fn_ptr;
		}

		virtual void set_function_override_body(compile_pass_fn fn)
		{
			if (_body.empty() == false)
				throw runtime_error("functions may either be defined via external pointers or ast nodes but not both");
			if (_external_body)
				throw runtime_error("functions may either be defined via external pointers or ast nodes but not both");
			_user_body = fn;
		}

		virtual void set_visibility(visibility::_enum visibility) { _visibility = visibility; }
		virtual function_node& node() { return *this; }

		virtual type_ref& return_type() { return _return_type; }
		virtual data_buffer<named_type> arguments() { return _arguments; }
		virtual visibility::_enum visibility() { return _visibility; }
		//a type composed of the fn name and the types as specializations
		virtual type_ref& type() { return _function_type; }
		virtual qualified_name name() { return _name; }
		virtual bool is_external() const { return _external_body != nullptr; }
		virtual ast_node_buffer get_function_body() { return _body; }
		virtual void*			get_function_external_body() { return _external_body; }
		virtual compile_pass_fn get_function_override_body() { return _user_body; }
		virtual void compile_first_pass(compiler_context& ctx)
		{
			vector<llvm_type_ptr> arg_types;
			vector<type_ref_ptr> cclj_arg_types;
			for_each(_arguments.begin(), _arguments.end(), [&]
				(named_type arg)
			{
				if (ctx._type_library->is_void_type(*arg.type) == false)
					arg_types.push_back(ctx.type_ref_type(*arg.type).get());
				cclj_arg_types.push_back(arg.type);
			});
			llvm_type_ptr rettype = ctx.type_ref_type(_return_type).get();
			FunctionType* fn_type = FunctionType::get(rettype, arg_types, false);
			string name_mangle(ctx.qualified_name_to_llvm_name(_name, cclj_arg_types));

			_function = Function::Create(fn_type
				, ctx.visibility_to_linkage(_visibility)
				, name_mangle.c_str()
				, &ctx._llvm_module);

			if ( _external_body )
				ctx._eng.addGlobalMapping(_function, _external_body);
		}

		static void initialize_function(compiler_context& context, Function& fn, data_buffer<named_type> fn_args )
		{
			size_t arg_idx = 0;
			for (Function::arg_iterator AI = fn.arg_begin(); arg_idx != fn_args.size();
				++AI, ++arg_idx)
			{
				named_type& fn_arg = fn_args[arg_idx];
				AI->setName(fn_arg.name.c_str());
			}

			// Create a new basic block to start insertion into.
			BasicBlock *function_entry_block = BasicBlock::Create(getGlobalContext(), "entry", &fn);
			context._builder.SetInsertPoint(function_entry_block);
			Function::arg_iterator AI = fn.arg_begin();
			IRBuilder<> entry_block_builder(&fn.getEntryBlock(), fn.getEntryBlock().begin());
			for (unsigned Idx = 0, e = fn_args.size(); Idx != e; ++Idx, ++AI) {
				named_type& arg_def(fn_args[Idx]);
				// Create an alloca for this variable.
				if (arg_def.type == nullptr) throw runtime_error("Invalid function argument");
				AllocaInst *Alloca = nullptr;
				if (arg_def.type != &context._type_library->get_void_type())
				{
					Alloca = entry_block_builder.CreateAlloca(context.type_ref_type(*arg_def.type).get()
						, 0, arg_def.name.c_str());

					// Store the initial value into the alloca.
					context._builder.CreateStore(AI, Alloca);
				}
				context._module->add_local_variable(arg_def.name, *arg_def.type, *Alloca );
			}
		}

		virtual void compile_second_pass(compiler_context& ctx)
		{
			if (_external_body == nullptr)
			{
				pair<llvm_value_ptr_opt, type_ref_ptr> last_statement(nullptr, nullptr);
				{
					compiler_scope_watcher _fn_scope(ctx);
					module::compilation_variable_scope fn_context(ctx._module);
					initialize_function(ctx, *_function, _arguments);

					if (_user_body)
					{
						last_statement = _user_body(ctx);
					}
					else
					{
						for (auto iter = _body.begin(), end = _body.end(); iter != end; ++iter)
						{
							ast_node& item = **iter;
							last_statement = item.compile_second_pass(ctx);
						}
					}
				}
				Value* retval = nullptr;
				if (last_statement.first.valid())
					retval = ctx._builder.CreateRet(last_statement.first.get());
				else
					ctx._builder.CreateRetVoid();
				verifyFunction(*_function);
				ctx._fpm.run(*_function);
			}
		}

		virtual llvm::Function& llvm()
		{
			if (!_function)
				throw runtime_error("first pass compilation not called");
			return *_function;
		}
	};

	template<typename keytype, typename valuetype>
	struct vectormap
	{
		typedef pair<keytype, valuetype>				entry_type;
		typedef pair<const keytype, valuetype>*			ordered_entry_type;
		typedef vector<ordered_entry_type>				vector_type;
		typedef unordered_map<keytype, valuetype>		map_type;

		vector_type		ordered_values;
		map_type		lookup_map;

		vectormap(){}

		pair<typename map_type::iterator, bool> insert(const entry_type& entry)
		{
			auto retval = lookup_map.insert(entry);
			if (retval.second)
				ordered_values.push_back(&(*retval.first));
			return retval;
		}

		typename vector_type::iterator ordered_begin() { return ordered_values.begin(); }
		typename vector_type::iterator ordered_end() { return ordered_values.end(); }

		typename map_type::iterator find(const keytype& key) { return lookup_map.find(key); }
		typename map_type::iterator end() { return lookup_map.end(); }
		typename map_type::iterator begin() { return lookup_map.begin(); }
	};




	struct datatype_node_impl : public datatype_node_factory, public datatype_node
	{
		struct operator_entry
		{
			datatype_operator_overload_policy::_enum	policy;
			vector<function_node_ptr>					operators;
			operator_entry()
				: policy(datatype_operator_overload_policy::allow_infinite_overloads)
			{}
			void compile_first_pass(compiler_context& ctx)
			{
				for_each(operators.begin(), operators.end(), [&](function_node_ptr fn)
				{
					fn->compile_first_pass(ctx);
				});
			}

			void compile_second_pass(compiler_context& ctx)
			{
				for_each(operators.begin(), operators.end(), [&](function_node_ptr fn)
				{
					fn->compile_second_pass(ctx);
				});
			}
		};

		typedef unordered_map<string_table_str, operator_entry > operator_map_type;
		typedef vectormap<string_table_str, datatype_property> property_map_type;

		string_table_ptr			_str_table;
		qualified_name				_name;
		type_ref&					_type;
		visibility::_enum           _visibility;
		property_map_type			_static_properties;
		property_map_type			_properties;
		operator_map_type			_operator_map;


		llvm::StructType*			_llvm_type;

		datatype_node_impl(string_table_ptr strt, qualified_name name, type_ref& type)
			: _str_table(strt)
			, _name(name)
			, _type(type)
			, _visibility(visibility::internal_visiblity)
			, _llvm_type(nullptr)
		{
			set_operator_policy(
				strt->register_str(datatype_predefined_operators::to_string(datatype_predefined_operators::destructor))
				, datatype_operator_overload_policy::allow_no_overloads);
		}

		virtual qualified_name name() const { return _name; }
		virtual visibility::_enum visibility()  { return _visibility; }
		virtual void set_visibility(visibility::_enum visibility) { _visibility = visibility; }
		void add_property(string_table_str name, datatype_property entry, property_map_type& property_map)
		{
			auto inserter = property_map.insert(make_pair(name, entry));
			if (inserter.second == false)
				throw runtime_error("Property already added");
		}
		virtual void add_field(named_type field)
		{
			add_property(field.name, field, _properties);
		}
		virtual datatype_node& node() { return *this; }


		virtual const type_ref& type() const { return _type; }

		virtual void add_static_field(named_type field) { add_property(field.name, field, _static_properties); }
		virtual vector<named_type> static_fields()
		{
			vector<named_type> retval;
			for_each(_static_properties.ordered_begin(), _static_properties.ordered_end(), [&](property_map_type::ordered_entry_type prop)
			{
				if (prop->second.type() == datatype_property_type::field)
					retval.push_back(prop->second.data<named_type>());
			});
			return retval;
		}

		virtual vector<named_type> fields()
		{
			vector<named_type> retval;
			for_each(_properties.ordered_begin(), _properties.ordered_end(), [&](property_map_type::ordered_entry_type prop)
			{
				if (prop->second.type() == datatype_property_type::field)
					retval.push_back(prop->second.data<named_type>());
			});
			return retval;
		}

		virtual void add_accessor(accessor access) { add_property(access.name, access, _properties); }

		virtual void add_static_accessor(accessor access) { add_property(access.name, access, _static_properties); }

		//A property may be either a field or accessor pair.
		virtual vector<datatype_property> properties()
		{
			vector<datatype_property> retval;
			for_each(_properties.ordered_begin(), _properties.ordered_end(), [&](property_map_type::ordered_entry_type prop)
			{
				retval.push_back(prop->second);
			});
			return retval;
		}

		virtual datatype_property find_property(string_table_str name)
		{
			auto iter = _properties.find(name);
			if (iter != _properties.end())
				return iter->second;
			return datatype_property();
		}

		virtual datatype_property get_property_by_index(int64_t idx)
		{
			if (idx >= 0 && idx < _properties.ordered_values.size())
				return _properties.ordered_values[(unsigned int)idx]->second;
			return datatype_property();
		}

		virtual int32_t index_of_field(string_table_str name)
		{
			int32_t field_idx = 0;
			for (auto iter = _properties.ordered_begin(), end = _properties.ordered_end(); iter != end; ++iter)
			{
				if ((*iter)->first == name)
				{
					if ((*iter)->second.type() != datatype_property_type::field)
						throw runtime_error("property is not a field");
					return field_idx;
				}
				if ((*iter)->second.type() == datatype_property_type::field)
					++field_idx;
			}
			throw runtime_error("Failed to find any property by name");
		}

		virtual int32_t index_of_field(int64_t prop_idx )
		{
			int32_t field_idx = 0;
			int64_t cur_prop_idx = 0;
			for (auto iter = _properties.ordered_begin(), end = _properties.ordered_end(); iter != end; ++iter, ++cur_prop_idx)
			{
				if (prop_idx == cur_prop_idx)
				{
					if ((*iter)->second.type() != datatype_property_type::field)
						throw runtime_error("property is not a field");
					return field_idx;
				}
				if ((*iter)->second.type() == datatype_property_type::field)
					++field_idx;
			}
			throw runtime_error("property index out of range");
		}

		virtual data_buffer<datatype_property> static_properties()
		{
			vector<datatype_property> retval;
			for_each(_static_properties.ordered_begin(), _static_properties.ordered_end(), [&](property_map_type::ordered_entry_type prop)
			{
				retval.push_back(prop->second);
			});
			return retval;
		}

		virtual datatype_property find_static_property(string_table_str name)
		{
			auto iter = _static_properties.find(name);
			if (iter != _static_properties.end())
				return iter->second;
			return datatype_property();
		}

		virtual string_table_ptr str_table() { return _str_table; }

		virtual function_node_buffer get_operator(string_table_str name)
		{
			operator_map_type::iterator iter = _operator_map.find(name);
			if (iter != _operator_map.end())
				return iter->second.operators;
			return function_node_buffer();
		}

		//defaults to allow infinite overloads
		virtual void set_operator_policy(string_table_str name, datatype_operator_overload_policy::_enum overload_policy)
		{
			auto iter = _operator_map.insert(make_pair(name, operator_entry()));
			iter.first->second.policy = overload_policy;
		}

		virtual datatype_operator_overload_policy::_enum get_operator_policy(string_table_str name)
		{
			auto iter = _operator_map.find(name);
			if (iter != _operator_map.end())
				return iter->second.policy;
			return datatype_operator_overload_policy::allow_infinite_overloads;
		}

		virtual void define_operator(string_table_str operator_name, function_node_ptr fn)
		{
			auto iter = _operator_map.insert(make_pair(operator_name, operator_entry()));
			operator_entry& entry(iter.first->second);
			if (entry.policy == datatype_operator_overload_policy::allow_no_overloads
				&& entry.operators.size())
				throw runtime_error("Operator that allows no overloads already assigned");
			entry.operators.push_back(fn);
		}

		virtual void compile_first_pass(compiler_context& ctx)
		{
			vector<named_type> my_fields = fields();
			auto full_type_name = ctx.qualified_name_to_llvm_name(_name, _type._specializations);

			vector<llvm_type_ptr> arg_types;
			for_each(my_fields.begin(), my_fields.end(), [&]
				(named_type field)
			{
				if (ctx._type_library->is_void_type(*field.type) == false)
					arg_types.push_back(ctx.type_ref_type(*field.type).get());
			});
			//Create struct type definition to llvm.
			_llvm_type = StructType::create(getGlobalContext(), arg_types);

			for_each(_properties.ordered_begin(), _properties.ordered_end(), [&](property_map_type::ordered_entry_type prop)
			{
				if (prop->second.type() == datatype_property_type::accessor)
				{
					accessor data = prop->second.data<accessor>();
					data.compile_first_pass(ctx);
				}
			});
			for_each(_operator_map.begin(), _operator_map.end(), [&](pair<string_table_str, operator_entry> op)
			{
				op.second.compile_first_pass(ctx);
			});
		}

		virtual void compile_second_pass(compiler_context& ctx)
		{
			for_each(_properties.ordered_begin(), _properties.ordered_end(), [&](property_map_type::ordered_entry_type prop)
			{
				if (prop->second.type() == datatype_property_type::accessor)
				{
					accessor data = prop->second.data<accessor>();
					data.compile_second_pass(ctx);
				}
			});
			for_each(_operator_map.begin(), _operator_map.end(), [&](pair<string_table_str, operator_entry> op)
			{
				op.second.compile_second_pass(ctx);
			});
		}

		virtual llvm::StructType& llvm()
		{
			if (_llvm_type == nullptr)
				throw runtime_error("compile first pass not called");
			return *_llvm_type;
		}
	};

	template<typename dtype> struct module_symbol_internal_type_id{};

	template<> struct module_symbol_internal_type_id<variable_node_ptr> {
		static module_symbol_type::_enum type() {
			return module_symbol_type::variable;
		}
	};

	template<> struct module_symbol_internal_type_id<vector<function_node_ptr> > {
		static module_symbol_type::_enum type() {
			return module_symbol_type::function;
		}
	};

	template<> struct module_symbol_internal_type_id<datatype_node_ptr> {
		static module_symbol_type::_enum type() {
			return module_symbol_type::datatype;
		}
	};



	struct module_symbol_internal_traits
	{
		enum _enum
		{
			buffer_size = sizeof(vector<function_node_ptr>),
		};

		static module_symbol_type::_enum empty_type() { return module_symbol_type::unknown_symbol_type; }
		typedef module_symbol_type::_enum id_type;
		template<typename dtype> static id_type typeof() { return module_symbol_internal_type_id<dtype>::type(); }
		template<typename trettype, typename tvisitor>
		static trettype do_visit(char* data, id_type data_type, tvisitor visitor)
		{
			switch (data_type)
			{
			case module_symbol_type::variable: return visitor(*reinterpret_cast<variable_node_ptr*>(data));
			case module_symbol_type::function: return visitor(*reinterpret_cast<vector<function_node_ptr>*>(data));
			case module_symbol_type::datatype: return visitor(*reinterpret_cast<datatype_node_ptr*>(data));
			default: return visitor();
			}
		}
	};

	typedef variant_traits_impl < module_symbol_internal_traits > module_symbol_internal_traits_impl;

	struct module_symbol_internal : public variant<module_symbol_internal_traits_impl>
	{
		typedef variant<module_symbol_internal_traits_impl> base;
		module_symbol_internal(){}
		module_symbol_internal(const module_symbol_internal& other) : base(static_cast<const base&>(other)) {}
		module_symbol_internal& operator=(const module_symbol_internal& other) { base::operator=(other); return *this; }
		module_symbol_internal(variable_node_ptr data) : base(data) {}
		module_symbol_internal(const vector<function_node_ptr>& data) : base(data) {}
		module_symbol_internal(datatype_node_ptr data) : base(data) {}
		module_symbol_internal(datatype_node_impl* data) : base(static_cast<datatype_node_ptr>( data ) ) {}

		operator module_symbol () const
		{
			switch (type())
			{
			case module_symbol_type::variable: return data<variable_node_ptr>();
			case module_symbol_type::function:
			{
				const vector<function_node_ptr>& item = data<vector<function_node_ptr> >();
				return function_node_buffer(const_cast<vector<function_node_ptr>&>(item));
			}
			case module_symbol_type::datatype: return data<datatype_node_ptr>();
			}
			return module_symbol();
		}
	};

	typedef vector<named_type> named_type_list;
	typedef vector<named_type_list> named_type_list_list;

	struct local_variable_entry
	{
		named_type		name;
		llvm::Value*	value;
		local_variable_entry() : value(nullptr) {}
		local_variable_entry(string_table_str name, type_ref& type, llvm::Value& value)
			: name(name, &type)
			, value(&value)
		{}
		local_variable_entry(string_table_str name, type_ref& void_type)
			: name(name, &void_type)
			, value(nullptr)
		{}
	};

	typedef vector<local_variable_entry> local_variable_entry_list;
	typedef vector<local_variable_entry_list> local_variable_entry_list_list;

	struct module_impl : public module
	{
		typedef vectormap<qualified_name, module_symbol_internal> symbol_map_type;
		typedef unordered_map<type_ref_ptr, datatype_node_ptr> type_datatype_map;

		string_table_ptr				_string_table;
		type_library_ptr				_type_library;
		qualified_name_table_ptr		_name_table;
		symbol_map_type					_symbol_map;
		vector<ast_node_ptr>			_init_statements;
		type_ref_ptr					_init_rettype;
		shared_ptr<function_node_impl>	_init_function;
		named_type_list_list			_local_variable_typecheck_stack;
		type_datatype_map				_datatypes;
		local_variable_entry_list_list	_local_variable_compile_stack;


		module_impl(string_table_ptr st
					, type_library_ptr tl
					, qualified_name_table_ptr nt) 
			: _string_table(st)
			, _type_library( tl )
			, _name_table( nt )
			, _init_rettype( nullptr )
		{}

		~module_impl()
		{
			for_each(_symbol_map.ordered_begin(), _symbol_map.ordered_end(), [this](symbol_map_type::ordered_entry_type& symbol)
			{
				delete_symbol(symbol->second);
			});
		}


		virtual string_table_ptr string_table() 
		{
			return _string_table;
		}


		void delete_symbol(module_symbol_internal& symbol)
		{
			switch (symbol.type())
			{
			case module_symbol_type::variable: delete static_cast<variable_node_impl*>(symbol.data<variable_node_ptr>()); break;
			case module_symbol_type::function:
			{
												 const vector<function_node_ptr> item = symbol.data<vector<function_node_ptr> >();
												 for_each(item.begin(), item.end(), [](function_node_ptr node)
												 {
													 delete static_cast<function_node_impl*>(node);
												 });

			}
				break;
			case module_symbol_type::datatype: delete static_cast<datatype_node_impl*>(symbol.data<datatype_node_ptr>()); break;
			}

		}

		void add_symbol(qualified_name name, module_symbol_internal& symbol)
		{
			auto inserter = _symbol_map.insert(make_pair(name, symbol));
			if (inserter.second == false)
			{
				delete_symbol(symbol);
				throw runtime_error("symbol already defined");
			}
			else
			{
				if (symbol.type() == module_symbol_type::datatype)
				{
					datatype_node_ptr dtype = symbol.data<datatype_node_ptr>();
					_datatypes.insert(make_pair(const_cast<type_ref_ptr>(&dtype->type()), dtype));
				}
			}
		}

		template<typename dtype>
		dtype add_symbol_t(qualified_name name, dtype dt)
		{
			module_symbol_internal symbol(dt);
			add_symbol(name, symbol);
			return dt;
		}

		virtual variable_node_factory& define_variable(qualified_name name, type_ref& type)
		{
			return *add_symbol_t(name, new variable_node_impl(name, type));
		}
		virtual function_factory& define_function(qualified_name name, named_type_buffer arguments, type_ref& rettype)
		{
			vector<function_node_ptr> insert_vec;
			module_symbol_internal insert_symbol(insert_vec);
			auto inserter = _symbol_map.insert(make_pair(name, insert_symbol));
			if (inserter.first->second.type() != module_symbol_type::function)
				throw runtime_error("failed to create function type");

			vector<function_node_ptr>& existing = inserter.first->second.data<vector<function_node_ptr> >();

			vector<type_ref_ptr> arg_buffer;
			for (size_t idx = 0, end = arguments.size(); idx < end; ++idx)
				arg_buffer.push_back(arguments[idx].type);

			type_ref& fn_type = _type_library->get_type_ref("fn", arg_buffer);

			for (size_t idx = 0, end = existing.size(); idx < end; ++idx)
			{
				if (&fn_type == &existing[idx]->type())
					throw runtime_error("function already defined");
			}

			auto retval = new function_node_impl(name, rettype, arguments, fn_type);

			existing.push_back(retval);

			return *retval;
		}

		//You can only add fields to a datatype once
		virtual datatype_node_factory& define_datatype(qualified_name name, type_ref& type)
		{
			return *add_symbol_t(name, new datatype_node_impl(_string_table, name, type));
		}

		virtual module_symbol find_symbol(qualified_name name)
		{
			auto finder = _symbol_map.find(name);
			if (finder != _symbol_map.end())
				return finder->second;
			return module_symbol();
		}


		virtual datatype_node_ptr find_datatype(type_ref& type)
		{
			auto iter = _datatypes.find(&type);
			if (iter != _datatypes.end())
				return iter->second;
			return nullptr;
		}

		virtual vector<module_symbol> symbols()
		{
			vector<module_symbol> retval;
			for_each(_symbol_map.ordered_begin(), _symbol_map.ordered_end(), [&](symbol_map_type::ordered_entry_type& entry)
			{
				retval.push_back(entry->second);
			});
			return retval;
		}



		virtual void begin_variable_type_check_scope()
		{
			_local_variable_typecheck_stack.push_back(named_type_list());
		}

		virtual void add_local_variable_type(string_table_str name, type_ref& type)
		{
			if (_local_variable_typecheck_stack.empty())
				throw runtime_error("variable added with empty type check stack");
			_local_variable_typecheck_stack.back().push_back(named_type(name, &type));
		}

		variable_lookup_typecheck_result type_check_property(datatype_property& prop, bool can_read, bool can_write)
		{
			
			switch (prop.type())
			{
			case datatype_property_type::field:
				return variable_lookup_typecheck_result(*prop.data<named_type>().type, can_read, can_write);
			case datatype_property_type::accessor:
			{
				auto prop_access = prop.data<accessor>();
				return variable_lookup_typecheck_result(*prop_access.type, can_read && prop_access.getter, can_write && prop_access.setter);
			}
				break;
			}
			return variable_lookup_typecheck_result();
		}

		virtual option<variable_lookup_typecheck_result> type_check_variable_access(const variable_lookup_chain& lookup_args)
		{
			type_ref_ptr base_variable_type = nullptr;
			if (lookup_args.name.names().size() == 1)
			{
				string_table_str var_name = lookup_args.name.names()[0];
				//run through the stack backwards looking for variables.
				for (size_t stack_idx = 0, stack_end = _local_variable_typecheck_stack.size()
							; stack_idx < stack_end && base_variable_type == nullptr
							; ++stack_idx)
				{
					const named_type_list& variables = _local_variable_typecheck_stack[stack_end - stack_idx - 1];
					for (size_t var_idx = 0, var_end = variables.size()
						; var_idx < var_end && base_variable_type == nullptr
						; ++var_idx)
					{
						const named_type& var_entry = variables[var_end - var_idx - 1];
						if (var_entry.name == var_name)
							base_variable_type = var_entry.type;
					}
				}
			}
			if (base_variable_type == nullptr)
			{
				auto find_result = _symbol_map.find(lookup_args.name);
				if (find_result != _symbol_map.end())
				{
					const module_symbol_internal& symbol = find_result->second;
					switch (symbol.type())
					{
					case module_symbol_type::variable:
						base_variable_type = &symbol.data<variable_node_ptr>()->type();
						break;
					case module_symbol_type::function:
					{
						const vector<function_node_ptr>& functions = symbol.data<vector<function_node_ptr> >();
						if (functions.size() == 1)
							base_variable_type = &_type_library->get_ptr_type(functions[0]->type());
						else
							throw runtime_error("ambiguous function pointer reference to overloaded function");
					}
						break;
					case module_symbol_type::datatype:
						throw runtime_error("unimplemented");
					}
				}
			}
			if (base_variable_type == nullptr)
				throw runtime_error("failed to resolve symbol");

			bool can_read = true;
			bool can_write = true;

			for (size_t idx = 0, end = lookup_args.lookup_chain.size()
				; idx < end && base_variable_type != nullptr; ++idx)
			{
				const variable_lookup_entry& entry = lookup_args.lookup_chain[idx];
				switch (entry.type())
				{
				case variable_lookup_entry_type::string_table_str:
				{
					auto dtype_ptr = find_datatype(*base_variable_type);
					base_variable_type = nullptr;
					if (dtype_ptr)
					{
						auto prop_entry = dtype_ptr->find_property(entry.data<string_table_str>());
						auto result = type_check_property(prop_entry, can_read, can_write);
						base_variable_type = result.type;
						can_read = result.read;
						can_write = result.write;
					}
				}
					break;
				
					//these work on ptr types, array types, and datatypes
				case variable_lookup_entry_type::int64:
				{
					//the actual uint32 value is ignored at this point.
					if (_type_library->is_pointer_type(*base_variable_type))
					{
						base_variable_type = &_type_library->deref_ptr_type(*base_variable_type);
					}
					else
					{
						auto dtype_ptr = find_datatype(*base_variable_type);
						base_variable_type = nullptr;
						if (dtype_ptr)
						{
							auto prop_entry = dtype_ptr->get_property_by_index(entry.data<int64_t>());
							auto result = type_check_property(prop_entry, can_read, can_write);
							base_variable_type = result.type;
							can_read = result.read;
							can_write = result.write;
						}
					}
				}
					break;
				case variable_lookup_entry_type::value:
					if (_type_library->is_pointer_type(*base_variable_type))
					{
						base_variable_type = &_type_library->deref_ptr_type(*base_variable_type);
					}
					else
						base_variable_type = nullptr;
					break;
				}
			}
			if (base_variable_type != nullptr)
				return variable_lookup_typecheck_result(*base_variable_type, can_read, can_write);
			else
				return option<variable_lookup_typecheck_result>();
		}

		virtual void end_variable_type_check_scope()
		{
			if (_local_variable_typecheck_stack.empty())
				throw runtime_error("variable added with empty type check stack");
			_local_variable_typecheck_stack.pop_back();
		}


		virtual void begin_variable_compilation_scope()
		{
			_local_variable_compile_stack.push_back(local_variable_entry_list());
		}

		virtual void add_local_variable(string_table_str name, type_ref& type, llvm::Value& value)
		{
			if (_local_variable_compile_stack.empty())
				throw runtime_error("invalid local variable management");
			_local_variable_compile_stack.back().push_back(local_variable_entry(name, type, value));
		}

		virtual void add_void_local_variable(string_table_str name)
		{
			if (_local_variable_compile_stack.empty())
				throw runtime_error("invalid local variable management");

			_local_variable_compile_stack.back().push_back(local_variable_entry(name, _type_library->get_void_type()));
		}

		struct variable_lookup_resolution_result
		{
			llvm::Value*			initial_resolution;
			type_ref_ptr			final_type;
			bool					is_stack;
			vector<llvm::Value*>	GEPArgs;

			variable_lookup_resolution_result()
				: initial_resolution(nullptr)
				, final_type(nullptr)
				, is_stack(false)
			{
			}
		};

		variable_lookup_resolution_result lookup_compile_variable(const variable_lookup_chain& lookup_args)
		{
			variable_lookup_resolution_result retval;

			if (lookup_args.name.names().size() == 1)
			{
				string_table_str var_name = lookup_args.name.names()[0];
				//run through the stack backwards looking for variables.
				for (size_t stack_idx = 0, stack_end = _local_variable_compile_stack.size()
					; stack_idx < stack_end && retval.initial_resolution == nullptr
					; ++stack_idx)
				{
					const local_variable_entry_list& variables = _local_variable_compile_stack[stack_end - stack_idx - 1];
					for (size_t var_idx = 0, var_end = variables.size()
						; var_idx < var_end && retval.initial_resolution == nullptr
						; ++var_idx)
					{
						const local_variable_entry& var_entry = variables[var_end - var_idx - 1];
						if (var_entry.name.name == var_name)
						{
							retval.initial_resolution = var_entry.value;
							retval.final_type = var_entry.name.type;
							retval.is_stack = true;
						}
					}
				}
			}
			if (retval.initial_resolution == nullptr)
			{
				auto find_result = _symbol_map.find(lookup_args.name);
				if (find_result != _symbol_map.end())
				{
					module_symbol_internal& symbol = find_result->second;
					switch (symbol.type())
					{
						//Need a special way to get function pointers for overloaded functions.
						//This is probably not the way to do it.
					case module_symbol_type::function:
						break;
					case module_symbol_type::variable:
					{
						variable_node_ptr variable = symbol.data<variable_node_ptr>();
						retval.initial_resolution = &variable->llvm_variable();
						retval.final_type = &variable->type();
					}
						break;
					}
				}
			}

			if (retval.initial_resolution && lookup_args.lookup_chain.size())
			{
				//bailing to only handle variable lookups. Accessors can come later.
				if (retval.is_stack)
					retval.GEPArgs.push_back(llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(llvm::getGlobalContext()), 0));
				for (size_t idx = 0, end = lookup_args.lookup_chain.size(); 
					idx < end && retval.final_type != nullptr ; ++idx)
				{
					const variable_lookup_entry& lookup_entry(lookup_args.lookup_chain[idx]);
					switch (lookup_entry.type())
					{
					case variable_lookup_entry_type::string_table_str:
					{
						auto str = lookup_entry.data<string_table_str>();
						auto dtype = find_datatype(*retval.final_type);
						retval.final_type = nullptr;
						if (dtype)
						{
							auto prop = dtype->find_property(str);
							if (prop.type() == datatype_property_type::field)
							{
								int32_t val_idx = dtype->index_of_field(str);
								retval.GEPArgs.push_back(llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(llvm::getGlobalContext()), val_idx));
								retval.final_type = prop.data<named_type>().type;
							}
							else
								retval.final_type = nullptr;
						}
					}
						break;
					case variable_lookup_entry_type::int64:
					{
						auto val_idx = static_cast<int32_t>( lookup_entry.data<int64_t>() );
						if (_type_library->is_pointer_type(*retval.final_type))
						{
							retval.GEPArgs.push_back(llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(llvm::getGlobalContext()), val_idx));
							retval.final_type = &_type_library->deref_ptr_type(*retval.final_type);
						}
						else
						{
							auto dtype = find_datatype(*retval.final_type);
							auto prop = dtype->get_property_by_index(val_idx);
							if (prop.type() == datatype_property_type::field)
							{
								int32_t field_idx = dtype->index_of_field(val_idx);
								retval.GEPArgs.push_back(llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(llvm::getGlobalContext()), field_idx));
								retval.final_type = prop.data<named_type>().type;
							}
						}
					}
						break;
					case variable_lookup_entry_type::value:
					{
						auto value = lookup_entry.data<llvm::Value*>();
						//need to cast this to a 32 bit integer else kaboom in the gep instr itself.
						retval.GEPArgs.push_back(value);
						retval.final_type = &_type_library->deref_ptr_type(*retval.final_type);
					}
						break;
					}
				}
			}
			if (retval.final_type)
				return retval;
			else
				return variable_lookup_resolution_result();
		}


		virtual pair<llvm::Value*, type_ref_ptr> load_variable(compiler_context& context, const variable_lookup_chain& lookup_args)
		{
			variable_lookup_resolution_result lookup_result = lookup_compile_variable(lookup_args);
			if (lookup_result.initial_resolution)
			{
				llvm::Value* loaded_value = nullptr;
				if (lookup_result.GEPArgs.size())
				{
					llvm::Value* new_ptr = context._builder.CreateGEP(lookup_result.initial_resolution, lookup_result.GEPArgs);
					loaded_value = context._builder.CreateLoad(new_ptr);
				}
				else
				{
					loaded_value = lookup_result.initial_resolution;
					if (lookup_result.is_stack)
						loaded_value = context._builder.CreateLoad(loaded_value);
				}
				return pair<llvm::Value*, type_ref_ptr>(loaded_value, lookup_result.final_type);
			}
			return pair<llvm::Value*, type_ref_ptr>(nullptr, nullptr);
		}

		virtual void store_variable(cclj::compiler_context& context, const variable_lookup_chain& lookup_args, llvm::Value& value)
		{
			variable_lookup_resolution_result lookup_result = lookup_compile_variable(lookup_args);
			if (lookup_result.initial_resolution)
			{
				if (lookup_result.GEPArgs.size())
				{
					llvm::Value* new_ptr = context._builder.CreateGEP(lookup_result.initial_resolution, lookup_result.GEPArgs);
					context._builder.CreateStore(&value, new_ptr);
				}
				else
				{
					llvm::Value* loaded_value = lookup_result.initial_resolution;
					if (lookup_result.is_stack)
						loaded_value = context._builder.CreateStore(&value, loaded_value);
				}
			}
		}
		virtual void end_variable_compilation_scope()
		{
			if (_local_variable_compile_stack.empty())
				throw runtime_error("invalid local variable management");
			_local_variable_compile_stack.pop_back();
		}

		virtual void append_init_ast_node(ast_node& node)
		{
			_init_statements.push_back(&node);
			_init_rettype = &node.type();
		}
		virtual type_ref& init_return_type()
		{
			if (!_init_rettype)
				_init_rettype = &_type_library->get_void_type();
			return *_init_rettype;
		}
		virtual void compile_first_pass(compiler_context& ctx)
		{
			for_each(_symbol_map.ordered_begin(), _symbol_map.ordered_end(), [&](symbol_map_type::ordered_entry_type& symbol_entry)
			{
				module_symbol_internal& symbol = symbol_entry->second;
				switch (symbol.type())
				{
				case module_symbol_type::variable:
					symbol.data<variable_node_ptr>()->compile_first_pass(ctx);
					break;
				case module_symbol_type::function:
				{
					vector<function_node_ptr>& fn_data = symbol.data<vector<function_node_ptr> >();
					for_each(fn_data.begin(), fn_data.end(), [&](function_node_ptr fn)
					{
						fn->compile_first_pass(ctx);
					});
				}
					break;
				case module_symbol_type::datatype:
					symbol.data<datatype_node_ptr>()->compile_first_pass(ctx);
					break;
				default:
					throw runtime_error("unrecognized symbol type");
				}
			});
			//qualified_name nm, type_ref& rettype, named_type_buffer args, type_ref& fn_type
			type_ref& fn_type = _type_library->get_type_ref("fn");
			vector<string_table_str> name_args;
			name_args.push_back(_string_table->register_str("module_init"));
			qualified_name nm = _name_table->register_name(name_args);
			if (_init_rettype == nullptr)
				_init_rettype = &_type_library->get_void_type();
			_init_function = make_shared<function_node_impl>(nm, *_init_rettype, named_type_buffer(), fn_type);
			_init_function->set_function_body(_init_statements);
			_init_function->compile_first_pass(ctx);
		}
		virtual void compile_second_pass(compiler_context& ctx)
		{
			for_each(_symbol_map.ordered_begin(), _symbol_map.ordered_end(), [&](symbol_map_type::ordered_entry_type& symbol_entry)
			{
				module_symbol_internal& symbol = symbol_entry->second;
				switch (symbol.type())
				{
				case module_symbol_type::variable:
					break;
				case module_symbol_type::function:
				{
					vector<function_node_ptr>& fn_data = symbol.data<vector<function_node_ptr> >();
					for_each(fn_data.begin(), fn_data.end(), [&](function_node_ptr fn)
					{
						fn->compile_second_pass(ctx);
					});
				}
					break;
				case module_symbol_type::datatype:
					symbol.data<datatype_node_ptr>()->compile_second_pass(ctx);
					break;
				default:
					throw runtime_error("unrecognized symbol type");
				}
			});
			_init_function->compile_second_pass(ctx);
		}
		//Returns the initialization function
		virtual llvm::Function& llvm()
		{
			if (!_init_function)
				throw runtime_error("compile first pass has not been called yet");
			return _init_function->llvm();
		}
	};
}


function_factory& module::define_function(qualified_name name, type_ref_ptr_buffer arguments, type_ref& rettype)
{
	stringstream str_builder;
	vector<named_type> arg_buffer(arguments.size());
	for (size_t idx = 0, end = arguments.size(); idx < end; ++idx)
	{
		str_builder.clear();
		str_builder << "arg" << idx;
		arg_buffer[idx] = named_type(string_table()->register_str(str_builder.str().c_str()), arguments[idx]);
	}
	return define_function(name, named_type_buffer(arg_buffer), rettype);
}

shared_ptr<module> module::create_module(string_table_ptr st, type_library_ptr tl, qualified_name_table_ptr nt)
{
	return make_shared<module_impl>(st, tl, nt);
}