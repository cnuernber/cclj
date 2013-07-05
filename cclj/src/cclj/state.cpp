//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/state.h"
#include "cclj/allocator.h"
#include "cclj/lisp.h"
#include "cclj/ast.h"
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
using namespace cclj::lisp;
using namespace cclj::ast;
using namespace llvm;


namespace
{
	struct state_impl : public state
	{
		allocator_ptr		_alloc;
		string_table_ptr	_str_table;
		cons_cell			_empty_cell;


		state_impl() : _alloc( allocator::create_checking_allocator() )
			, _str_table( string_table::create() )
		{
		}
		virtual float execute( const string& data )
		{
			factory_ptr factory = factory::create_factory( _alloc, _empty_cell );
			reader_ptr reader = reader::create_reader( factory, _str_table );
			object_ptr_buffer parse_result = reader->parse( data );
			context_map global_context;
			//run through, code-gen the function defs and call the functions.
			InitializeNativeTarget();
			LLVMContext &Context = getGlobalContext();
			shared_ptr<Module> TheModule( new Module("my cool jit", Context) );

			// Create the JIT.  This takes ownership of the module.
			std::string ErrStr;
			shared_ptr<ExecutionEngine> llvm_exe_engine( EngineBuilder(TheModule.get()).setErrorStr(&ErrStr).create() );
			if (!llvm_exe_engine) {
				throw runtime_error( "Could not create ExecutionEngine\n" );
			}

			FunctionPassManager OurFPM(TheModule.get());

			// Set up the optimizer pipeline.  Start with registering info about how the
			// target lays out data structures.
			OurFPM.add(new DataLayout(*llvm_exe_engine->getDataLayout()));
			// Provide basic AliasAnalysis support for GVN.
			OurFPM.add(createBasicAliasAnalysisPass());
			// Promote allocas to registers.
			OurFPM.add(createPromoteMemoryToRegisterPass());
			// Do simple "peephole" optimizations and bit-twiddling optzns.
			OurFPM.add(createInstructionCombiningPass());
			// Reassociate expressions.
			OurFPM.add(createReassociatePass());
			// Eliminate Common SubExpressions.
			OurFPM.add(createGVNPass());
			// Simplify the control flow graph (deleting unreachable blocks, etc).
			OurFPM.add(createCFGSimplificationPass());

			OurFPM.doInitialization();

			return 0.0f;
		}
	};
}

state_ptr state::create_state() { return make_shared<state_impl>(); }


