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

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
bool older_ndilib_runtime_exists();

typedef const NDIlib_v5 *(*NDIlib_v5_load_)(void);

bool check_ndilib_version(std::string version);

#ifdef _WIN32
HINSTANCE hGetProcIDDLL = nullptr;
#else
void *hGetProcIDDLL = nullptr;
#endif

NDIlib_find_instance_t ndi_finder = nullptr;

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-ndi] obs_module_load");

	ndiLib = load_ndilib();
	if (!ndiLib) {
		const bool olderNdiRuntimeInstalled = older_ndilib_runtime_exists();
		const char *code = olderNdiRuntimeInstalled ? NDI_RUNTIME_VERSION_MISMATCH : NDI_RUNTIME_NOT_FOUND;
		std::string message = olderNdiRuntimeInstalled
					      ? "An older NDI Runtime is installed; " PLUGIN_MIN_NDI_VERSION
						" or newer is required."
					      : "NDI Runtime " PLUGIN_MIN_NDI_VERSION " or newer was not found.";
		obs_module_set_load_error(obs_current_module(), code, message.c_str());
		blog(LOG_ERROR, "[obs-ndi] obs_module_load: %s Module won't load.", message.c_str());
		return false;
	}

	const char *ndiVersion = ndiLib->version();
	const std::string ndiVersionString = ndiVersion ? ndiVersion : "";
	if (!check_ndilib_version(ndiVersionString)) {
		std::string message = "Installed NDI Runtime version '" + ndiVersionString +
				      "' is not supported; " PLUGIN_MIN_NDI_VERSION " or newer is required.";
		obs_module_set_load_error(obs_current_module(), NDI_RUNTIME_VERSION_MISMATCH, message.c_str());
		blog(LOG_ERROR, "[obs-ndi] obs_module_load: %s Module won't load.", message.c_str());
		return false;
	}

	if (!ndiLib->initialize()) {
		blog(LOG_ERROR,
		     "[obs-ndi] obs_module_load: ndiLib->initialize() failed; CPU unsupported by NDI library. Module won't load.");
		return false;
	}

	blog(LOG_INFO, "[obs-ndi] obs_module_load: NDI library initialized successfully ('%s')",
	     ndiVersionString.c_str());

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

#ifdef _WIN32
	if (hGetProcIDDLL) {
		FreeLibrary(hGetProcIDDLL);
		hGetProcIDDLL = nullptr;
	}
#else
	if (hGetProcIDDLL) {
		dlclose(hGetProcIDDLL);
		hGetProcIDDLL = nullptr;
	}
#endif

	blog(LOG_INFO, "[obs-ndi] obs_module_unload: goodbye !");
}

#ifdef _WIN32
static bool expand_registry_string(const std::basic_string<TCHAR> &rawValue, std::basic_string<TCHAR> &value)
{
	const DWORD size = ExpandEnvironmentStrings(rawValue.c_str(), nullptr, 0);
	if (!size)
		return false;

	std::vector<TCHAR> buffer(size);
	ExpandEnvironmentStrings(rawValue.c_str(), buffer.data(), size);
	value = buffer.data();
	return !value.empty();
}

static bool read_registry_env_var(HKEY root, const TCHAR *subKey, const TCHAR *name, std::basic_string<TCHAR> &value)
{
	DWORD type = 0;
	DWORD size = 0;
	LSTATUS status = RegGetValue(root, subKey, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, nullptr, &size);
	if (status != ERROR_SUCCESS || size <= sizeof(TCHAR))
		return false;

	std::vector<TCHAR> buffer(size / sizeof(TCHAR));
	status = RegGetValue(root, subKey, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, buffer.data(), &size);
	if (status != ERROR_SUCCESS)
		return false;

	std::basic_string<TCHAR> rawValue(buffer.data());
	if (type == REG_EXPAND_SZ)
		return expand_registry_string(rawValue, value);

	value = rawValue;
	return !value.empty();
}

static bool get_env_var(const TCHAR *name, std::basic_string<TCHAR> &value)
{
	const DWORD size = GetEnvironmentVariable(name, nullptr, 0);
	if (size > 1) {
		std::vector<TCHAR> buffer(size);
		GetEnvironmentVariable(name, buffer.data(), size);
		value = buffer.data();
		return true;
	}

	if (read_registry_env_var(HKEY_CURRENT_USER, TEXT("Environment"), name, value))
		return true;

	return read_registry_env_var(HKEY_LOCAL_MACHINE,
				     TEXT("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"), name,
				     value);
}

static bool file_exists(const std::basic_string<TCHAR> &path)
{
	const DWORD attributes = GetFileAttributes(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ndi_runtime_dll_exists(const TCHAR *envVarName)
{
	std::basic_string<TCHAR> runtimePath;
	if (!get_env_var(envVarName, runtimePath))
		return false;

	std::basic_string<TCHAR> libraryPath = runtimePath;
	libraryPath.append(TEXT("\\"));
	libraryPath.append(TEXT(NDILIB_LIBRARY_NAME));
	return file_exists(libraryPath);
}

bool older_ndilib_runtime_exists()
{
	const TCHAR *legacyRuntimeEnvVars[] = {
		TEXT("NDI_RUNTIME_DIR_V5"),
		TEXT("NDI_RUNTIME_DIR_V4"),
		TEXT("NDI_RUNTIME_DIR_V3"),
		TEXT("NDI_RUNTIME_DIR_V2"),
	};

	for (const TCHAR *envVarName : legacyRuntimeEnvVars) {
		if (ndi_runtime_dll_exists(envVarName))
			return true;
	}

	return false;
}

const NDIlib_v5 *load_ndilib()
{
	std::basic_string<TCHAR> strEnvVar;
	if (!get_env_var(TEXT(NDILIB_REDIST_FOLDER), strEnvVar))
		return nullptr;

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
		blog(LOG_INFO, "ERROR: NDIlib_v3_load not found in loaded library");
	} else {
		blog(LOG_INFO, "NDI runtime loaded successfully");

		// Locate function in DLL.
		lib_load = (NDIlib_v5_load_)GetProcAddress(hGetProcIDDLL, "NDIlib_v5_load");

		// Check if function was located.
		if (!lib_load) {
			blog(LOG_INFO, "ERROR: NDIlib_v5_load not found in loaded library");
		} else {
			return lib_load();
		}
	}

	blog(LOG_ERROR, "Can't find the NDI library");
	return nullptr;
}

#else

static std::string append_path_component(const std::string &base, const char *component)
{
	if (base.empty() || !component || !*component)
		return base;

	if (base.back() == '/')
		return base + component;

	return base + "/" + component;
}

static bool file_exists(const std::string &path)
{
	struct stat stats;
	return stat(path.c_str(), &stats) == 0 && S_ISREG(stats.st_mode);
}

bool older_ndilib_runtime_exists()
{
	const char *legacyRuntimeEnvVars[] = {
		"NDI_RUNTIME_DIR_V5",
		"NDI_RUNTIME_DIR_V4",
		"NDI_RUNTIME_DIR_V3",
		"NDI_RUNTIME_DIR_V2",
	};

#ifdef __APPLE__
	const char *legacyLibraryName = "libndi.dylib";
#else
	const char *legacyLibraryName = "libndi.so.5";
#endif

	for (const char *envVarName : legacyRuntimeEnvVars) {
		const char *runtimePath = getenv(envVarName);
		if (!runtimePath || !*runtimePath)
			continue;

		if (file_exists(append_path_component(runtimePath, legacyLibraryName)))
			return true;
	}

	return false;
}

static void add_runtime_dir_candidates(std::vector<std::string> &candidates, const char *runtimeDir)
{
	if (!runtimeDir || !*runtimeDir)
		return;

	const std::string base = runtimeDir;
	if (base.size() > 6 && base.substr(base.size() - 6) == ".dylib") {
		candidates.push_back(base);
		return;
	}

	candidates.push_back(append_path_component(base, NDILIB_LIBRARY_NAME));
#ifdef __APPLE__
	candidates.push_back(append_path_component(base, "libndi_advanced.dylib"));
#endif
}

static const NDIlib_v5 *try_load_ndilib(const std::string &path)
{
	blog(LOG_INFO, "Trying to load NDI runtime at: %s", path.c_str());

	dlerror();
	void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		const char *error = dlerror();
		blog(LOG_DEBUG, "Unable to load NDI runtime candidate '%s': %s", path.c_str(),
		     error ? error : "unknown error");
		return nullptr;
	}

	dlerror();
	NDIlib_v5_load_ lib_load = reinterpret_cast<NDIlib_v5_load_>(dlsym(handle, "NDIlib_v5_load"));
	const char *error = dlerror();
	if (error || !lib_load) {
		blog(LOG_ERROR, "NDIlib_v5_load not found in loaded library '%s': %s", path.c_str(),
		     error ? error : "unknown error");
		dlclose(handle);
		return nullptr;
	}

	const NDIlib_v5 *lib = lib_load();
	if (!lib) {
		blog(LOG_ERROR, "NDIlib_v5_load returned null for loaded library '%s'", path.c_str());
		dlclose(handle);
		return nullptr;
	}

	hGetProcIDDLL = handle;
	blog(LOG_INFO, "NDI runtime loaded successfully from '%s'", path.c_str());
	return lib;
}

const NDIlib_v5 *load_ndilib()
{
	std::vector<std::string> candidates;

	add_runtime_dir_candidates(candidates, getenv(NDILIB_REDIST_FOLDER));
	add_runtime_dir_candidates(candidates, getenv("NDILIB_REDIST_FOLDER"));
	add_runtime_dir_candidates(candidates, "/usr/lib");
	add_runtime_dir_candidates(candidates, "/usr/local/lib");

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
		if (const NDIlib_v5 *lib = try_load_ndilib(candidate))
			return lib;
	}

	if (!getenv(NDILIB_REDIST_FOLDER)) {
		blog(LOG_ERROR, "Can't find the NDI library. The runtime environment variable '%s' is not set.",
		     NDILIB_REDIST_FOLDER);
	} else {
		blog(LOG_ERROR, "Can't find the NDI library using runtime directory '%s'",
		     getenv(NDILIB_REDIST_FOLDER));
	}

	return nullptr;
}

#endif

bool check_ndilib_version(std::string version)
{
	std::string versionNumber = version.substr(version.rfind(' ') + 1);

	std::string majorVersionNumber = versionNumber.substr(0, versionNumber.find('.'));
	versionNumber.erase(0, versionNumber.find('.') + 1);

	std::string minorVersionNumber = versionNumber.substr(0, versionNumber.find('.'));
	versionNumber.erase(0, versionNumber.find('.') + 1);
	try {
		if (std::stoi(majorVersionNumber) < NDI_LIB_MAJOR_VERSION_NUMBER) {
			return false;
		}

		if (std::stoi(majorVersionNumber) == NDI_LIB_MAJOR_VERSION_NUMBER &&
		    std::stoi(minorVersionNumber) < NDI_LIB_MINOR_VERSION_NUMBER) {
			return false;
		}
	} catch (...) {
		if (version.find(" .1.0.0") != std::string::npos) { // whitelist ndi broken version
			return true;
		}
		return false;
	}

	return true;
}
