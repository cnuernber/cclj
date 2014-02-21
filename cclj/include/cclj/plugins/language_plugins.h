//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_PLUGINS_LANGUAGE_PLUGINS_H
#define CCLJ_PLUGINS_LANGUAGE_PLUGINS_H
#pragma once
#include "cclj/cclj.h"
#include "cclj/plugins/compiler_plugin.h"

namespace cclj { namespace plugins {

	class language_plugins
	{
	public:
		static void register_plugins(qualified_name_table_ptr name_table
			, string_plugin_map_ptr top_level_special_forms
			, string_plugin_map_ptr special_forms);
	};
}}
#endif