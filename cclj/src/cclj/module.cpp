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

			ctx._module.getGlobalList().push_back(_variable);
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
		visibility::_enum		_visibility;

		llvm::Function*			_function;


		function_node_impl(qualified_name nm, type_ref& rettype, named_type_buffer args, type_ref& fn_type)
			: _name(nm)
			, _return_type(rettype)
			, _function_type(fn_type)
			, _visibility(visibility::internal_visiblity)
			, _function(nullptr)
		{
			_arguments.assign(args.begin(), args.end());
		}

		virtual void set_function_body(ast_node_buffer body) { _body.assign(body.begin(), body.end()); }

		virtual void set_visibility(visibility::_enum visibility) { _visibility = visibility; }
		virtual function_node& node() { return *this; }

		virtual type_ref& return_type() { return _return_type; }
		virtual data_buffer<named_type> arguments() { return _arguments; }
		virtual visibility::_enum visibility() { return _visibility; }
		//a type composed of the fn name and the types as specializations
		virtual type_ref& type() { return _function_type; }
		virtual qualified_name name() { return _name; }
		virtual ast_node_buffer get_function_body() { return _body; }
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
				, &ctx._module);
		}

		static void initialize_function(compiler_context& context, Function& fn, data_buffer<named_type> fn_args
			, variable_context& var_context)
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
				var_context.add_variable(arg_def.name, Alloca, *arg_def.type);
			}
		}

		virtual void compile_second_pass(compiler_context& ctx)
		{
			pair<llvm_value_ptr_opt, type_ref_ptr> last_statement(nullptr, nullptr);
			{
				compiler_scope_watcher _fn_scope(ctx);
				variable_context fn_context(ctx._variables);
				initialize_function(ctx, *_function, _arguments, fn_context);

				for (auto iter = _body.begin(), end = _body.end(); iter != end; ++iter)
				{
					ast_node& item = **iter;
					last_statement = item.compile_second_pass(ctx);
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

	template<> struct module_symbol_internal_type_id<variable_node> {
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
				const vector<function_node_ptr> item = data<vector<function_node_ptr> >();
				return function_node_buffer(const_cast<vector<function_node_ptr>&>(item));
			}
			case module_symbol_type::datatype: return data<datatype_node_ptr>();
			}
			return module_symbol();
		}
	};


	struct module_impl : public module
	{
		typedef vectormap<qualified_name, module_symbol_internal> symbol_map_type;
		string_table_ptr			_string_table;
		type_library_ptr			_type_library;
		qualified_name_table_ptr	_name_table;
		symbol_map_type				_symbol_map;
		vector<ast_node_ptr>		_init_statements;
		type_ref_ptr				_init_rettype;
		shared_ptr<function_node_impl>			_init_function;

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
			for_each(_symbol_map.begin(), _symbol_map.end(), [this](module_symbol_internal& symbol)
			{
				delete_symbol(symbol);
			});
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
			vector<type_ref_ptr> arg_type_buffer; 
			for_each(arguments.begin(), arguments.end(), [&](named_type& nt)
			{
				arg_type_buffer.push_back(nt.type);
			});
			type_ref& fn_type = _type_library->get_type_ref("fn", arg_type_buffer);
			return *add_symbol_t(name, new function_node_impl(name, rettype, arguments, fn_type));
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

		virtual vector<module_symbol> symbols()
		{
			vector<module_symbol> retval;
			for_each(_symbol_map.ordered_begin(), _symbol_map.ordered_end(), [&](symbol_map_type::ordered_entry_type& entry)
			{
				retval.push_back(entry->second);
			});
		}

		virtual void append_init_ast_node(ast_node& node)
		{
			_init_statements.push_back(&node);
			_init_rettype = &node.type();
		}
		virtual type_ref_ptr init_return_type()
		{
			return _init_rettype;
		}
		virtual void compile_first_pass(compiler_context& ctx)
		{
			for_each(_symbol_map.ordered_begin(), _symbol_map.ordered_end(), [&](module_symbol_internal& symbol)
			{
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
			_init_function = make_shared<function_node_impl>(nm, _init_rettype, named_type_buffer(), fn_type);
			_init_function->set_function_body(_init_statements);
			_init_function->compile_first_pass(ctx);
		}
		virtual void compile_second_pass(compiler_context& ctx)
		{
			for_each(_symbol_map.ordered_begin(), _symbol_map.ordered_end(), [&](module_symbol_internal& symbol)
			{
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