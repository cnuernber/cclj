//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/plugins/base_plugins.h"
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

#include "cclj/llvm_base_numeric_type_helper.h"

using namespace cclj;
using namespace cclj::lisp;
using namespace cclj::plugins;
using namespace llvm;

#define CCLJ_BASE_PLUGINS_DESTRUCT_AST_NODE(data_type) \
} namespace cclj { CCLJ_SLAB_ALLOCATOR_REQUIRES_DESTRUCTION(data_type) } namespace {

namespace
{
	struct function_call_ast_node : public ast_node
	{
		function_node_ptr _function;
		function_call_ast_node(function_node_ptr fn) : ast_node(fn->return_type()), _function(fn)
		{
		}

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			vector<llvm::Value*> fn_args;
			for (auto iter = children().begin(), end = children().end(); iter != end; ++iter)
			{
				ast_node& node(*iter);
				auto pass_result = node.compile_second_pass(context).first;
				if (pass_result)
					fn_args.push_back(pass_result.get());
			}

			type_ref& rettype = _function->return_type();
			const char* twine = "calltmp";
			bool is_void = &rettype == &context._type_library->get_void_type();
			if (is_void)
				twine = "";
			Value* retval = context._builder.CreateCall(&_function->llvm(), fn_args, twine);
			if (is_void)
				retval = nullptr;
			return make_pair(retval
				, &rettype);
		}
	};

	CCLJ_BASE_PLUGINS_DESTRUCT_AST_NODE(function_call_ast_node)

	ast_node& type_check_function_application(reader_context& context, lisp::cons_cell& cell)
	{
		symbol& fn_name = object_traits::cast_ref<symbol>(cell._value);
		function_node_buffer functions = context._module->find_function(context._name_table->register_name(fn_name._name));
		if (functions.size() == 0)
			throw runtime_error("unable to resolve function");

		vector<type_ref_ptr> arg_types;
		vector<ast_node_ptr> resolved_args;
		//ensure we can find the function definition.
		for (cons_cell* arg = object_traits::cast<cons_cell>(cell._next)
			; arg; arg = object_traits::cast<cons_cell>(arg->_next))
		{
			ast_node& eval_result = context._type_checker(arg->_value);
			arg_types.push_back(&eval_result.type());
			resolved_args.push_back(&eval_result);
		}

		function_node_ptr result_function(nullptr);
		for (size_t idx = 0, end = functions.size(); idx < end; ++idx)
		{
			result_function = functions[idx];
			auto fn_args = result_function->arguments();
			if (fn_args.size() == arg_types.size())
			{
				size_t arg_idx, arg_end;
				for (arg_idx = 0, arg_end = fn_args.size(); arg_idx < arg_end; ++arg_idx)
				{
					if (fn_args[arg_idx].type != arg_types[arg_idx])
						break;
				}
				if (arg_idx == arg_end)
					break;
			}
		}

		if (result_function == nullptr)
			throw runtime_error("no function found with matching arguments types");

		function_call_ast_node* new_node = context._ast_allocator->construct<function_call_ast_node>(result_function);
		for_each(resolved_args.begin(), resolved_args.end(), [&](ast_node_ptr arg_node) { new_node->children().push_back(*arg_node); });
		return *new_node;
	}
	vector<string> split_symbol(symbol& sym)
	{
		vector<string> retval;
		string temp(sym._name.c_str());
		size_t last_offset = 0;
		for (size_t off = temp.find('.'); off != string::npos;
			off = temp.find('.', off + 1))
		{
			retval.push_back(temp.substr(last_offset, off - last_offset));
			last_offset = off + 1;
		}
		if (last_offset < temp.size())
		{
			retval.push_back(temp.substr(last_offset, temp.size() - last_offset));
		}
		return retval;
	}
}

ast_node& base_language_plugins::type_check_apply(reader_context& context, lisp::cons_cell& cell)
{
	symbol& fn_name = object_traits::cast_ref<symbol>(cell._value);

	auto plugin_ptr_iter = context._special_forms->find(fn_name._name);
	if (plugin_ptr_iter != context._special_forms->end())
	{
		auto node_ptr = plugin_ptr_iter->second->type_check(context, cell);
		if (node_ptr) return *node_ptr;

		throw runtime_error("Failed to type check apply");
	}
	return type_check_function_application(context, cell);
}

namespace {
	struct variable_chain_ast_node : public ast_node
	{
		variable_lookup_chain _chain;
		variable_chain_ast_node(const variable_lookup_chain& ch, const type_ref& type) : ast_node(type), _chain(ch)
		{
		}

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			return context._module->load_variable(context, _chain);
		}
	};

	CCLJ_BASE_PLUGINS_DESTRUCT_AST_NODE(variable_chain_ast_node)
}

ast_node& base_language_plugins::type_check_symbol(reader_context& context, lisp::symbol& sym)
{
	symbol& cell_symbol = sym;
	vector<string> parts = split_symbol(cell_symbol);
	if (parts.size() == 1)
	{
		//common case.
		variable_lookup_chain var_chain(context._name_table->register_name(parts[0]));
		auto check_result = context._module->type_check_variable_access(var_chain);
		if (check_result.valid() == false)
			throw runtime_error("Invalid type check results");
		return *context._ast_allocator->construct<variable_chain_ast_node>(var_chain, *check_result.value().type);
	}
	else
	{
		throw runtime_error("compound symbols not supported yet");
	}
}

namespace
{
	template<typename num_type>
	struct str_to_num
	{
	};

	//Lots of things to do here.  First would be to allow compile time expressions as constants and not just numbers.
	//Second would be to allow number of different bases
	//third would be to have careful checking of ranges.
	template<> struct str_to_num<bool> { static bool parse(const string& val) { return std::stol(val) ? true : false; } };
	template<> struct str_to_num<uint8_t> { static uint8_t parse(const string& val) { return static_cast<uint8_t>(std::stol(val)); } };
	template<> struct str_to_num<int8_t> { static int8_t parse(const string& val) { return static_cast<int8_t>(std::stol(val)); } };
	template<> struct str_to_num<uint16_t> { static uint16_t parse(const string& val) { return static_cast<uint16_t>(std::stol(val)); } };
	template<> struct str_to_num<int16_t> { static int16_t parse(const string& val) { return static_cast<int16_t>(std::stol(val)); } };
	template<> struct str_to_num<uint32_t> { static uint32_t parse(const string& val) { return static_cast<uint32_t>(std::stoll(val)); } };
	template<> struct str_to_num<int32_t> { static int32_t parse(const string& val) { return static_cast<int32_t>(std::stoll(val)); } };
	template<> struct str_to_num<uint64_t> { static uint64_t parse(const string& val) { return static_cast<uint64_t>(std::stoll(val)); } };
	template<> struct str_to_num<int64_t> { static int64_t parse(const string& val) { return static_cast<int64_t>(std::stoll(val)); } };
	template<> struct str_to_num<float> { static float parse(const string& val) { return static_cast<float>(std::stof(val)); } };
	template<> struct str_to_num<double> { static double parse(const string& val) { return static_cast<double>(std::stod(val)); } };


	template<typename number_type>
	uint8_t* parse_constant_value(reader_context& context, const std::string& val)
	{
		number_type parse_val = str_to_num<number_type>::parse(val);
		uint8_t* retval = context._factory->allocate_data(sizeof(number_type), sizeof(number_type));
		memcpy(retval, &parse_val, sizeof(number_type));
		return retval;
	}
	struct numeric_constant_ast_node : public ast_node
	{
		uint8_t*	_data;
		numeric_constant_ast_node(string_table_ptr st, uint8_t* data, const type_ref& dtype)
			: ast_node(dtype)
			, _data(data)
		{
		}

		virtual bool executable_statement() const { return true; }

		virtual uint32_t eval_to_uint32(type_library& library) const
		{
			switch (library.to_base_numeric_type(type()))
			{
			case base_numeric_types::f32:
				return static_cast<uint32_t>(*reinterpret_cast<float*>(_data));
			case base_numeric_types::f64:
				return static_cast<uint32_t>(*reinterpret_cast<double*>(_data));
			case base_numeric_types::i8:
			case base_numeric_types::u8:
				return static_cast<uint32_t>(*reinterpret_cast<uint8_t*>(_data));
			case base_numeric_types::i16:
			case base_numeric_types::u16:
				return static_cast<uint32_t>(*reinterpret_cast<uint16_t*>(_data));
			case base_numeric_types::i32:
			case base_numeric_types::u32:
				return static_cast<uint32_t>(*reinterpret_cast<uint32_t*>(_data));
			case base_numeric_types::i64:
			case base_numeric_types::u64:
				return static_cast<uint32_t>(*reinterpret_cast<uint64_t*>(_data));
			}
			throw runtime_error("invalid numeric type for numeric constant");
		}

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			switch (context._type_library->to_base_numeric_type(type()))
			{
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name )		\
			case base_numeric_types::name:			\
			return make_pair(llvm_helper::llvm_constant_map<base_numeric_types::name>::parse(_data)	\
			, &type());
				CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
			default: break;
			}
			throw runtime_error("bad numeric constant");
		}
	};

}

ast_node& base_language_plugins::type_check_numeric_constant(reader_context& context, lisp::constant& cell)
{
	constant& cell_constant = cell;
	type_ref_ptr constant_type = nullptr;
	string number_string(cell_constant._unparsed_number.c_str());
	if (cell_constant._unevaled_type)
		constant_type = &context._type_evaluator(*cell_constant._unevaled_type);
	else
	{
		if (number_string.find('.'))
			constant_type = &context._type_library->get_type_ref(base_numeric_types::f64);
		else
			constant_type = &context._type_library->get_type_ref(base_numeric_types::i64);
	}
	uint8_t* num_value(nullptr);
	switch (context._type_library->to_base_numeric_type(*constant_type))
	{
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name )	\
	case base_numeric_types::name:	\
	num_value			\
	= parse_constant_value<numeric_type_to_c_type_map<base_numeric_types::name>::numeric_type>(context, number_string);	\
	break;
		CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
	default:
		throw runtime_error("Invalid constant type");
	}
	if (context._type_library->to_base_numeric_type(*constant_type)
		== base_numeric_types::no_known_type) throw runtime_error("invalid base numeric type");
	return *context._ast_allocator->construct<numeric_constant_ast_node>(context._string_table, num_value, *constant_type);
}

namespace {
	class defn_compiler_plugin : public compiler_plugin
	{
		virtual ast_node* type_check(reader_context& context, lisp::cons_cell& cell)
		{
			cons_cell& fn_name_cell = object_traits::cast_ref<cons_cell>(cell._next);
			symbol& fn_name = object_traits::cast_ref<symbol>(fn_name_cell._value);
			if (fn_name._unevaled_type == nullptr)
				throw runtime_error("Function definitions must have a type");

			context.symbol_type(fn_name);
			cons_cell& arg_array_cell = object_traits::cast_ref<cons_cell>(fn_name_cell._next);
			array&  arg_array = object_traits::cast_ref<array>(arg_array_cell._value);
			cons_cell& body = object_traits::cast_ref<cons_cell>(arg_array_cell._next);
			
			vector<named_type> fn_args;
			module::type_check_variable_scope typecheck_variable_scope(context._module);
			for (size_t idx = 0, end = arg_array._data.size(); idx < end; ++idx)
			{
				symbol& arg_symbol = object_traits::cast_ref<symbol>(arg_array._data[idx]);
				type_ref& arg_type = context.symbol_type(arg_symbol);
				fn_args.push_back(named_type(arg_symbol._name, &arg_type));
				context._module->add_local_variable_type(arg_symbol._name, arg_type);
			}
			vector<ast_node_ptr> body_nodes;
			for (cons_cell* body_item = &body; body_item; body_item = object_traits::cast<cons_cell>(body_item->_next) )
			{
				body_nodes.push_back(&context._type_checker(body_item->_value));
			}
			function_factory& factory = context._module->define_function(context._name_table->register_name(fn_name._name)
																			, named_type_buffer( fn_args )
																			, context.symbol_type( fn_name ) );
			factory.set_function_body(body_nodes);
			return nullptr;
		}
	};
}

void base_language_plugins::register_base_compiler_plugins(string_table_ptr str_table
	, string_plugin_map_ptr top_level_special_forms
	, string_plugin_map_ptr /*special_forms*/
	, string_lisp_evaluator_map& /*lisp_evaluators*/)
{
	top_level_special_forms->insert(make_pair( str_table->register_str("defn"), make_shared<defn_compiler_plugin>() ));
}

namespace
{
	typedef function<llvm::Value* (IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)> binary_fn_implementation;

	pair<llvm_value_ptr_opt, type_ref_ptr> implement_binary_function(compiler_context& ctx, const binary_fn_implementation& impl, type_ref& rettype)
	{
		variable_lookup_chain chain;
		chain.name = ctx._name_table->register_name("lhs");
		llvm_value_ptr lhs = ctx._module->load_variable(ctx, chain).first;
		chain.name = ctx._name_table->register_name("rhs");
		llvm_value_ptr rhs = ctx._module->load_variable(ctx, chain).first;
		llvm_value_ptr retval = impl(ctx._builder, lhs, rhs);
		return make_pair(retval, &rettype);
	}

	void register_binary_function(module_ptr module
									, const char* name
									, qualified_name_table_ptr name_table
									, type_ref& retval_type
									, type_ref& lhs_type
									, type_ref& rhs_type
									, const binary_fn_implementation& impl)
	{
		auto str_table = name_table->string_table();
		auto lhs_name = str_table->register_str("lhs");
		auto rhs_name = str_table->register_str("rhs");
		named_type arg_names[] = { named_type(lhs_name, &lhs_type), named_type(rhs_name, &rhs_type) };
		named_type_buffer arg_buffer(arg_names, 2);
		function_factory& new_fn = module->define_function(name_table->register_name(name), arg_buffer, retval_type);
		type_ref_ptr retval_type_ptr(&retval_type);
		compile_pass_fn fn_body = [=](compiler_context&ctx)
		{
			return implement_binary_function(ctx, impl, *retval_type_ptr);
		};
		new_fn.set_function_override_body(fn_body);
	}

	void register_numeric_binary_fn(module_ptr module
		, type_library_ptr type_lib
		, qualified_name_table_ptr name_table
		, const char* name
		, const binary_fn_implementation& impl
		, base_numeric_types::_enum* type_list
		, size_t num_types
		, bool is_bool_retval )
	{
		type_ref& bool_rettype = type_lib->get_type_ref(base_numeric_types::i1);
		for (size_t idx = 0, end = num_types; idx < end; ++idx)
		{
			type_ref& item_type = type_lib->get_type_ref(type_list[idx]);
			type_ref* retval_type = &item_type;
			if (is_bool_retval)
				retval_type = &bool_rettype;
			register_binary_function(module, name, name_table, *retval_type, item_type, item_type, impl);
		}
	}

	void register_binary_float_fn(module_ptr module
		, type_library_ptr type_lib
		, qualified_name_table_ptr name_table
		, const char* name
		, const binary_fn_implementation& impl
		, bool is_bool_rettype )
	{
		base_numeric_types::_enum type_list[] =
		{
			base_numeric_types::f32,
			base_numeric_types::f64,
		};
		register_numeric_binary_fn(module, type_lib, name_table, name, impl, type_list, 2, is_bool_rettype);
	}


	void register_binary_signed_integer_fn(module_ptr module
		, type_library_ptr type_lib
		, qualified_name_table_ptr name_table
		, const char* name
		, const binary_fn_implementation& impl
		, bool is_bool_rettype)
	{
		base_numeric_types::_enum type_list[] =
		{
			base_numeric_types::i8,
			base_numeric_types::i16,
			base_numeric_types::i32,
			base_numeric_types::i64,
		};

		register_numeric_binary_fn(module, type_lib, name_table, name, impl, type_list, 4, is_bool_rettype);
	}

	void register_binary_unsigned_integer_fn(module_ptr module
		, type_library_ptr type_lib
		, qualified_name_table_ptr name_table
		, const char* name
		, const binary_fn_implementation& impl
		, bool is_bool_rettype)
	{
		base_numeric_types::_enum type_list[] =
		{
			base_numeric_types::u8,
			base_numeric_types::u16,
			base_numeric_types::u32,
			base_numeric_types::u64,
		};

		register_numeric_binary_fn(module, type_lib, name_table, name, impl, type_list, 4, is_bool_rettype);
	}

	void register_binary_integer_fn(module_ptr module
		, type_library_ptr type_lib
		, qualified_name_table_ptr name_table
		, const char* name
		, const binary_fn_implementation& impl
		, bool is_bool_rettype)
	{
		register_binary_signed_integer_fn(module, type_lib, name_table, name, impl, is_bool_rettype);
		register_binary_unsigned_integer_fn(module, type_lib, name_table, name, impl, is_bool_rettype);
	}
}

void binary_low_level_ast_node::register_binary_functions(shared_ptr<module> module, type_library_ptr type_lib
	, qualified_name_table_ptr name_table)
{
	register_binary_float_fn(module, type_lib, name_table, "+"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateFAdd(lhs, rhs, "tmpadd");
	}, false);


	register_binary_float_fn(module, type_lib, name_table, "-"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateFSub(lhs, rhs, "tmpadd");
	}, false);

	
	register_binary_float_fn(module, type_lib, name_table, "*"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateFMul(lhs, rhs, "tmpadd");
	}, false);

	register_binary_float_fn(module, type_lib, name_table, "/"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateFDiv(lhs, rhs, "tmpadd");
	}, false);

	register_binary_integer_fn(module, type_lib, name_table, "+"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateAdd(lhs, rhs, "tmpadd");
	}, false);

	register_binary_integer_fn(module, type_lib, name_table, "-"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateSub(lhs, rhs, "tmpadd");
	}, false);

	register_binary_integer_fn(module, type_lib, name_table, "*"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateMul(lhs, rhs, "tmpadd");
	}, false);

	register_binary_signed_integer_fn(module, type_lib, name_table, "/"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateSDiv(lhs, rhs, "tmpadd");
	}, false);

	register_binary_unsigned_integer_fn(module, type_lib, name_table, "/"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateUDiv(lhs, rhs, "tmpadd");
	}, false);

	//boolean operations


	register_binary_float_fn(module, type_lib, name_table, "<"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateFCmpULT(lhs, rhs, "tmpcmp");
	}, true);

	register_binary_float_fn(module, type_lib, name_table, ">"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateFCmpUGT(lhs, rhs, "tmpcmp");
	}, true);

	register_binary_float_fn(module, type_lib, name_table, "=="
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateFCmpUEQ(lhs, rhs, "tmpcmp");
	}, true);

	register_binary_float_fn(module, type_lib, name_table, "!="
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateFCmpUNE(lhs, rhs, "tmpcmp");
	}, true);

	register_binary_float_fn(module, type_lib, name_table, ">="
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateFCmpUGE(lhs, rhs, "tmpcmp");
	}, true);

	register_binary_float_fn(module, type_lib, name_table, "<="
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateFCmpULE(lhs, rhs, "tmpcmp");
	}, true);

	register_binary_integer_fn(module, type_lib, name_table, "=="
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateICmpEQ(lhs, rhs, "tmpadd");
	}, true);

	register_binary_integer_fn(module, type_lib, name_table, "!="
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateICmpNE(lhs, rhs, "tmpadd");
	}, true);

	register_binary_unsigned_integer_fn(module, type_lib, name_table, "<"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateICmpULT(lhs, rhs, "tmpadd");
	}, true);

	register_binary_unsigned_integer_fn(module, type_lib, name_table, "<="
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateICmpULE(lhs, rhs, "tmpadd");
	}, true);

	register_binary_unsigned_integer_fn(module, type_lib, name_table, ">"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateICmpUGT(lhs, rhs, "tmpadd");
	}, true);

	
	register_binary_unsigned_integer_fn(module, type_lib, name_table, ">="
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateICmpUGE(lhs, rhs, "tmpadd");
	}, true);

	register_binary_signed_integer_fn(module, type_lib, name_table, "<"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateICmpSLT(lhs, rhs, "tmpadd");
	}, true);

	register_binary_signed_integer_fn(module, type_lib, name_table, "<="
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateICmpSLE(lhs, rhs, "tmpadd");
	}, true);

	register_binary_signed_integer_fn(module, type_lib, name_table, ">"
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateICmpSGT(lhs, rhs, "tmpadd");
	}, true);

	register_binary_signed_integer_fn(module, type_lib, name_table, ">="
		, [](IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs)
	{
		return builder.CreateICmpSGE(lhs, rhs, "tmpadd");
	}, true);
}