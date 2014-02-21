//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_PLUGINS_PLUGIN_IMPL_HELPER_H
#define CCLJ_PLUGINS_PLUGIN_IMPL_HELPER_H
#pragma once

#define CCLJ_BASE_PLUGINS_DESTRUCT_AST_NODE(data_type) \
} namespace cclj { CCLJ_SLAB_ALLOCATOR_REQUIRES_DESTRUCTION(data_type) } namespace {

#endif