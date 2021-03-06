//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_QUALIFIED_NAME_TABLE_H
#define CCLJ_QUALIFIED_NAME_TABLE_H
#include "cclj/cclj.h"
#include "cclj/string_table.h"
#include "cclj/data_buffer.h"

namespace cclj
{

	typedef data_buffer<string_table_str> string_table_str_buffer;
	class qualified_name
	{
		string_table_str_buffer	_names;
	public:
		qualified_name() {}
		qualified_name(const qualified_name& other)
			: _names(other._names)
		{
		}
		size_t hash_code() const { return reinterpret_cast<size_t>(_names.begin()); }
		bool operator==(const qualified_name& other) const
		{
			return _names.begin() == other._names.begin();
		}
		string_table_str_buffer names() const { return _names; }

		string_table_str* begin() const { return _names.begin(); }
		string_table_str* end() const { return _names.end(); }


		//should only be used internally to the qualified name table
		static qualified_name unsafe_create_qualified_name(string_table_str_buffer nm)
		{
			qualified_name retval;
			retval._names = nm;
			return retval;
		}
	};

	class qualified_name_table
	{
	protected:
		virtual ~qualified_name_table(){}
	public:
		friend class shared_ptr<qualified_name_table>;

		virtual string_table_ptr string_table() = 0;
		virtual qualified_name register_name(string_table_str_buffer name) = 0;
		qualified_name register_name(const char* nm)
		{
			auto str = string_table()->register_str(nm);
			string_table_str_buffer buf(&str, 1);
			return register_name(buf);
		}
		qualified_name register_name(const string& nm)
		{
			return register_name(nm.c_str());
		}

		qualified_name register_name(data_buffer<string> name)
		{
			if (name.size() == 1)
				return register_name(name[0].c_str());

			vector<string_table_str> data_buf(name.size());
			auto str_table = string_table();
			for (size_t idx = 0, end = name.size(); idx < end; ++idx)
				data_buf[idx] = str_table->register_str(name[idx].c_str());
			return register_name(data_buf);
		}
		static shared_ptr<qualified_name_table> create_table(string_table_ptr st);
	};

	typedef shared_ptr<qualified_name_table> qualified_name_table_ptr;
}

namespace std
{
	template<> struct hash<cclj::qualified_name>
	{
		size_t operator()(const cclj::qualified_name& nm) const
		{
			return nm.hash_code();
		}
	};
}
#endif