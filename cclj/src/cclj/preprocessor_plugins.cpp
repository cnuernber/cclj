//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/plugins/preprocessor_plugins.h"
#include "cclj/module.h"

using namespace cclj;
using namespace cclj::lisp;
using namespace cclj::plugins;
using namespace llvm;

#ifdef _WIN32
#pragma warning(disable:4996)
#endif

namespace
{
	struct macro_preprocessor : public compiler_plugin
	{
		const symbol&			_name;
		data_buffer<object_ptr> _arguments;
		cons_cell&				_body;

		static const char* static_type() { return "macro_preprocessor"; }

		macro_preprocessor(const symbol& name, data_buffer<object_ptr> args, const cons_cell& body)
			: compiler_plugin()
			, _name(name)
			, _arguments(args)
			, _body(const_cast<cons_cell&>(body))
		{
		}

		static void quote_lisp_object(reader_context& context, object_ptr& item)
		{
			switch (item->type())
			{
			case types::cons_cell:
			{
									 cons_cell& arg_cell = object_traits::cast_ref<cons_cell>(item);
									 symbol& fn_name = object_traits::cast_ref<symbol>(arg_cell._value);
									 if (fn_name._name == context._string_table->register_str("unquote"))
									 {
										 cons_cell& uq_arg = object_traits::cast_ref<cons_cell>(arg_cell._next);
										 symbol& uq_arg_sym = object_traits::cast_ref<symbol>(uq_arg._value);
										 auto sym_iter = context._preprocessor_symbols.find(uq_arg_sym._name);
										 if (sym_iter == context._preprocessor_symbols.end())
											 throw runtime_error("failed to figure out preprocessor symbol");
										 item = sym_iter->second;
									 }
									 else
									 {
										 quote_list(context, arg_cell);
									 }
			}
				break;
			case types::array:
			{
								 array& arg_array = object_traits::cast_ref<array>(item);
								 quote_array(context, arg_array);
			}
				break;
			case types::symbol:
			{
								  symbol& sym = object_traits::cast_ref<symbol>(item);
								  if (sym._unevaled_type)
									  quote_list(context, *sym._unevaled_type);
			}
				break;
			case types::constant:
			{
									constant& cons = object_traits::cast_ref<constant>(item);
									if (cons._unevaled_type)
										quote_list(context, *cons._unevaled_type);
			}
				break;
			default: break; //leave item as is
			}
		}

		static void quote_list(reader_context& context, cons_cell& item)
		{
			for (cons_cell* next_arg = object_traits::cast<cons_cell>(&item);
				next_arg; next_arg = object_traits::cast<cons_cell>(next_arg->_next))
			{
				quote_lisp_object(context, next_arg->_value);
			}
		}

		static void quote_array(reader_context& context, array& item)
		{
			for (size_t idx = 0, end = item._data.size(); idx < end; ++idx)
			{
				quote_lisp_object(context, item._data[idx]);
			}
		}

		static object_ptr quote(reader_context& context, cons_cell& first_arg)
		{
			cons_cell& arg_val = object_traits::cast_ref<cons_cell>(first_arg._value);
			object_ptr retval = &arg_val;
			quote_list(context, arg_val);
			return retval;
		}

		static object_ptr lisp_apply(reader_context& context, cons_cell& cell)
		{
			symbol& app_name = object_traits::cast_ref<symbol>(cell._value);
			if (app_name._name == context._string_table->register_str("quote"))
				return quote(context, object_traits::cast_ref<cons_cell>(cell._next));
			else
			{
				auto iter = context._preprocessor_evaluators.find(app_name._name);
				if (iter != context._preprocessor_evaluators.end())
				{
					return iter->second->eval(context, cell);
				}
				throw runtime_error("unable to eval lisp preprocessor symbol");
			}
		}

		static double eval_constant(reader_context& /*context*/, constant& src)
		{
			return std::stod(src._unparsed_number.c_str());
		}

		static object_ptr double_to_constant(reader_context& context, double& val)
		{
			constant* retval = context._factory->create_constant();
			cons_cell* typeval = context._factory->create_cell();
			symbol* type_name = context._factory->create_symbol();
			char data_buf[1024];
			sprintf(data_buf, "%f", val);
			retval->_unparsed_number = context._string_table->register_str(data_buf);
			retval->_unevaled_type = typeval;
			typeval->_value = type_name;
			type_name->_name = context._string_table->register_str("f64");
			return retval;
		}

		static object_ptr eval_symbol(reader_context& context, symbol& src)
		{
			auto iter = context._preprocessor_symbols.find(src._name);
			if (iter == context._preprocessor_symbols.end())
				throw runtime_error("Error evaulating symbol");
			return iter->second;
		}

		static object_ptr lisp_eval(reader_context& context, object_ptr src)
		{
			if (src == nullptr)
				throw runtime_error("invalid lisp evaluation");
			switch (src->type())
			{
			case types::array: throw runtime_error("unable to eval an array");
			case types::symbol: return eval_symbol(context, object_traits::cast_ref<symbol>(src));
			case types::constant: return src;
			case types::cons_cell: return lisp_apply(context, object_traits::cast_ref<cons_cell>(src));
			default: break;
			}
			throw runtime_error("unable to evaluate lisp symbol");
		}

		static vector<object_ptr> eval_fn_arguments(reader_context& context, cons_cell& callsite)
		{
			vector<object_ptr> arg_list;
			for (cons_cell* arg_cell = object_traits::cast<cons_cell>(callsite._next); arg_cell
				; arg_cell = object_traits::cast<cons_cell>(arg_cell->_next))
				arg_list.push_back(lisp_eval(context, arg_cell->_value));
			return arg_list;
		}

		virtual ast_node* type_check(reader_context& context, cons_cell& callsite)
		{
			string_obj_ptr_map old_symbols(context._preprocessor_symbols);
			context._preprocessor_symbols.clear();
			preprocess_symbol_context preprocess(context._preprocessor_symbols);
			cons_cell* previous_arg(&callsite);
			for_each(_arguments.begin(), _arguments.end(), [&]
				(object_ptr arg)
			{
				cons_cell* next_arg = &object_traits::cast_ref<cons_cell>(previous_arg->_next);
				symbol& arg_name = object_traits::cast_ref<symbol>(arg);
				preprocess.add_symbol(arg_name._name, *next_arg->_value);
				previous_arg = next_arg;
			});
			object_ptr retval = nullptr;
			for (cons_cell* body_cell = &_body; body_cell
				; body_cell = object_traits::cast<cons_cell>(body_cell->_next))
			{
				retval = lisp_apply(context, object_traits::cast_ref<cons_cell>(body_cell->_value));
			}
			context._preprocessor_symbols = old_symbols;
			return &context._type_checker(retval);
		}
	};

	class macro_def_plugin : public compiler_plugin
	{
	public:
		macro_def_plugin()
			: compiler_plugin()
		{
		}

		ast_node* type_check(reader_context& context, lisp::cons_cell& cell)
		{
			cons_cell& second_cell = object_traits::cast_ref<cons_cell>(cell._next);
			symbol& macro_name = object_traits::cast_ref<symbol>(second_cell._value);
			cons_cell& third_cell = object_traits::cast_ref<cons_cell>(second_cell._next);
			cons_cell& fourth_cell = object_traits::cast_ref<cons_cell>(third_cell._next);
			data_buffer<object_ptr> arg_array = object_traits::cast_ref<array>(third_cell._value)._data;
			compiler_plugin_ptr preprocess
				= make_shared<macro_preprocessor>(macro_name, arg_array, fourth_cell);
			(*context._special_forms)[macro_name._name] = preprocess;
			return nullptr;
		}
	};
}


void preprocessor_plugins::register_plugins(qualified_name_table_ptr name_table
	, string_plugin_map_ptr top_level_special_forms
	, string_plugin_map_ptr /*special_forms*/
	, string_lisp_evaluator_map& /*lisp_evaluators*/)
{
	string_table_ptr string_table = name_table->string_table();
	top_level_special_forms->insert(make_pair(string_table->register_str("defmacro")
		, make_shared<macro_def_plugin>()));
}