//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/plugins/compiler_plugin.h"
#include "cclj/llvm_base_numeric_type_helper.h"
#include "cclj/variant.h"
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
#include "llvm/Support/raw_ostream.h"
#ifdef _WIN32
#pragma warning(pop)
#endif


using namespace cclj;
using namespace cclj::lisp;
using namespace llvm;

compiler_context::compiler_context(type_library_ptr tl
					, qualified_name_table_ptr name_table
					, module_ptr module
					, llvm::Module& m,  llvm::FunctionPassManager& fpm
					, llvm::ExecutionEngine& eng )
	: _llvm_module( m )
	, _name_table( name_table )
	, _module( module )
	, _fpm( fpm )
	, _eng( eng )
	, _type_library( tl )
	, _builder( getGlobalContext() )
{
}

void compiler_context::enter_scope()
{
	_scopes.push_back( compiler_scope() );
}
void compiler_context::add_exit_block( llvm::BasicBlock& block )
{
	if ( _scopes.empty() ) throw runtime_error( "no scope to add block" );
	_scopes.back().add_block( block );
}

void compiler_context::exit_scope()
{
	if ( _scopes.empty() ) throw runtime_error( "no scope to exit" );
	compiler_scope& scope = _scopes.back();
	//Get current function
	if ( scope._blocks.size() )
	{
		Function* function = _builder.GetInsertBlock()->getParent();
		for_each( scope._blocks.rbegin(), scope._blocks.rend(), [&,this]
		( BasicBlock* block )
		{
			function->getBasicBlockList().push_back( block );
			_builder.CreateBr( block );
			_builder.SetInsertPoint( block );
		} );
	}
	_scopes.pop_back();
}



string compiler_context::qualified_name_to_llvm_name(qualified_name nm)
{
	_name_buffer.clear();
	bool first = true;
	for_each(nm.begin(), nm.end(), [&](string_table_str data){
		if (!first)
			_name_buffer << "_";
		first = false;
		_name_buffer << data.c_str();
	});
	return _name_buffer.str();
}

namespace
{
	void type_ref_to_llvm_name(type_ref& type, stringstream& str)
	{
		str << type._name.c_str();
		if (type._specializations.size())
		{
			str << "[";
			for_each(type._specializations.begin(), type._specializations.end(), [&](type_ref_ptr type)
			{
				type_ref_to_llvm_name(*type, str);
				str << " ";
			});
			str << "]";
		}
	}
}


string compiler_context::qualified_name_to_llvm_name(qualified_name nm, type_ref_ptr_buffer specializations)
{
	qualified_name_to_llvm_name(nm);
	if (specializations.size())
	{
		_name_buffer << "[";
		for_each(specializations.begin(), specializations.end(), [&](type_ref_ptr type)
		{
			type_ref_to_llvm_name(*type, _name_buffer);
			_name_buffer << " ";
		});
		_name_buffer << "]";
	}
	return _name_buffer.str();
}


llvm::GlobalValue::LinkageTypes compiler_context::visibility_to_linkage(visibility::_enum vis)
{
	switch (vis)
	{
	case visibility::internal_visiblity:
		return llvm::GlobalValue::LinkageTypes::PrivateLinkage;
	case visibility::external_visibility:
		return llvm::GlobalValue::LinkageTypes::ExternalLinkage;
	default:
		throw runtime_error("Unrecognized linkage type");
	}
}

namespace
{

	llvm_type_ptr_opt do_get_type_ref( compiler_context& context, type_ref& type )
	{
		if ( context._type_library->is_pointer_type( type ) )
		{

			llvm_type_ptr_opt llvm_ptr = context.type_ref_type( context._type_library->deref_ptr_type( type ) );
			if ( llvm_ptr )
				return PointerType::get( llvm_ptr.get(), 0 );
			return llvm_type_ptr_opt();
		}
		else if (context._type_library->is_tuple_type(type))
		{
			vector<llvm_type_ptr> arg_types;
			for (auto iter = type._specializations.begin(), end = type._specializations.end()
				;  iter != end; ++iter)
			{
				if (!context._type_library->is_void_type(**iter))
					arg_types.push_back(context.type_ref_type(**iter).get());
			}
			//Create struct type definition to llvm.
			return StructType::create(getGlobalContext(), arg_types);
		}
		else
		{
			if ( type._name == context._type_library->string_table()->register_str( "unqual" ) )
			{
				Type* intType = IntegerType::getInt32Ty( getGlobalContext() );
				return PointerType::getUnqual( intType );
			}
			if ( &type == &context._type_library->get_void_type() )
				return Type::getVoidTy( getGlobalContext() );
			if ( &type == &context._type_library->get_void_type() )
				return llvm_type_ptr_opt();
			//else we inserted something, so we need to ensure it is valid.
			base_numeric_types::_enum val = context._type_library->to_base_numeric_type( type );
			llvm_type_ptr base_type = nullptr;
			switch( val )
			{
		#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name )					\
			case base_numeric_types::name: base_type					\
				= llvm_helper::llvm_constant_map<base_numeric_types::name>::type(); break;
				CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
		#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
			default:
				throw runtime_error( "unable to find type" );
			}
			if ( base_type == nullptr ) throw runtime_error( "unable to find type" );
			return base_type;
		}
	}

}

llvm_type_ptr_opt compiler_context::type_ref_type( type_ref& type )
{
	pair<type_llvm_type_map::iterator,bool> inserter = _type_map.insert( make_pair( &type, nullptr ) );
	if ( inserter.second == false )
		return inserter.first->second;
	auto ptr = do_get_type_ref( *this, type );
	if ( ptr )
		inserter.first->second = ptr.get();
	return inserter.first->second;
}


reader_context::reader_context( allocator_ptr alloc, lisp::factory_ptr f, type_library_ptr l
					, string_table_ptr st, type_check_function tc
						, type_eval_function te
						, string_plugin_map_ptr special_forms
						, string_plugin_map_ptr top_level_special_forms
						, slab_allocator_ptr ast_alloc
						, string_lisp_evaluator_map& lisp_evals
						, qualified_name_table_ptr name_table
						, shared_ptr<module> module)
	: _factory( f )
	, _type_library( l )
	, _string_table( st )
	, _ast_allocator( ast_alloc )
	, _type_checker( tc )
	, _type_evaluator( te )
	, _special_forms( special_forms )
	, _top_level_special_forms( top_level_special_forms )
	, _preprocessor_evaluators(lisp_evals)
	, _name_table( name_table )
	, _module( module )
{
}


type_ref& reader_context::symbol_type( symbol& symbol )
{
	if ( symbol._evaled_type != nullptr )
		return *symbol._evaled_type;
	if ( symbol._unevaled_type == nullptr )
		throw runtime_error( "symbol has no type" );
	symbol._evaled_type = &_type_evaluator( *symbol._unevaled_type );
	return *symbol._evaled_type;
}


