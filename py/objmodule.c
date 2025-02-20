/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Damien P. George
 * Copyright (c) 2014-2015 Paul Sokolovsky
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "py/bc.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "py/builtin.h"

#include "genhdr/moduledefs.h"

#if MICROPY_MODULE_BUILTIN_INIT
STATIC void mp_module_call_init(mp_obj_t module_name, mp_obj_t module_obj);
#endif

STATIC void module_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_module_t *self = MP_OBJ_TO_PTR(self_in);

    const char *module_name = "";
    mp_map_elem_t *elem = mp_map_lookup(&self->globals->map, MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_MAP_LOOKUP);
    if (elem != NULL) {
        module_name = mp_obj_str_get_str(elem->value);
    }

    #if MICROPY_PY___FILE__
    // If we store __file__ to imported modules then try to lookup this
    // symbol to give more information about the module.
    elem = mp_map_lookup(&self->globals->map, MP_OBJ_NEW_QSTR(MP_QSTR___file__), MP_MAP_LOOKUP);
    if (elem != NULL) {
        mp_printf(print, "<module '%s' from '%s'>", module_name, mp_obj_str_get_str(elem->value));
        return;
    }
    #endif

    mp_printf(print, "<module '%s'>", module_name);
}

STATIC void module_attr_try_delegation(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    #if MICROPY_MODULE_ATTR_DELEGATION
    // Delegate lookup to a module's custom attr method (found in last lot of globals dict).
    mp_obj_module_t *self = MP_OBJ_TO_PTR(self_in);
    mp_map_t *map = &self->globals->map;
    if (map->table[map->alloc - 1].key == MP_OBJ_NEW_QSTR(MP_QSTRnull)) {
        ((mp_attr_fun_t)MP_OBJ_TO_PTR(map->table[map->alloc - 1].value))(self_in, attr, dest);
    }
    #else
    (void)self_in;
    (void)attr;
    (void)dest;
    #endif
}

STATIC void module_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_obj_module_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        // load attribute
        mp_map_elem_t *elem = mp_map_lookup(&self->globals->map, MP_OBJ_NEW_QSTR(attr), MP_MAP_LOOKUP);
        if (elem != NULL) {
            dest[0] = elem->value;
        #if MICROPY_MODULE_GETATTR
        } else if (attr != MP_QSTR___getattr__) {
            elem = mp_map_lookup(&self->globals->map, MP_OBJ_NEW_QSTR(MP_QSTR___getattr__), MP_MAP_LOOKUP);
            if (elem != NULL) {
                dest[0] = mp_call_function_1(elem->value, MP_OBJ_NEW_QSTR(attr));
            } else {
                module_attr_try_delegation(self_in, attr, dest);
            }
        #endif
        } else {
            module_attr_try_delegation(self_in, attr, dest);
        }
    } else {
        // delete/store attribute
        mp_obj_dict_t *dict = self->globals;
        if (dict->map.is_fixed) {
            #if MICROPY_CAN_OVERRIDE_BUILTINS
            if (dict == &mp_module_builtins_globals) {
                if (MP_STATE_VM(mp_module_builtins_override_dict) == NULL) {
                    MP_STATE_VM(mp_module_builtins_override_dict) = MP_OBJ_TO_PTR(mp_obj_new_dict(1));
                }
                dict = MP_STATE_VM(mp_module_builtins_override_dict);
            } else
            #endif
            {
                // can't delete or store to fixed map
                module_attr_try_delegation(self_in, attr, dest);
                return;
            }
        }
        if (dest[1] == MP_OBJ_NULL) {
            // delete attribute
            mp_obj_dict_delete(MP_OBJ_FROM_PTR(dict), MP_OBJ_NEW_QSTR(attr));
        } else {
            // store attribute
            mp_obj_dict_store(MP_OBJ_FROM_PTR(dict), MP_OBJ_NEW_QSTR(attr), dest[1]);
        }
        dest[0] = MP_OBJ_NULL; // indicate success
    }
}

const mp_obj_type_t mp_type_module = {
    { &mp_type_type },
    .name = MP_QSTR_module,
    .print = module_print,
    .attr = module_attr,
};

mp_obj_t mp_obj_new_module(qstr module_name) {
    mp_map_t *mp_loaded_modules_map = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_elem_t *el = mp_map_lookup(mp_loaded_modules_map, MP_OBJ_NEW_QSTR(module_name), MP_MAP_LOOKUP_ADD_IF_NOT_FOUND);
    // We could error out if module already exists, but let C extensions
    // add new members to existing modules.
    if (el->value != MP_OBJ_NULL) {
        return el->value;
    }

    // create new module object
    mp_module_context_t *o = m_new_obj(mp_module_context_t);
    o->module.base.type = &mp_type_module;
    o->module.globals = MP_OBJ_TO_PTR(mp_obj_new_dict(MICROPY_MODULE_DICT_SIZE));

    // store __name__ entry in the module
    mp_obj_dict_store(MP_OBJ_FROM_PTR(o->module.globals), MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(module_name));

    // store the new module into the slot in the global dict holding all modules
    el->value = MP_OBJ_FROM_PTR(o);

    // return the new module
    return MP_OBJ_FROM_PTR(o);
}

/******************************************************************************/
// Global module table and related functions

STATIC const mp_rom_map_elem_t mp_builtin_module_table[] = {
    { MP_ROM_QSTR(MP_QSTR___main__), MP_ROM_PTR(&mp_module___main__) },
    { MP_ROM_QSTR(MP_QSTR_builtins), MP_ROM_PTR(&mp_module_builtins) },
    { MP_ROM_QSTR(MP_QSTR_micropython), MP_ROM_PTR(&mp_module_micropython) },

    #if MICROPY_PY_IO
    { MP_ROM_QSTR(MP_QSTR_uio), MP_ROM_PTR(&mp_module_io) },
    #endif
    #if MICROPY_PY_COLLECTIONS
    { MP_ROM_QSTR(MP_QSTR_ucollections), MP_ROM_PTR(&mp_module_collections) },
    #endif
    #if MICROPY_PY_STRUCT
    { MP_ROM_QSTR(MP_QSTR_ustruct), MP_ROM_PTR(&mp_module_ustruct) },
    #endif

    #if MICROPY_PY_BUILTINS_FLOAT
    #if MICROPY_PY_MATH
    { MP_ROM_QSTR(MP_QSTR_math), MP_ROM_PTR(&mp_module_math) },
    #endif
    #if MICROPY_PY_BUILTINS_COMPLEX && MICROPY_PY_CMATH
    { MP_ROM_QSTR(MP_QSTR_cmath), MP_ROM_PTR(&mp_module_cmath) },
    #endif
    #endif
    #if MICROPY_PY_SYS
    { MP_ROM_QSTR(MP_QSTR_usys), MP_ROM_PTR(&mp_module_sys) },
    #endif
    #if MICROPY_PY_GC && MICROPY_ENABLE_GC
    { MP_ROM_QSTR(MP_QSTR_gc), MP_ROM_PTR(&mp_module_gc) },
    #endif
    #if MICROPY_PY_THREAD
    { MP_ROM_QSTR(MP_QSTR__thread), MP_ROM_PTR(&mp_module_thread) },
    #endif

    // extmod modules

    #if MICROPY_PY_UASYNCIO
    { MP_ROM_QSTR(MP_QSTR__uasyncio), MP_ROM_PTR(&mp_module_uasyncio) },
    #endif
    #if MICROPY_PY_UERRNO
    { MP_ROM_QSTR(MP_QSTR_uerrno), MP_ROM_PTR(&mp_module_uerrno) },
    #endif
    #if MICROPY_PY_UCTYPES
    { MP_ROM_QSTR(MP_QSTR_uctypes), MP_ROM_PTR(&mp_module_uctypes) },
    #endif
    #if MICROPY_PY_UZLIB
    { MP_ROM_QSTR(MP_QSTR_uzlib), MP_ROM_PTR(&mp_module_uzlib) },
    #endif
    #if MICROPY_PY_UJSON
    { MP_ROM_QSTR(MP_QSTR_ujson), MP_ROM_PTR(&mp_module_ujson) },
    #endif
    #if MICROPY_PY_UOS
    { MP_ROM_QSTR(MP_QSTR_uos), MP_ROM_PTR(&mp_module_uos) },
    #endif
    #if MICROPY_PY_URE
    { MP_ROM_QSTR(MP_QSTR_ure), MP_ROM_PTR(&mp_module_ure) },
    #endif
    #if MICROPY_PY_UHEAPQ
    { MP_ROM_QSTR(MP_QSTR_uheapq), MP_ROM_PTR(&mp_module_uheapq) },
    #endif
    #if MICROPY_PY_UTIMEQ
    { MP_ROM_QSTR(MP_QSTR_utimeq), MP_ROM_PTR(&mp_module_utimeq) },
    #endif
    #if MICROPY_PY_UHASHLIB
    { MP_ROM_QSTR(MP_QSTR_uhashlib), MP_ROM_PTR(&mp_module_uhashlib) },
    #endif
    #if MICROPY_PY_UCRYPTOLIB
    { MP_ROM_QSTR(MP_QSTR_ucryptolib), MP_ROM_PTR(&mp_module_ucryptolib) },
    #endif
    #if MICROPY_PY_UBINASCII
    { MP_ROM_QSTR(MP_QSTR_ubinascii), MP_ROM_PTR(&mp_module_ubinascii) },
    #endif
    #if MICROPY_PY_URANDOM
    { MP_ROM_QSTR(MP_QSTR_urandom), MP_ROM_PTR(&mp_module_urandom) },
    #endif
    #if MICROPY_PY_USELECT
    { MP_ROM_QSTR(MP_QSTR_uselect), MP_ROM_PTR(&mp_module_uselect) },
    #endif
    #if MICROPY_PY_USSL
    { MP_ROM_QSTR(MP_QSTR_ussl), MP_ROM_PTR(&mp_module_ussl) },
    #endif
    #if MICROPY_PY_LWIP
    { MP_ROM_QSTR(MP_QSTR_lwip), MP_ROM_PTR(&mp_module_lwip) },
    #endif
    #if MICROPY_PY_MACHINE
    { MP_ROM_QSTR(MP_QSTR_umachine), MP_ROM_PTR(&mp_module_machine) },
    #endif
    #if MICROPY_PY_UWEBSOCKET
    { MP_ROM_QSTR(MP_QSTR_uwebsocket), MP_ROM_PTR(&mp_module_uwebsocket) },
    #endif
    #if MICROPY_PY_WEBREPL
    { MP_ROM_QSTR(MP_QSTR__webrepl), MP_ROM_PTR(&mp_module_webrepl) },
    #endif
    #if MICROPY_PY_FRAMEBUF
    { MP_ROM_QSTR(MP_QSTR_framebuf), MP_ROM_PTR(&mp_module_framebuf) },
    #endif
    #if MICROPY_PY_BTREE
    { MP_ROM_QSTR(MP_QSTR_btree), MP_ROM_PTR(&mp_module_btree) },
    #endif
    #if MICROPY_PY_BLUETOOTH
    { MP_ROM_QSTR(MP_QSTR_ubluetooth), MP_ROM_PTR(&mp_module_ubluetooth) },
    #endif
    #if MICROPY_PY_UPLATFORM
    { MP_ROM_QSTR(MP_QSTR_uplatform), MP_ROM_PTR(&mp_module_uplatform) },
    #endif

    // extra builtin modules as defined by a port
    MICROPY_PORT_BUILTIN_MODULES

    #ifdef MICROPY_REGISTERED_MODULES
    // builtin modules declared with MP_REGISTER_MODULE()
    MICROPY_REGISTERED_MODULES
    #endif
};

MP_DEFINE_CONST_MAP(mp_builtin_module_map, mp_builtin_module_table);

// Tries to find a loaded module, otherwise attempts to load a builtin, otherwise MP_OBJ_NULL.
mp_obj_t mp_module_get_loaded_or_builtin(qstr module_name) {
    // First try loaded modules.
    mp_map_elem_t *elem = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map, MP_OBJ_NEW_QSTR(module_name), MP_MAP_LOOKUP);

    if (!elem) {
        #if MICROPY_MODULE_WEAK_LINKS
        return mp_module_get_builtin(module_name);
        #else
        // Otherwise try builtin.
        elem = mp_map_lookup((mp_map_t *)&mp_builtin_module_map, MP_OBJ_NEW_QSTR(module_name), MP_MAP_LOOKUP);
        if (!elem) {
            return MP_OBJ_NULL;
        }

        #if MICROPY_MODULE_BUILTIN_INIT
        // If found, it's a newly loaded built-in, so init it.
        mp_module_call_init(MP_OBJ_NEW_QSTR(module_name), elem->value);
        #endif
        #endif
    }

    return elem->value;
}

#if MICROPY_MODULE_WEAK_LINKS
// Tries to find a loaded module, otherwise attempts to load a builtin, otherwise MP_OBJ_NULL.
mp_obj_t mp_module_get_builtin(qstr module_name) {
    // Try builtin.
    mp_map_elem_t *elem = mp_map_lookup((mp_map_t *)&mp_builtin_module_map, MP_OBJ_NEW_QSTR(module_name), MP_MAP_LOOKUP);
    if (!elem) {
        return MP_OBJ_NULL;
    }

    #if MICROPY_MODULE_BUILTIN_INIT
    // If found, it's a newly loaded built-in, so init it.
    mp_module_call_init(MP_OBJ_NEW_QSTR(module_name), elem->value);
    #endif

    return elem->value;
}
#endif

#if MICROPY_MODULE_BUILTIN_INIT
STATIC void mp_module_register(mp_obj_t module_name, mp_obj_t module) {
    mp_map_t *mp_loaded_modules_map = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_lookup(mp_loaded_modules_map, module_name, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = module;
}

STATIC void mp_module_call_init(mp_obj_t module_name, mp_obj_t module_obj) {
    // Look for __init__ and call it if it exists
    mp_obj_t dest[2];
    mp_load_method_maybe(module_obj, MP_QSTR___init__, dest);
    if (dest[0] != MP_OBJ_NULL) {
        mp_call_method_n_kw(0, 0, dest);
        // Register module so __init__ is not called again.
        // If a module can be referenced by more than one name (eg due to weak links)
        // then __init__ will still be called for each distinct import, and it's then
        // up to the particular module to make sure it's __init__ code only runs once.
        mp_module_register(module_name, module_obj);
    }
}
#endif

void mp_module_generic_attr(qstr attr, mp_obj_t *dest, const uint16_t *keys, mp_obj_t *values) {
    for (size_t i = 0; keys[i] != MP_QSTRnull; ++i) {
        if (attr == keys[i]) {
            if (dest[0] == MP_OBJ_NULL) {
                // load attribute (MP_OBJ_NULL returned for deleted items)
                dest[0] = values[i];
            } else {
                // delete or store (delete stores MP_OBJ_NULL)
                values[i] = dest[1];
                dest[0] = MP_OBJ_NULL; // indicate success
            }
            return;
        }
    }
}
