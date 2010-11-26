/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Native Lua wrappers around GIRepository.
 */

#include <string.h>
#include "lgi.h"

typedef GIBaseInfo *(* InfosItemGet)(GIBaseInfo* info, gint item);

static int
info_gc (lua_State *L)
{
  GIBaseInfo **info = luaL_checkudata (L, 1, GI_INFO);
  g_base_info_unref (*info);
  return 0;
}

/* Creates new instance of info from given GIBaseInfo pointer. */
static int
info_new (lua_State *L, GIBaseInfo *info)
{
  if (info)
    {
      GIBaseInfo **ud_info = lua_newuserdata (L, sizeof (info));
      *ud_info = info;
      luaL_getmetatable (L, GI_INFO);
      lua_setmetatable (L, -2);
    }
  else
    lua_pushnil (L);

  return 1;
}

/* Userdata representing single group of infos (e.g. methods on
   object, fields of struct etc.).  Emulates Lua table for access. */
typedef struct _Infos
{
  GIBaseInfo *info;
  gint count;
  InfosItemGet item_get;
} Infos;
#define GI_INFOS "lgi.gi.infos"

static int
infos_len (lua_State *L)
{
  Infos* infos = luaL_checkudata (L, 1, GI_INFOS);
  lua_pushnumber (L, infos->count + 1);
  return 1;
}

static int
infos_index (lua_State *L)
{
  Infos* infos = luaL_checkudata (L, 1, GI_INFOS);
  gint n = luaL_checkinteger (L, 2) - 1;
  luaL_argcheck (L, n >= 0 && n < infos->count, 2, "out of bounds");
  return info_new (L, infos->item_get (infos->info, n));
}

/* Creates new userdata object representing given category of infos. */
static int
infos_new (lua_State *L, GIBaseInfo *info, gint count, InfosItemGet item_get)
{
  Infos *infos = lua_newuserdata (L, sizeof (Infos));
  luaL_getmetatable (L, GI_INFOS);
  lua_setmetatable (L, -2);
  infos->info = g_base_info_ref (info);
  infos->count = count;
  infos->item_get = item_get;
  return 1;
}

static const luaL_Reg gi_infos_reg[] = {
  { "__gc", info_gc },
  { "__len", infos_len },
  { "__index", infos_index },
  { NULL, NULL }
};

static int
info_index (lua_State *L)
{
  GIBaseInfo **info = luaL_checkudata (L, 1, GI_INFO);
  const gchar *prop = luaL_checkstring (L, 2);

#define INFOS(n1, n2)							\
  else if (strcmp (prop, #n2 "s") == 0)					\
    return infos_new (L, *info,						\
		      g_ ## n1 ## _info_get_n_ ## n2 ## s (*info),	\
		      g_ ## n1 ## _info_get_ ## n2);

#define INFOS2(n1, n2, n3)					\
  else if (strcmp (prop, #n3) == 0)				\
    return infos_new (L, *info,					\
		      g_ ## n1 ## _info_get_n_ ## n3 (*info),	\
		      g_ ## n1 ## _info_get_ ## n2);

  if (strcmp (prop, "type") == 0)
    {
      switch (g_base_info_get_type (*info))
	{
#define H(n1, n2)				\
	  case GI_INFO_TYPE_ ## n1:		\
	    lua_pushstring (L, #n2);		\
	    return 1;

	  H(FUNCTION, function)
	    H(CALLBACK, callback)
	    H(STRUCT, struct)
	    H(BOXED, boxed)
	    H(ENUM, enum)
	    H(FLAGS, flags)
	    H(OBJECT, object)
	    H(INTERFACE, interface)
	    H(CONSTANT, constant)
	    H(ERROR_DOMAIN, error_domain)
	    H(UNION, union)
	    H(VALUE, value)
	    H(SIGNAL, signal)
	    H(VFUNC, vfunc)
	    H(PROPERTY, property)
	    H(FIELD, field)
	    H(ARG, arg)
	    H(TYPE, type)
	    H(UNRESOLVED, unresolved)
#undef H
	default:
	  g_assert_not_reached ();
	}
    }

#define H(n1, n2)						\
  else if (strcmp (prop, "is_" #n2) == 0)			\
    {								\
      lua_pushboolean (L, GI_IS_ ## n1 ## _INFO (*info));	\
      return 1;							\
    }
  H(ARG, arg)
    H(CALLABLE, callable)
    H(FUNCTION, function)
    H(SIGNAL, signal)
    H(VFUNC, vfunc)
    H(CONSTANT, constant)
    H(ERROR_DOMAIN, error_domain)
    H(FIELD, field)
    H(PROPERTY, property)
    H(REGISTERED_TYPE, registered_type)
    H(ENUM, enum)
    H(INTERFACE, interface)
    H(OBJECT, object)
    H(STRUCT, struct)
    H(UNION, union)
    H(TYPE, type)
    H(VALUE, value)
#undef H

  else if (strcmp (prop, "name") == 0)
    {
      lua_pushstring (L, g_base_info_get_name (*info));
      return 1;
    }
  else if (strcmp (prop, "namespace") == 0)
    {
      lua_pushstring (L, g_base_info_get_namespace (*info));
      return 1;
    }
  else if (strcmp (prop, "deprecated") == 0)
    {
      lua_pushboolean (L, g_base_info_is_deprecated (*info));
      return 1;
    }
  else if (strcmp (prop, "container") == 0)
    {
      GIBaseInfo *container = g_base_info_get_container (*info);
      return info_new (L, g_base_info_ref (container));
    }
  else if (strcmp (prop, "typeinfo") == 0)
    {
      GITypeInfo *ti = NULL;
      if (GI_IS_ARG_INFO (*info))
	ti = g_arg_info_get_type (*info);
      else if (GI_IS_CONSTANT_INFO (*info))
	ti = g_constant_info_get_type (*info);
      else if (GI_IS_PROPERTY_INFO (*info))
	ti = g_property_info_get_type (*info);
      else if (GI_IS_FIELD_INFO (*info))
	ti = g_field_info_get_type (*info);

      if (ti)
	return info_new (L, ti);
    }
  else if (GI_IS_REGISTERED_TYPE_INFO (*info))
    {
      if (strcmp (prop, "gtype") == 0)
	{
	  lua_pushnumber (L, g_registered_type_info_get_g_type (*info));
	  return 1;
	}
    }
  else if (GI_IS_VALUE_INFO (*info))
    {
      if (strcmp (prop, "value") == 0)
	{
	  lua_pushnumber (L, g_value_info_get_value (*info));
	  return 1;
	}
    }
  else if (GI_IS_STRUCT_INFO (*info))
    {
      if (strcmp (prop, "is_gtype_struct") == 0)
	{
	  lua_pushboolean (L, g_struct_info_is_gtype_struct (*info));
	  return 1;
	}
      INFOS (struct, field)
	INFOS (struct, method);
    }
  else if (GI_IS_UNION_INFO (*info))
    {
      if (0);
      INFOS (union, field)
	INFOS (union, method);
    }
  else if (GI_IS_INTERFACE_INFO (*info))
    {
      if (0);
      INFOS (interface, prerequisite)
	INFOS (interface, method)
	INFOS (interface, constant)
	INFOS2 (interface, property, properties)
	INFOS (interface, signal);
    }
  else if (GI_IS_OBJECT_INFO (*info))
    {
      if (strcmp (prop, "parent") == 0)
	return info_new (L, g_object_info_get_parent (*info));
      INFOS (object, interface)
	INFOS (object, field)
	INFOS (object, method)
	INFOS (object, constant)
	INFOS2 (object, property, properties)
	INFOS (object, signal);
    }
  else if (GI_IS_TYPE_INFO (*info))
    {
      GITypeTag tag = g_type_info_get_tag (*info);
      if (strcmp (prop, "tag") == 0)
	{
	  lua_pushstring (L, g_type_tag_to_string (tag));
	  return 1;
	}
      else if (strcmp (prop, "param") == 0)
	{
	  if (tag == GI_TYPE_TAG_ARRAY || tag == GI_TYPE_TAG_GLIST ||
	      tag == GI_TYPE_TAG_GSLIST || tag == GI_TYPE_TAG_GHASH)
	    {
	      lua_newtable (L);
	      info_new (L, g_type_info_get_param_type (*info, 0));
	      lua_rawseti (L, -2, 1);
	      if (tag == GI_TYPE_TAG_GHASH)
		{
		  info_new (L, g_type_info_get_param_type (*info, 1));
		  lua_rawseti (L, -2, 2);
		}
	      return 1;
	    }
	}
      else if (strcmp (prop, "interface") == 0 && tag == GI_TYPE_TAG_INTERFACE)
	{
	  info_new (L, g_type_info_get_interface (*info));
	  return 1;
	}
      else if (strcmp (prop, "array_type") == 0 && tag == GI_TYPE_TAG_ARRAY)
	{
	  switch (g_type_info_get_array_type (*info))
	    {
#define H(n1, n2)			 \
	      case GI_ARRAY_TYPE_ ## n1: \
		lua_pushstring (L, #n2); \
		return 1;

	      H(C, c)
		H(ARRAY, array)
		H(PTR_ARRAY, ptr_array)
		H(BYTE_ARRAY, byte_array)
#undef H
	    default:
	      g_assert_not_reached ();
	    }
	}
    }

  lua_pushfstring (L, "unsupported info property `%s'", prop);
  return luaL_argerror (L, 2, lua_tostring (L, -1));

#undef INFOS
#undef INFOS2
}

static const luaL_Reg gi_info_reg[] = {
  { "__gc", info_gc },
  { "__index", info_index },
  { NULL, NULL }
};

/* Userdata representing namespace in girepository. */
#define GI_NAMESPACE "lgi.gi.namespace"

static int
namespace_len (lua_State *L)
{
  const gchar *ns = luaL_checkudata (L, 1, GI_NAMESPACE);
  lua_pushinteger (L, g_irepository_get_n_infos (NULL, ns) + 1);
  return 1;
}

static int
namespace_index (lua_State *L)
{
  const gchar *ns = luaL_checkudata (L, 1, GI_NAMESPACE);
  const gchar *prop;
  if (lua_isnumber (L, 2))
    return info_new (L, g_irepository_get_info (NULL, ns,
						lua_tointeger (L, 2) - 1));
  prop = luaL_checkstring (L, 2);
  if (strcmp (prop, "dependencies") == 0)
    {
      gchar **deps = g_irepository_get_dependencies (NULL, ns);
      if (deps == NULL)
	lua_pushnil (L);
      else
	{
	  int index;
	  gchar **dep;
	  lua_newtable (L);
	  for (index = 1, dep = deps; *dep; dep++, index++)
	    {
	      lua_pushstring (L, *dep);
	      lua_rawseti (L, -2, index);
	    }
	  g_strfreev (deps);
	}

      return 1;
    }
  else if (strcmp (prop, "version") == 0)
    {
      lua_pushstring (L, g_irepository_get_version (NULL, ns));
      return 1;
    }
  else
    /* Try to lookup the symbol. */
    return info_new (L, g_irepository_find_by_name (NULL, ns, prop));
}

static int
namespace_new (lua_State *L, const gchar *namespace)
{
  gchar *ns = lua_newuserdata (L, strlen (namespace) + 1);
  luaL_getmetatable (L, GI_NAMESPACE);
  lua_setmetatable (L, -2);
  strcpy (ns, namespace);
  return 1;
}

static const luaL_Reg gi_namespace_reg[] = {
  { "__index", namespace_index },
  { "__len", namespace_len },
  { NULL, NULL }
};

/* Lua API: core.gi.require(namespace[, version[, typelib_dir]]) */
static int
gi_require (lua_State *L)
{
  GError *err = NULL;
  const gchar *namespace = luaL_checkstring (L, 1);
  const gchar *version = luaL_optstring (L, 2, NULL);
  const gchar *typelib_dir = luaL_optstring (L, 3, NULL);
  GITypelib *typelib;

  if (typelib_dir == NULL)
    typelib = g_irepository_require (NULL, namespace, version,
				     G_IREPOSITORY_LOAD_FLAG_LAZY, &err);
  else
    typelib = g_irepository_require_private (NULL, typelib_dir, namespace,
					     version,
					     G_IREPOSITORY_LOAD_FLAG_LAZY,
					     &err);
  if (!typelib)
    {
      lua_pushboolean (L, 0);
      lua_pushstring (L, err->message);
      lua_pushnumber (L, err->code);
      g_error_free (err);
      return 3;
    }

  return namespace_new (L, namespace);
}

static int
gi_index (lua_State *L)
{
  if (lua_isnumber (L, 2))
    return info_new (L, g_irepository_find_by_gtype (NULL,
						     luaL_checknumber (L, 2)));
  else
    return namespace_new (L, luaL_checkstring (L, 2));
}

void
lgi_gi_init (lua_State *L)
{
  /* Register metatable for infos object. */
  luaL_newmetatable (L, GI_INFOS);
  luaL_register (L, NULL, gi_infos_reg);
  lua_pop (L, 1);

  /* Register metatable for info object. */
  luaL_newmetatable (L, GI_INFO);
  luaL_register (L, NULL, gi_info_reg);
  lua_pop (L, 1);

  /* Register metatable for namespace object. */
  luaL_newmetatable (L, GI_NAMESPACE);
  luaL_register (L, NULL, gi_namespace_reg);
  lua_pop (L, 1);

  /* Register global API. */
  lua_newtable (L);
  lua_pushcfunction (L, gi_require);
  lua_setfield (L, -2, "require");
  lua_newtable (L);
  lua_pushcfunction (L, gi_index);
  lua_setfield (L, -2, "__index");
  lua_setmetatable (L, -2);
  lua_setfield (L, -2, "gi");
}
