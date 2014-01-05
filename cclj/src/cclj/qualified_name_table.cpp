//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/qualified_name_table.h"

using namespace cclj;

namespace
{
	struct qualified_name_key
	{
		data_buffer<string_table_str> _buffer;
		size_t _hash_code;
		qualified_name_key(data_buffer<string_table_str> buf)
			: _buffer(buf)
			, _hash_code(0)
		{
			for_each(buf.begin(), buf.end(), [this](string_table_str st)
			{
				_hash_code = _hash_code ^ std::hash<string_table_str>()(st);
			});
		}
		qualified_name_key() : _hash_code(0) {}


		size_t hash_code() const { return _hash_code; }
		bool operator==(const qualified_name_key& other) const
		{
			bool retval = other._buffer.size() == _buffer.size();
			for (size_t idx = 0, end = _buffer.size(); idx < end && retval; ++idx)
			{
				retval = _buffer[idx] == other._buffer[idx];
			}
			return retval;
		}
	};
}

namespace std
{
	template<> struct hash<qualified_name_key>
	{
		size_t operator()(const qualified_name_key& key) const
		{
			return key.hash_code();
		}
	};
}

namespace
{
	typedef unordered_map<qualified_name_key, vector<string_table_str> > qualified_name_key_map;
	struct qualified_name_table_impl : public qualified_name_table
	{
		string_table_ptr		_string_table;
		qualified_name_key_map _names;

		qualified_name_table_impl(string_table_ptr st) : _string_table( st ) {}

		virtual string_table_ptr string_table() { return _string_table; }

		virtual qualified_name register_name(string_table_str_buffer name)
		{
			qualified_name_key_map::iterator existing = _names.find(name);
			if (existing == _names.end())
			{
				vector<string_table_str> data;
				data.assign(name.begin(), name.end());
				existing = _names.insert(make_pair(string_table_str_buffer(data), vector<string_table_str>())).first;
				existing->second.swap(data);
			}
			return qualified_name::unsafe_create_qualified_name(existing->second);
		}
	};
}

qualified_name_table_ptr qualified_name_table::create_table()
{
	return make_shared<qualified_name_table_impl>();
}