# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

#

#--------------------------------------------------
# Initialize FnGeolib3 first

def kodachi_bootstrap_and_initialize():
    import os
    import ctypes

    kodachi_root          = os.environ[ 'REZ_KODACHI_ROOT' ]
    # NOTE: this is Foundry's namespaced boost (Fnboost namespace)
    boost_python_so_path  = kodachi_root + '/bin/libFnboost_python.so.1.61.0'
    kodachi_so_path       = kodachi_root + '/lib/libkodachi.so'
    kodachi_cache_so_path = kodachi_root + '/lib/libkodachi_cache.so'
    pykodachi_so_path     = kodachi_root + '/python/kodachi/pykodachi.so'
    
    kodachi_so       = ctypes.CDLL(kodachi_so_path,       mode = ctypes.RTLD_GLOBAL)
    if os.path.exists(boost_python_so_path):
        boost_so         = ctypes.CDLL(boost_python_so_path,  mode = ctypes.RTLD_GLOBAL)
    kodachi_cache_so = ctypes.CDLL(kodachi_cache_so_path, mode = ctypes.RTLD_GLOBAL)    
    pykodachi_so     = ctypes.CDLL(pykodachi_so_path,     mode = ctypes.RTLD_GLOBAL)
    
    pykodachi_bootstrap_func = pykodachi_so['pykodachi_bootstrap']
    pykodachi_initialize_func = pykodachi_so['pykodachi_initialize']

    # Bootstrap Geolib3 runtime
    if pykodachi_bootstrap_func("") == 1:
        # If bootstrap successful, initialize Kodachi/Katana components.
        # Calls kodachi::T::setHost(FnPluginHost*), where T is a component.
        pykodachi_initialize_func()
        return True
        
    return False

kodachi_bootstrap_and_initialize()

from pykodachi import *
from PyFnAttribute import *
import PyFnGeolibServices as FnGeolibServices
from PyFnGeolibServices import OpArgsBuilders as op_args_builder

#--------------------------------------------------


