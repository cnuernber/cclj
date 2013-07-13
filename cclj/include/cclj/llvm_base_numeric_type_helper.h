//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef LLVM_BASE_NUMERIC_TYPE_HELPER_H
#define LLVM_BASE_NUMERIC_TYPE_HELPER_H
#include "cclj/lisp.h"
#ifdef _WIN32
#pragma warning(push,2)
#endif
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#ifdef _WIN32
#pragma warning(pop)
#endif


namespace cclj { namespace llvm_helper {
	using namespace llvm;
	using namespace cclj::lisp;
	template<base_numeric_types::_enum number_type>
	struct llvm_constant_map
	{
	};

	template<> struct llvm_constant_map<base_numeric_types::i1>
	{
		static Value* parse( const uint8_t* data )
		{
			bool datum = *reinterpret_cast<const bool*>( data );
			return datum ? ConstantInt::getTrue(getGlobalContext()) : ConstantInt::getFalse(getGlobalContext());
		}
	};
	
	template<> struct llvm_constant_map<base_numeric_types::i8>
	{
		static Value* parse( const uint8_t* data )
		{
			uint8_t datum = *reinterpret_cast<const uint8_t*>( data );
			return ConstantInt::get( Type::getInt8Ty(getGlobalContext()), datum, true );
		}
	};
	template<> struct llvm_constant_map<base_numeric_types::u8>
	{
		static Value* parse( const uint8_t* data )
		{
			uint8_t datum = *reinterpret_cast<const uint8_t*>( data );
			return ConstantInt::get( Type::getInt8Ty(getGlobalContext()), datum, false );
		}
	};
	
	template<> struct llvm_constant_map<base_numeric_types::i16>
	{
		static Value* parse( const uint8_t* data )
		{
			uint16_t datum = *reinterpret_cast<const uint16_t*>( data );
			return ConstantInt::get( Type::getInt16Ty(getGlobalContext()), datum, true );
		}
	};
	template<> struct llvm_constant_map<base_numeric_types::u16>
	{
		static Value* parse( const uint8_t* data )
		{
			uint16_t datum = *reinterpret_cast<const uint16_t*>( data );
			return ConstantInt::get( Type::getInt16Ty(getGlobalContext()), datum, false );
		}
	};
	template<> struct llvm_constant_map<base_numeric_types::i32>
	{
		static Value* parse( const uint8_t* data )
		{
			uint32_t datum = *reinterpret_cast<const uint32_t*>( data );
			return ConstantInt::get( Type::getInt32Ty(getGlobalContext()), datum, true );
		}
	};
	template<> struct llvm_constant_map<base_numeric_types::u32>
	{
		static Value* parse( const uint8_t* data )
		{
			uint32_t datum = *reinterpret_cast<const uint32_t*>( data );
			return ConstantInt::get( Type::getInt32Ty(getGlobalContext()), datum, false );
		}
	};
	template<> struct llvm_constant_map<base_numeric_types::i64>
	{
		static Value* parse( const uint8_t* data )
		{
			uint64_t datum = *reinterpret_cast<const uint64_t*>( data );
			return ConstantInt::get( Type::getInt64Ty(getGlobalContext()), datum, true );
		}
	};
	template<> struct llvm_constant_map<base_numeric_types::u64>
	{
		static Value* parse( const uint8_t* data )
		{
			uint64_t datum = *reinterpret_cast<const uint64_t*>( data );
			return ConstantInt::get( Type::getInt64Ty(getGlobalContext()), datum, false );
		}
	};
	template<> struct llvm_constant_map<base_numeric_types::f32>
	{
		static Value* parse( const uint8_t* data )
		{
			float datum = *reinterpret_cast<const float*>( data );
			return ConstantFP::get( Type::getFloatTy(getGlobalContext()), datum );
		}
	};
	template<> struct llvm_constant_map<base_numeric_types::f64>
	{
		static Value* parse( const uint8_t* data )
		{
			double datum = *reinterpret_cast<const double*>( data );
			return ConstantFP::get( Type::getDoubleTy(getGlobalContext()), datum );
		}
	};

}}

#endif