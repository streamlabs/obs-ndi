/*
obs-ndi (NDI I/O in OBS Studio)
Copyright (C) 2016-2017 Stéphane Lepin <stephane.lepin@gmail.com>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library. If not, see <https://www.gnu.org/licenses/>
*/

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <sys/stat.h>

#include <obs-module.h>
#include <util/platform.h>

#include "obs-ndi.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Stephane Lepin (Palakis)")
OBS_MODULE_USE_DEFAULT_LOCALE("obs-ndi", "en-US")

const NDIlib_v3* ndiLib = nullptr;

extern struct obs_source_info create_ndi_source_info();
struct obs_source_info ndi_source_info;

extern struct obs_output_info create_ndi_output_info();
struct obs_output_info ndi_output_info;

extern struct obs_source_info create_ndi_filter_info();
struct obs_source_info ndi_filter_info;

extern struct obs_source_info create_ndi_audiofilter_info();
struct obs_source_info ndi_audiofilter_info;

extern struct obs_source_info create_alpha_filter_info();
struct obs_source_info alpha_filter_info;

const NDIlib_v3* load_ndilib();

#ifdef _WIN32
HINSTANCE hGetProcIDDLL;
#else
void *hGetProcIDDLL = nullptr;
#endif

typedef const NDIlib_v3* (*NDIlib_v3_load_)(void);

NDIlib_find_instance_t ndi_finder;
obs_output_t* main_out;
bool main_output_running = false;

#ifndef _WIN32
static const NDIlib_v3 *try_load_ndilib(const std::string &path)
{
	dlerror();
	void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		const char *error = dlerror();
		blog(LOG_DEBUG, "Unable to load NDI runtime candidate '%s': %s", path.c_str(),
		     error ? error : "unknown error");
		return nullptr;
	}

	dlerror();
	NDIlib_v3_load_ lib_load = reinterpret_cast<NDIlib_v3_load_>(dlsym(handle, "NDIlib_v3_load"));
	const char *error = dlerror();
	if (error || !lib_load) {
		blog(LOG_ERROR, "NDIlib_v3_load not found in loaded library '%s': %s", path.c_str(),
		     error ? error : "unknown error");
		dlclose(handle);
		return nullptr;
	}

	const NDIlib_v3 *lib = lib_load();
	if (!lib) {
		blog(LOG_ERROR, "NDIlib_v3_load returned null for loaded library '%s'", path.c_str());
		dlclose(handle);
		return nullptr;
	}

	hGetProcIDDLL = handle;
	blog(LOG_INFO, "NDI runtime loaded successfully from '%s'", path.c_str());
	return lib;
}

static void add_runtime_dir_candidates(std::vector<std::string> &candidates, const char *runtimeDir)
{
	if (!runtimeDir || !*runtimeDir)
		return;

	std::string base = runtimeDir;
	candidates.push_back(base + "/" + NDILIB_LIBRARY_NAME);
#ifdef __APPLE__
	candidates.push_back(base + "/libndi.dylib");
	candidates.push_back(base + "/libndi_advanced.dylib");
#endif
}
#endif

bool obs_module_load(void) {
    blog(LOG_INFO, "hello ! (version %s)", OBS_NDI_VERSION);
	
    ndiLib = load_ndilib();
    if (!ndiLib) {
		blog(LOG_ERROR, "Error when loading the library.");
        return false;
    }

    if (!ndiLib->NDIlib_initialize()) {
        blog(LOG_ERROR, "CPU unsupported by NDI library. Module won't load.");
        return false;
    }

    blog(LOG_INFO, "NDI library initialized successfully");

    NDIlib_find_create_t find_desc = {0};
    find_desc.show_local_sources = true;
    find_desc.p_groups = NULL;
    ndi_finder = ndiLib->NDIlib_find_create_v2(&find_desc);

    ndi_source_info = create_ndi_source_info();
    obs_register_source(&ndi_source_info);

    ndi_filter_info = create_ndi_filter_info();
    obs_register_source(&ndi_filter_info);

    ndi_audiofilter_info = create_ndi_audiofilter_info();
    obs_register_source(&ndi_audiofilter_info);

    alpha_filter_info = create_alpha_filter_info();
    obs_register_source(&alpha_filter_info);

	return true;
}

void obs_module_unload() {
    blog(LOG_INFO, "goodbye !");

    if (ndiLib) {
        ndiLib->NDIlib_find_destroy(ndi_finder);
        ndiLib->NDIlib_destroy();
    }

	if (hGetProcIDDLL) {
#ifdef _WIN32
		FreeLibrary(hGetProcIDDLL);
#else
		dlclose(hGetProcIDDLL);
#endif
		hGetProcIDDLL = nullptr;
	}
}

const char* obs_module_name() {
    return "obs-ndi";
}

const char* obs_module_description() {
    return "NDI input/output integration for OBS Studio";
}

const NDIlib_v3* load_ndilib() {
#ifdef _WIN32
    const int szEnvVar = GetEnvironmentVariable(TEXT(NDILIB_REDIST_FOLDER), 0, 0);

    if (szEnvVar == 0) return nullptr;

    std::basic_string<TCHAR> strEnvVar;
    /* Reserve isn't good enough here since
     * using a basic_string as a buffer will
     * automatically adjust its size. */
    strEnvVar.resize(szEnvVar - 1);

    GetEnvironmentVariable(TEXT(NDILIB_REDIST_FOLDER), &strEnvVar[0], szEnvVar);

    std::basic_string<TCHAR> strLibName(TEXT(NDILIB_LIBRARY_NAME));

    std::basic_string<TCHAR> strPath;
    strPath.append(strEnvVar);
    strPath.append(TEXT("\\"));
    strPath.append(strLibName);

	NDIlib_v3_load_ lib_load = nullptr;
	// Load NewTek NDI Redist dll
	hGetProcIDDLL = LoadLibrary(strPath.data());

	if (hGetProcIDDLL == NULL) {
		blog(LOG_INFO, "ERROR: NDIlib_v3_load not found in loaded library");
	}
	else {
		blog(LOG_INFO, "NDI runtime loaded successfully");

		// Locate function in DLL.
		lib_load = (NDIlib_v3_load_)GetProcAddress(hGetProcIDDLL, "NDIlib_v3_load");

		// Check if function was located.
		if (!lib_load) {
			blog(LOG_INFO, "ERROR: NDIlib_v3_load not found in loaded library");
		}
		else {
			return lib_load();
						
		}
	}

    blog(LOG_ERROR, "Can't find the NDI library");
    return nullptr;
#else
	const char *runtimeDir = std::getenv(NDILIB_REDIST_FOLDER);
	std::vector<std::string> candidates;
	add_runtime_dir_candidates(candidates, runtimeDir);

#ifdef __APPLE__
	candidates.push_back("/Applications/NDI Scan Converter.app/Contents/Frameworks/libndi.dylib");
	candidates.push_back("/Applications/NDI Video Monitor.app/Contents/Frameworks/libndi_advanced.dylib");
	candidates.push_back("/Applications/NDI Test Patterns.app/Contents/Frameworks/libndi_advanced.dylib");
	candidates.push_back("/Applications/NDI Virtual Input.app/Contents/Frameworks/libndi_advanced.dylib");
	candidates.push_back("/Applications/NDI Discovery.app/Contents/Frameworks/libndi_advanced.dylib");
	candidates.push_back("libndi.dylib");
	candidates.push_back("libndi_advanced.dylib");
#else
	candidates.push_back(NDILIB_LIBRARY_NAME);
#endif

	for (const std::string &candidate : candidates) {
		if (const NDIlib_v3 *lib = try_load_ndilib(candidate))
			return lib;
	}

	if (!runtimeDir) {
		blog(LOG_ERROR, "Can't find the NDI library. The runtime environment variable '%s' is not set.",
		     NDILIB_REDIST_FOLDER);
	} else {
		blog(LOG_ERROR, "Can't find the NDI library using runtime directory '%s'", runtimeDir);
	}

	return nullptr;
#endif
}
