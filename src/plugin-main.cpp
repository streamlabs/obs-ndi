/******************************************************************************
	Copyright (C) 2016-2024 DistroAV <contact@distroav.org>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, see <https://www.gnu.org/licenses/>.
******************************************************************************/


#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <sys/stat.h>
#include <obs-module.h>
#include <util/platform.h>

#include "plugin-main.h"

#include <iostream>
#include <vector>
#include <sstream>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

const char *obs_module_name()
{
	return "obs-ndi";
}

const char *obs_module_description()
{
	return "NDI input/output integration for OBS Studio";
}

// TODO: FIXMEEEEEEEEEEEEEEEEEEEEEEEEEEEEE !!!!!!!!!!!!!!!!!!!!!!!!!
const NDIlib_v5 *ndiLib = nullptr;

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

const NDIlib_v5 *load_ndilib();

typedef const NDIlib_v5 *(*NDIlib_v5_load_)(void);


bool check_ndilib_version(std::string version);

#ifdef WIN32
HINSTANCE hGetProcIDDLL;
#endif

NDIlib_find_instance_t ndi_finder = nullptr;

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-ndi] obs_module_load");

	ndiLib = load_ndilib();
	if (!ndiLib) {
		blog(LOG_ERROR,
		     "[obs-ndi] obs_module_load: load_ndilib() failed; Module won't load.");
		return false;
	}

	if (!ndiLib->initialize()) {
		blog(LOG_ERROR,
		     "[obs-ndi] obs_module_load: ndiLib->initialize() failed; CPU unsupported by NDI library. Module won't load.");
		return false;
	}

	blog(LOG_INFO,
	     "[obs-ndi] obs_module_load: NDI library initialized successfully ('%s')",
	     ndiLib->version());

	NDIlib_find_create_t find_desc = {0};
	find_desc.show_local_sources = true;
	find_desc.p_groups = NULL;
	ndi_finder = ndiLib->find_create_v2(&find_desc);

	ndi_source_info = create_ndi_source_info();
	obs_register_source(&ndi_source_info);

	ndi_output_info = create_ndi_output_info();
	obs_register_output(&ndi_output_info);

	ndi_filter_info = create_ndi_filter_info();
	obs_register_source(&ndi_filter_info);

	ndi_audiofilter_info = create_ndi_audiofilter_info();
	obs_register_source(&ndi_audiofilter_info);

	alpha_filter_info = create_alpha_filter_info();
	obs_register_source(&alpha_filter_info);

	return true;
}

void obs_module_post_load(void)
{
	blog(LOG_INFO, "[obs-ndi] obs_module_post_load: ...");
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[obs-ndi] +obs_module_unload()");

	if (ndiLib) {
		if (ndi_finder) {
			ndiLib->find_destroy(ndi_finder);
			ndi_finder = nullptr;
		}
		ndiLib->destroy();
		ndiLib = nullptr;
	}

#ifdef WIN32
	if (hGetProcIDDLL)
		FreeLibrary(hGetProcIDDLL);
#else
	//TODO: FIXME
#endif

	blog(LOG_INFO, "[obs-ndi] obs_module_unload: goodbye !");
}

#ifdef WIN32
const NDIlib_v5 *load_ndilib()
{
	const int szEnvVar =
		GetEnvironmentVariable(TEXT(NDILIB_REDIST_FOLDER), 0, 0);

	if (szEnvVar == 0)
		return nullptr;

	std::basic_string<TCHAR> strEnvVar;
	/* Reserve isn't good enough here since
     * using a basic_string as a buffer will
     * automatically adjust its size. */
	strEnvVar.resize(szEnvVar - 1);

	GetEnvironmentVariable(TEXT(NDILIB_REDIST_FOLDER), &strEnvVar[0],
			       szEnvVar);

	std::basic_string<TCHAR> strLibName(TEXT(NDILIB_LIBRARY_NAME));

	std::basic_string<TCHAR> strPath;
	strPath.append(strEnvVar);
	strPath.append(TEXT("\\"));
	strPath.append(strLibName);

	NDIlib_v5_load_ lib_load = nullptr;
	// Load NewTek NDI Redist dll
	SetDllDirectory(strEnvVar.c_str());
	hGetProcIDDLL = LoadLibrary(strPath.data());
	SetDllDirectory(NULL);

	if (hGetProcIDDLL == NULL) {
		blog(LOG_INFO,
		     "ERROR: NDIlib_v3_load not found in loaded library");
	} else {
		blog(LOG_INFO, "NDI runtime loaded successfully");

		// Locate function in DLL.
		lib_load = (NDIlib_v5_load_)GetProcAddress(hGetProcIDDLL,
							   "NDIlib_v5_load");

		// Check if function was located.
		if (!lib_load) {
			blog(LOG_INFO,
			     "ERROR: NDIlib_v5_load not found in loaded library");
		} else {
			return lib_load();
		}
	}

	blog(LOG_ERROR, "Can't find the NDI library");
	return nullptr;
}

#else

const NDIlib_v5 *load_ndilib()
{
	std::vector<const char *> locations;
	const char *redist_folder = getenv("NDILIB_REDIST_FOLDER");

	if (redist_folder)
		locations.push_back(redist_folder);

	locations.push_back("/usr/lib/");
	locations.push_back("/usr/local/lib/");

	for (auto path : locations) {
		std::string lib = path;
		lib += NDILIB_LIBRARY_NAME;

		blog(LOG_INFO, "Trying to load lib at: %s", lib.c_str());

		FILE *file = fopen(lib.c_str(), "r");
		if (!file)
			continue;

		fclose(file);
		blog(LOG_INFO, "Found NDI library at '%s'", lib.c_str());

		void *handle = dlopen(lib.c_str(), RTLD_NOW);

		if (!handle)
			continue;

		blog(LOG_INFO, "NDI runtime loaded successfully");

		NDIlib_v5_load_ lib_load =
			(NDIlib_v5_load_)dlsym(handle, "NDIlib_v5_load");
		if (!lib_load) {
			blog(LOG_INFO,
			     "ERROR: NDIlib_v5_load not found in loaded library");
		} else {
			return lib_load();
		}
	}

	blog(LOG_ERROR, "Can't find the NDI library");
	return nullptr;
}

#endif

bool check_ndilib_version(std::string version)
{
	std::string versionNumber = version.substr(version.rfind(' ') + 1);

	std::string majorVersionNumber = versionNumber.substr(0, versionNumber.find('.'));
	versionNumber.erase(0, versionNumber.find('.') + 1);

	std::string minorVersionNumber =
		versionNumber.substr(0, versionNumber.find('.'));
	versionNumber.erase(0, versionNumber.find('.') + 1);
	try {
		if (std::stoi(majorVersionNumber) <
		    NDI_LIB_MAJOR_VERSION_NUMBER) {
			return false;
		}

		if (std::stoi(majorVersionNumber) == NDI_LIB_MAJOR_VERSION_NUMBER &&
		    std::stoi(minorVersionNumber) < NDI_LIB_MINOR_VERSION_NUMBER) {
			return false;
		}
	} catch (...) {
		if (version.find(" .1.0.0") !=
		    std::string::npos) { // whitelist ndi broken version
			return true;
		}
		return false;
	}

	return true;
}
