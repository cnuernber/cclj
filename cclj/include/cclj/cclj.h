//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_H
#define CCLJ_H
#pragma once
#include <memory>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <limits>
#include <algorithm>
#include <cassert>
#include <iterator>
#include <cstdint>
#include <utility>
#include <string>
#include <functional>
#include <vector>
#include <regex>
#include <cstdlib>

namespace cclj
{
	using std::unordered_set;
	using std::vector;
	using std::numeric_limits;
	using std::for_each;
	using std::remove_if;
	using std::unordered_map;
	using std::make_pair;
	using std::copy_if;
	using std::inserter;
	using std::make_shared;
	using std::shared_ptr;
	using std::string;
	using std::hash;
	using std::pair;
	using std::advance;
	using std::copy;
	using std::function;
	using std::vector;
	using std::swap;
	using std::runtime_error;
	using std::is_pod;
	using std::find;
	using std::find_if;
	using std::transform;
	using std::sort;


	class garbage_collector;
	class allocator;
	class reference_tracker;
	class gc_object;
}

#endif