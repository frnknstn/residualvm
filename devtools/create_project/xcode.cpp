/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "xcode.h"

#include <fstream>
#include <algorithm>

#if defined(_WIN32) || defined(WIN32)
#include <windows.h>
#else
#include <sys/param.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#endif

namespace CreateProjectTool {

#define DEBUG_XCODE_HASH 0

#define ADD_DEFINE(defines, name) \
	defines.push_back(name);

#define ADD_SETTING(config, key, value) \
	config.settings[key] = Setting(value, "", SettingsNoQuote);

#define ADD_SETTING_ORDER(config, key, value, order) \
	config.settings[key] = Setting(value, "", SettingsNoQuote, 0, order);

#define ADD_SETTING_ORDER_NOVALUE(config, key, comment, order) \
	config.settings[key] = Setting("", comment, SettingsNoValue, 0, order);

#define ADD_SETTING_QUOTE(config, key, value) \
	config.settings[key] = Setting(value);

#define ADD_SETTING_QUOTE_VAR(config, key, value) \
	config.settings[key] = Setting(value, "", SettingsQuoteVariable);

#define ADD_SETTING_LIST(config, key, values, flags, indent) \
	config.settings[key] = Setting(values, flags, indent);

#define REMOVE_SETTING(config, key) \
	config.settings.erase(key);

#define ADD_BUILD_FILE(id, name, comment) { \
	Object *buildFile = new Object(this, id, name, "PBXBuildFile", "PBXBuildFile", comment); \
	buildFile->addProperty("fileRef", getHash(name), name, SettingsNoValue); \
	_buildFile.add(buildFile); \
	_buildFile.flags = SettingsSingleItem; \
}

#define ADD_FILE_REFERENCE(name, properties) { \
	Object *fileRef = new Object(this, name, name, "PBXFileReference", "PBXFileReference", name); \
	if (!properties.fileEncoding.empty()) fileRef->addProperty("fileEncoding", properties.fileEncoding, "", SettingsNoValue); \
	if (!properties.lastKnownFileType.empty()) fileRef->addProperty("lastKnownFileType", properties.lastKnownFileType, "", SettingsNoValue); \
	if (!properties.fileName.empty()) fileRef->addProperty("name", properties.fileName, "", SettingsNoValue); \
	if (!properties.filePath.empty()) fileRef->addProperty("path", properties.filePath, "", SettingsNoValue); \
	if (!properties.sourceTree.empty()) fileRef->addProperty("sourceTree", properties.sourceTree, "", SettingsNoValue); \
	_fileReference.add(fileRef); \
	_fileReference.flags = SettingsSingleItem; \
}

XCodeProvider::XCodeProvider(StringList &global_warnings, std::map<std::string, StringList> &project_warnings, const int version)
	: ProjectProvider(global_warnings, project_warnings, version) {
}

void XCodeProvider::createWorkspace(const BuildSetup &setup) {
	// Create project folder
	std::string workspace = setup.outputDir + '/' + "residualvm.xcodeproj";

#if defined(_WIN32) || defined(WIN32)
	if (!CreateDirectory(workspace.c_str(), NULL))
		if (GetLastError() != ERROR_ALREADY_EXISTS)
			error("Could not create folder \"" + setup.outputDir + '/' + "residualvm.xcodeproj\"");
#else
	if (mkdir(workspace.c_str(), 0777) == -1) {
		if (errno == EEXIST) {
			// Try to open as a folder (might be a file / symbolic link)
			DIR *dirp = opendir(workspace.c_str());
			if (dirp == NULL) {
				error("Could not create folder \"" + setup.outputDir + '/' + "residualvm.xcodeproj\"");
			} else {
				// The folder exists, just close the stream and return
				closedir(dirp);
			}
		} else {
			error("Could not create folder \"" + setup.outputDir + '/' + "residualvm.xcodeproj\"");
		}
	}
#endif

	// Setup global objects
	setupDefines(setup);
	_targets.push_back("ResidualVM-iPhone");
	_targets.push_back("ResidualVM-OS X");
	_targets.push_back("ResidualVM-Simulator");

	setupCopyFilesBuildPhase();
	setupFrameworksBuildPhase();
	setupNativeTarget();
	setupProject();
	setupResourcesBuildPhase();
	setupBuildConfiguration();
}

// We are done with constructing all the object graph and we got through every project, output the main project file
// (this is kind of a hack since other providers use separate project files)
void XCodeProvider::createOtherBuildFiles(const BuildSetup &setup) {
	// This needs to be done at the end when all build files have been accounted for
	setupSourcesBuildPhase();

	ouputMainProjectFile(setup);
}

// Store information about a project here, for use at the end
void XCodeProvider::createProjectFile(const std::string &, const std::string &, const BuildSetup &setup, const std::string &moduleDir,
                                      const StringList &includeList, const StringList &excludeList) {
	std::string modulePath;
	if (!moduleDir.compare(0, setup.srcDir.size(), setup.srcDir)) {
		modulePath = moduleDir.substr(setup.srcDir.size());
		if (!modulePath.empty() && modulePath.at(0) == '/')
			modulePath.erase(0, 1);
	}

	std::ofstream project;
	if (modulePath.size())
		addFilesToProject(moduleDir, project, includeList, excludeList, setup.filePrefix + '/' + modulePath);
	else
		addFilesToProject(moduleDir, project, includeList, excludeList, setup.filePrefix);
}

//////////////////////////////////////////////////////////////////////////
// Main Project file
//////////////////////////////////////////////////////////////////////////
void XCodeProvider::ouputMainProjectFile(const BuildSetup &setup) {
	std::ofstream project((setup.outputDir + '/' + "residualvm.xcodeproj" + '/' + "project.pbxproj").c_str());
	if (!project)
		error("Could not open \"" + setup.outputDir + '/' + "residualvm.xcodeproj" + '/' + "project.pbxproj\" for writing");

	//////////////////////////////////////////////////////////////////////////
	// Header
	project << "// !$*UTF8*$!\n"
	           "{\n"
	           "\t" << writeSetting("archiveVersion", "1", "", SettingsNoQuote) << ";\n"
	           "\tclasses = {\n"
	           "\t};\n"
	           "\t" << writeSetting("objectVersion", "46", "", SettingsNoQuote) << ";\n"
	           "\tobjects = {\n";

	//////////////////////////////////////////////////////////////////////////
	// List of objects
	project << _buildFile.toString();
	project << _copyFilesBuildPhase.toString();
	project << _fileReference.toString();
	project << _frameworksBuildPhase.toString();
	project << _groups.toString();
	project << _nativeTarget.toString();
	project << _project.toString();
	project << _resourcesBuildPhase.toString();
	project << _sourcesBuildPhase.toString();
	project << _buildConfiguration.toString();
	project << _configurationList.toString();

	//////////////////////////////////////////////////////////////////////////
	// Footer
	project << "\t};\n"
	           "\t" << writeSetting("rootObject", getHash("PBXProject"), "Project object", SettingsNoQuote) << ";\n"
	           "}\n";

}

//////////////////////////////////////////////////////////////////////////
// Files
//////////////////////////////////////////////////////////////////////////
void XCodeProvider::writeFileListToProject(const FileNode &dir, std::ofstream &projectFile, const int indentation,
                                           const StringList &duplicate, const std::string &objPrefix, const std::string &filePrefix) {

	// Add comments for shared lists
	_buildFile.comment = "PBXBuildFile";
	_fileReference.comment = "PBXFileReference";

	// Init root group
	_groups.comment = "PBXGroup";
	Object *group = new Object(this, "PBXGroup", "PBXGroup", "PBXGroup", "", "");

	//Property children;
	//children.flags = SettingsAsList;
	//group->properties["children"] = children;
	group->addProperty("children", "", "", SettingsNoValue|SettingsAsList);

	group->addProperty("sourceTree", "<group>", "", SettingsNoValue|SettingsQuoteVariable);

	_groups.add(group);

	// TODO Add files
}

//////////////////////////////////////////////////////////////////////////
// Setup functions
//////////////////////////////////////////////////////////////////////////
void XCodeProvider::setupCopyFilesBuildPhase() {
	// Nothing to do here
}

/**
 * Sets up the frameworks build phase.
 *
 * (each native target has different build rules)
 */
void XCodeProvider::setupFrameworksBuildPhase() {
	_frameworksBuildPhase.comment = "PBXFrameworksBuildPhase";

	// Setup framework file properties
	std::map<std::string, FileProperty> properties;

	// Frameworks
	properties["ApplicationServices.framework"] = FileProperty("wrapper.framework", "ApplicationServices.framework", "System/Library/Frameworks/ApplicationServices.framework", "SDKROOT");
	properties["AudioToolbox.framework"]        = FileProperty("wrapper.framework", "AudioToolbox.framework", "System/Library/Frameworks/AudioToolbox.framework", "SDKROOT");
	properties["AudioUnit.framework"]           = FileProperty("wrapper.framework", "AudioUnit.framework", "System/Library/Frameworks/AudioUnit.framework", "SDKROOT");
	properties["Carbon.framework"]              = FileProperty("wrapper.framework", "Carbon.framework", "System/Library/Frameworks/Carbon.framework", "SDKROOT");
	properties["Cocoa.framework"]               = FileProperty("wrapper.framework", "Cocoa.framework", "System/Library/Frameworks/Cocoa.framework", "SDKROOT");
	properties["CoreAudio.framework"]           = FileProperty("wrapper.framework", "CoreAudio.framework", "System/Library/Frameworks/CoreAudio.framework", "SDKROOT");
	properties["CoreFoundation.framework"]      = FileProperty("wrapper.framework", "CoreFoundation.framework", "System/Library/Frameworks/CoreFoundation.framework", "SDKROOT");
	properties["CoreMIDI.framework"]            = FileProperty("wrapper.framework", "CoreMIDI.framework", "System/Library/Frameworks/CoreMIDI.framework", "SDKROOT");
	properties["Foundation.framework"]          = FileProperty("wrapper.framework", "Foundation.framework", "System/Library/Frameworks/Foundation.framework", "SDKROOT");
	properties["IOKit.framework"]               = FileProperty("wrapper.framework", "IOKit.framework", "System/Library/Frameworks/IOKit.framework", "SDKROOT");
	properties["OpenGLES.framework"]            = FileProperty("wrapper.framework", "OpenGLES.framework", "System/Library/Frameworks/OpenGLES.framework", "SDKROOT");
	properties["QuartzCore.framework"]          = FileProperty("wrapper.framework", "QuartzCore.framework", "System/Library/Frameworks/QuartzCore.framework", "SDKROOT");
	properties["QuickTime.framework"]           = FileProperty("wrapper.framework", "QuickTime.framework", "System/Library/Frameworks/QuickTime.framework", "SDKROOT");
	properties["UIKit.framework"]               = FileProperty("wrapper.framework", "UIKit.framework", "System/Library/Frameworks/UIKit.framework", "SDKROOT");

	// Local libraries
	properties["libFLAC.a"]                     = FileProperty("archive.ar", "libFLAC.a", "lib/libFLAC.a", "\"<group>\"");
	properties["libmad.a"]                      = FileProperty("archive.ar", "libmad.a", "lib/libmad.a", "\"<group>\"");
	//properties["libmpeg2.a"]                    = FileProperty("archive.ar", "libmpeg2.a", "lib/libmpeg2.a", "\"<group>\"");
	properties["libvorbisidec.a"]               = FileProperty("archive.ar", "libvorbisidec.a", "lib/libvorbisidec.a", "\"<group>\"");

	//////////////////////////////////////////////////////////////////////////
	// iPhone
	Object *framework_iPhone = new Object(this, "PBXFrameworksBuildPhase_" + _targets[0], "PBXFrameworksBuildPhase", "PBXFrameworksBuildPhase", "", "Frameworks");

	framework_iPhone->addProperty("buildActionMask", "2147483647", "", SettingsNoValue);
	framework_iPhone->addProperty("runOnlyForDeploymentPostprocessing", "0", "", SettingsNoValue);

	// List of frameworks
	Property iPhone_files;
	iPhone_files.hasOrder = true;
	iPhone_files.flags = SettingsAsList;

	ValueList frameworks_iPhone;
	frameworks_iPhone.push_back("CoreAudio.framework");
	frameworks_iPhone.push_back("CoreFoundation.framework");
	frameworks_iPhone.push_back("Foundation.framework");
	frameworks_iPhone.push_back("UIKit.framework");
	frameworks_iPhone.push_back("AudioToolbox.framework");
	frameworks_iPhone.push_back("QuartzCore.framework");
	frameworks_iPhone.push_back("libmad.a");
	frameworks_iPhone.push_back("libmpeg2.a");
	frameworks_iPhone.push_back("libFLAC.a");
	frameworks_iPhone.push_back("libvorbisidec.a");
	frameworks_iPhone.push_back("OpenGLES.framework");

	int order = 0;
	for (ValueList::iterator framework = frameworks_iPhone.begin(); framework != frameworks_iPhone.end(); framework++) {
		std::string id = "Frameworks_" + *framework + "_iphone";
		std::string comment = *framework + " in Frameworks";

		ADD_SETTING_ORDER_NOVALUE(iPhone_files, getHash(id), comment, order++);
		ADD_BUILD_FILE(id, *framework, comment);
		ADD_FILE_REFERENCE(*framework, properties[*framework]);
	}

	framework_iPhone->properties["files"] = iPhone_files;

	_frameworksBuildPhase.add(framework_iPhone);

	//////////////////////////////////////////////////////////////////////////
	// ScummVM-OS X
	Object *framework_OSX = new Object(this, "PBXFrameworksBuildPhase_" + _targets[1], "PBXFrameworksBuildPhase", "PBXFrameworksBuildPhase", "", "Frameworks");

	framework_OSX->addProperty("buildActionMask", "2147483647", "", SettingsNoValue);
	framework_OSX->addProperty("runOnlyForDeploymentPostprocessing", "0", "", SettingsNoValue);

	// List of frameworks
	Property osx_files;
	osx_files.hasOrder = true;
	osx_files.flags = SettingsAsList;

	ValueList frameworks_osx;
	frameworks_osx.push_back("CoreFoundation.framework");
	frameworks_osx.push_back("Foundation.framework");
	frameworks_osx.push_back("AudioToolbox.framework");
	frameworks_osx.push_back("QuickTime.framework");
	frameworks_osx.push_back("CoreMIDI.framework");
	frameworks_osx.push_back("CoreAudio.framework");
	frameworks_osx.push_back("QuartzCore.framework");
	frameworks_osx.push_back("Carbon.framework");
	frameworks_osx.push_back("ApplicationServices.framework");
	frameworks_osx.push_back("IOKit.framework");
	frameworks_osx.push_back("Cocoa.framework");
	frameworks_osx.push_back("AudioUnit.framework");

	order = 0;
	for (ValueList::iterator framework = frameworks_osx.begin(); framework != frameworks_osx.end(); framework++) {
		std::string id = "Frameworks_" + *framework + "_osx";
		std::string comment = *framework + " in Frameworks";

		ADD_SETTING_ORDER_NOVALUE(osx_files, getHash(id), comment, order++);
		ADD_BUILD_FILE(id, *framework, comment);
		ADD_FILE_REFERENCE(*framework, properties[*framework]);
	}

	framework_OSX->properties["files"] = osx_files;

	_frameworksBuildPhase.add(framework_OSX);

	//////////////////////////////////////////////////////////////////////////
	// Simulator
	Object *framework_simulator = new Object(this, "PBXFrameworksBuildPhase_" + _targets[2], "PBXFrameworksBuildPhase", "PBXFrameworksBuildPhase", "", "Frameworks");

	framework_simulator->addProperty("buildActionMask", "2147483647", "", SettingsNoValue);
	framework_simulator->addProperty("runOnlyForDeploymentPostprocessing", "0", "", SettingsNoValue);

	// List of frameworks
	Property simulator_files;
	simulator_files.hasOrder = true;
	simulator_files.flags = SettingsAsList;

	ValueList frameworks_simulator;
	frameworks_simulator.push_back("CoreAudio.framework");
	frameworks_simulator.push_back("CoreFoundation.framework");
	frameworks_simulator.push_back("Foundation.framework");
	frameworks_simulator.push_back("UIKit.framework");
	frameworks_simulator.push_back("AudioToolbox.framework");
	frameworks_simulator.push_back("QuartzCore.framework");
	frameworks_simulator.push_back("OpenGLES.framework");

	order = 0;
	for (ValueList::iterator framework = frameworks_simulator.begin(); framework != frameworks_simulator.end(); framework++) {
		std::string id = "Frameworks_" + *framework + "_simulator";
		std::string comment = *framework + " in Frameworks";

		ADD_SETTING_ORDER_NOVALUE(simulator_files, getHash(id), comment, order++);
		ADD_BUILD_FILE(id, *framework, comment);
		ADD_FILE_REFERENCE(*framework, properties[*framework]);
	}

	framework_simulator->properties["files"] = simulator_files;

	_frameworksBuildPhase.add(framework_simulator);
}

void XCodeProvider::setupNativeTarget() {
	_nativeTarget.comment = "PBXNativeTarget";

	// Output native target section
	for (unsigned int i = 0; i < _targets.size(); i++) {
		Object *target = new Object(this, "PBXNativeTarget_" + _targets[i], "PBXNativeTarget", "PBXNativeTarget", "", _targets[i]);

		target->addProperty("buildConfigurationList", getHash("XCConfigurationList_" + _targets[i]), "Build configuration list for PBXNativeTarget \"" + _targets[i] + "\"", SettingsNoValue);

		Property buildPhases;
		buildPhases.hasOrder = true;
		buildPhases.flags = SettingsAsList;
		buildPhases.settings[getHash("PBXResourcesBuildPhase_" + _targets[i])] = Setting("", "Resources", SettingsNoValue, 0, 0);
		buildPhases.settings[getHash("PBXSourcesBuildPhase_" + _targets[i])] = Setting("", "Sources", SettingsNoValue, 0, 1);
		buildPhases.settings[getHash("PBXFrameworksBuildPhase_" + _targets[i])] = Setting("", "Frameworks", SettingsNoValue, 0, 2);
		target->properties["buildPhases"] = buildPhases;

		target->addProperty("buildRules", "", "", SettingsNoValue|SettingsAsList);

		target->addProperty("dependencies", "", "", SettingsNoValue|SettingsAsList);

		target->addProperty("name", _targets[i], "", SettingsNoValue|SettingsQuoteVariable);
		target->addProperty("productName", "residualvm", "", SettingsNoValue);
		target->addProperty("productReference", getHash("PBXFileReference_ResidualVM.app_" + _targets[i]), "ResidualVM.app", SettingsNoValue);
		target->addProperty("productType", "com.apple.product-type.application", "", SettingsNoValue|SettingsQuoteVariable);

		_nativeTarget.add(target);
	}
}

void XCodeProvider::setupProject() {
	_project.comment = "PBXProject";

	Object *project = new Object(this, "PBXProject", "PBXProject", "PBXProject", "", "Project object");

	project->addProperty("buildConfigurationList", getHash("XCConfigurationList_residualvm"), "Build configuration list for PBXProject \"residualvm\"", SettingsNoValue);
	project->addProperty("compatibilityVersion", "Xcode 3.2", "", SettingsNoValue|SettingsQuoteVariable);
	project->addProperty("developmentRegion", "English", "", SettingsNoValue);
	project->addProperty("hasScannedForEncodings", "1", "", SettingsNoValue);

	// List of known regions
	Property regions;
	regions.flags = SettingsAsList;
	ADD_SETTING_ORDER_NOVALUE(regions, "English", "", 0);
	ADD_SETTING_ORDER_NOVALUE(regions, "Japanese", "", 1);
	ADD_SETTING_ORDER_NOVALUE(regions, "French", "", 2);
	ADD_SETTING_ORDER_NOVALUE(regions, "German", "", 3);
	project->properties["knownRegions"] = regions;

	project->addProperty("mainGroup", getHash("PBXGroup_CustomTemplate"), "CustomTemplate", SettingsNoValue);
	project->addProperty("projectDirPath", "", "", SettingsNoValue|SettingsQuoteVariable);
	project->addProperty("projectRoot", "", "", SettingsNoValue|SettingsQuoteVariable);

	// List of targets
	Property targets;
	targets.flags = SettingsAsList;
	targets.settings[getHash("PBXNativeTarget_" + _targets[0])] = Setting("", _targets[0], SettingsNoValue, 0, 0);
	targets.settings[getHash("PBXNativeTarget_" + _targets[1])] = Setting("", _targets[1], SettingsNoValue, 0, 1);
	targets.settings[getHash("PBXNativeTarget_" + _targets[2])] = Setting("", _targets[2], SettingsNoValue, 0, 2);
	project->properties["targets"] = targets;

	_project.add(project);
}

void XCodeProvider::setupResourcesBuildPhase() {
	_resourcesBuildPhase.comment = "PBXResourcesBuildPhase";

	// Setup resource file properties
	std::map<std::string, FileProperty> properties;
	properties["modern.zip"]       = FileProperty("archive.zip", "", "modern.zip", "\"<group>\"");

	properties["kyra.dat"]         = FileProperty("file", "", "kyra.dat", "\"<group>\"");
	properties["lure.dat"]         = FileProperty("file", "", "lure.dat", "\"<group>\"");
	properties["queen.tbl"]        = FileProperty("file", "", "queen.tbl", "\"<group>\"");
	properties["sky.cpt"]          = FileProperty("file", "", "sky.cpt", "\"<group>\"");
	properties["drascula.dat"]     = FileProperty("file", "", "drascula.dat", "\"<group>\"");
	properties["hugo.dat"]         = FileProperty("file", "", "hugo.dat", "\"<group>\"");
	properties["teenagent.dat"]    = FileProperty("file", "", "teenagent.dat", "\"<group>\"");
	properties["toon.dat"]         = FileProperty("file", "", "toon.dat", "\"<group>\"");

	properties["Default.png"]      = FileProperty("image.png", "", "Default.png", "\"<group>\"");
	properties["icon.png"]         = FileProperty("image.png", "", "icon.png", "\"<group>\"");
	properties["icon-72.png"]      = FileProperty("image.png", "", "icon-72.png", "\"<group>\"");
	properties["icon4.png"]        = FileProperty("image.png", "", "icon4.png", "\"<group>\"");

	// Same as for containers: a rule for each native target
	for (unsigned int i = 0; i < _targets.size(); i++) {
		Object *resource = new Object(this, "PBXResourcesBuildPhase_" + _targets[i], "PBXResourcesBuildPhase", "PBXResourcesBuildPhase", "", "Resources");

		resource->addProperty("buildActionMask", "2147483647", "", SettingsNoValue);

		// Add default files
		Property files;
		files.hasOrder = true;
		files.flags = SettingsAsList;

		ValueList files_list;
		files_list.push_back("modern.zip");
		files_list.push_back("kyra.dat");
		files_list.push_back("lure.dat");
		files_list.push_back("queen.tbl");
		files_list.push_back("sky.cpt");
		files_list.push_back("Default.png");
		files_list.push_back("icon.png");
		files_list.push_back("icon-72.png");
		files_list.push_back("icon4.png");
		files_list.push_back("drascula.dat");
		files_list.push_back("hugo.dat");
		files_list.push_back("teenagent.dat");
		files_list.push_back("toon.dat");

		int order = 0;
		for (ValueList::iterator file = files_list.begin(); file != files_list.end(); file++) {
			std::string id = "PBXResources_" + *file;
			std::string comment = *file + " in Resources";

			ADD_SETTING_ORDER_NOVALUE(files, getHash(id), comment, order++);
			// TODO Fix crash when adding build file for data
			//ADD_BUILD_FILE(id, *file, comment);
			ADD_FILE_REFERENCE(*file, properties[*file]);
		}

		// Add custom files depending on the target
		if (_targets[i] == "ResidualVM-OS X") {
			files.settings[getHash("PBXResources_residualvm.icns")] = Setting("", "residualvm.icns in Resources", SettingsNoValue, 0, 6);

			// Remove 2 iphone icon files
			files.settings.erase(getHash("PBXResources_Default.png"));
			files.settings.erase(getHash("PBXResources_icon.png"));
		}

		resource->properties["files"] = files;

		resource->addProperty("runOnlyForDeploymentPostprocessing", "0", "", SettingsNoValue);

		_resourcesBuildPhase.add(resource);
	}
}

void XCodeProvider::setupSourcesBuildPhase() {
	// TODO
}

// Setup all build configurations
void XCodeProvider::setupBuildConfiguration() {

	_buildConfiguration.comment = "XCBuildConfiguration";
	_buildConfiguration.flags = SettingsAsList;

	///****************************************
	// * iPhone
	// ****************************************/

	// Debug
	Object *iPhone_Debug_Object = new Object(this, "XCBuildConfiguration_ResidualVM-iPhone_Debug", _targets[0] /* ResidualVM-iPhone */, "XCBuildConfiguration", "PBXNativeTarget", "Debug");
	Property iPhone_Debug;
	ADD_SETTING_QUOTE(iPhone_Debug, "ARCHS", "$(ARCHS_UNIVERSAL_IPHONE_OS)");
	ADD_SETTING_QUOTE(iPhone_Debug, "CODE_SIGN_IDENTITY", "iPhone Developer");
	ADD_SETTING_QUOTE_VAR(iPhone_Debug, "CODE_SIGN_IDENTITY[sdk=iphoneos*]", "iPhone Developer");
	ADD_SETTING(iPhone_Debug, "COMPRESS_PNG_FILES", "NO");
	ADD_SETTING(iPhone_Debug, "COPY_PHASE_STRIP", "NO");
	ADD_SETTING_QUOTE(iPhone_Debug, "DEBUG_INFORMATION_FORMAT", "dwarf-with-dsym");
	ValueList iPhone_FrameworkSearchPaths;
	iPhone_FrameworkSearchPaths.push_back("$(inherited)");
	iPhone_FrameworkSearchPaths.push_back("\"$(SDKROOT)$(SYSTEM_LIBRARY_DIR)/PrivateFrameworks\"");
	ADD_SETTING_LIST(iPhone_Debug, "FRAMEWORK_SEARCH_PATHS", iPhone_FrameworkSearchPaths, SettingsAsList, 5);
	ADD_SETTING(iPhone_Debug, "GCC_DYNAMIC_NO_PIC", "NO");
	ADD_SETTING(iPhone_Debug, "GCC_ENABLE_CPP_EXCEPTIONS", "NO");
	ADD_SETTING(iPhone_Debug, "GCC_ENABLE_FIX_AND_CONTINUE", "NO");
	ADD_SETTING(iPhone_Debug, "GCC_OPTIMIZATION_LEVEL", "0");
	ADD_SETTING(iPhone_Debug, "GCC_PRECOMPILE_PREFIX_HEADER", "NO");
	ADD_SETTING_QUOTE(iPhone_Debug, "GCC_PREFIX_HEADER", "");
	ADD_SETTING(iPhone_Debug, "GCC_THUMB_SUPPORT", "NO");
	ADD_SETTING(iPhone_Debug, "GCC_UNROLL_LOOPS", "YES");
	ValueList iPhone_HeaderSearchPaths;
	iPhone_HeaderSearchPaths.push_back("../../engines/");
	iPhone_HeaderSearchPaths.push_back("../../");
	iPhone_HeaderSearchPaths.push_back("include/");
	ADD_SETTING_LIST(iPhone_Debug, "HEADER_SEARCH_PATHS", iPhone_HeaderSearchPaths, SettingsAsList|SettingsNoQuote, 5);
	ADD_SETTING(iPhone_Debug, "INFOPLIST_FILE", "Info.plist");
	ValueList iPhone_LibPaths;
	iPhone_LibPaths.push_back("$(inherited)");
	iPhone_LibPaths.push_back("\"$(SRCROOT)/lib\"");
	ADD_SETTING_LIST(iPhone_Debug, "LIBRARY_SEARCH_PATHS", iPhone_LibPaths, SettingsAsList, 5);
	ADD_SETTING(iPhone_Debug, "ONLY_ACTIVE_ARCH", "YES");
	ADD_SETTING(iPhone_Debug, "PREBINDING", "NO");
	ADD_SETTING(iPhone_Debug, "PRODUCT_NAME", "ResidualVM");
	ADD_SETTING_QUOTE(iPhone_Debug, "PROVISIONING_PROFILE", "EF590570-5FAC-4346-9071-D609DE2B28D8");
	ADD_SETTING_QUOTE_VAR(iPhone_Debug, "PROVISIONING_PROFILE[sdk=iphoneos*]", "");
	ADD_SETTING(iPhone_Debug, "SDKROOT", "iphoneos4.0");
	ADD_SETTING_QUOTE(iPhone_Debug, "TARGETED_DEVICE_FAMILY", "1,2");

	iPhone_Debug_Object->addProperty("name", "Debug", "", SettingsNoValue);
	iPhone_Debug_Object->properties["buildSettings"] = iPhone_Debug;

	// Release
	Object *iPhone_Release_Object = new Object(this, "XCBuildConfiguration_ResidualVM-iPhone_Release", _targets[0] /* ResidualVM-iPhone */, "XCBuildConfiguration", "PBXNativeTarget", "Release");
	Property iPhone_Release(iPhone_Debug);
	ADD_SETTING(iPhone_Release, "GCC_OPTIMIZATION_LEVEL", "3");
	ADD_SETTING(iPhone_Release, "COPY_PHASE_STRIP", "YES");
	REMOVE_SETTING(iPhone_Release, "GCC_DYNAMIC_NO_PIC");
	ADD_SETTING(iPhone_Release, "WRAPPER_EXTENSION", "app");

	iPhone_Release_Object->addProperty("name", "Release", "", SettingsNoValue);
	iPhone_Release_Object->properties["buildSettings"] = iPhone_Release;

	_buildConfiguration.add(iPhone_Debug_Object);
	_buildConfiguration.add(iPhone_Release_Object);

	/****************************************
	 * scummvm
	 ****************************************/

	// Debug
	Object *residualvm_Debug_Object = new Object(this, "XCBuildConfiguration_residualvm_Debug", "residualvm", "XCBuildConfiguration", "PBXProject", "Debug");
	Property residualvm_Debug;
	ADD_SETTING(residualvm_Debug, "ALWAYS_SEARCH_USER_PATHS", "NO");
	ADD_SETTING_QUOTE(residualvm_Debug, "ARCHS", "$(ARCHS_STANDARD_32_BIT)");
	ADD_SETTING_QUOTE(residualvm_Debug, "CODE_SIGN_IDENTITY", "Don't Code Sign");
	ADD_SETTING_QUOTE_VAR(residualvm_Debug, "CODE_SIGN_IDENTITY[sdk=iphoneos*]", "Don't Code Sign");
	ADD_SETTING_QUOTE(residualvm_Debug, "FRAMEWORK_SEARCH_PATHS", "");
	ADD_SETTING(residualvm_Debug, "GCC_C_LANGUAGE_STANDARD", "c99");
	ADD_SETTING(residualvm_Debug, "GCC_ENABLE_CPP_EXCEPTIONS", "NO");
	ADD_SETTING(residualvm_Debug, "GCC_ENABLE_CPP_RTTI", "NO");
	ADD_SETTING(residualvm_Debug, "GCC_INPUT_FILETYPE", "automatic");
	ADD_SETTING(residualvm_Debug, "GCC_OPTIMIZATION_LEVEL", "0");
	ValueList residualvm_defines(_defines);
	ADD_DEFINE(residualvm_defines, "IPHONE");
	ADD_DEFINE(residualvm_defines, "XCODE");
	ADD_DEFINE(residualvm_defines, "IPHONE_OFFICIAL");
	ADD_SETTING_LIST(residualvm_Debug, "GCC_PREPROCESSOR_DEFINITIONS", residualvm_defines, SettingsNoQuote|SettingsAsList, 5);
	ADD_SETTING(residualvm_Debug, "GCC_THUMB_SUPPORT", "NO");
	ADD_SETTING(residualvm_Debug, "GCC_USE_GCC3_PFE_SUPPORT", "NO");
	ADD_SETTING(residualvm_Debug, "GCC_WARN_ABOUT_RETURN_TYPE", "YES");
	ADD_SETTING(residualvm_Debug, "GCC_WARN_UNUSED_VARIABLE", "YES");
	ValueList residualvm_HeaderPaths;
	residualvm_HeaderPaths.push_back("include/");
	residualvm_HeaderPaths.push_back("../../engines/");
	residualvm_HeaderPaths.push_back("../../");
	ADD_SETTING_LIST(residualvm_Debug, "HEADER_SEARCH_PATHS", residualvm_HeaderPaths, SettingsNoQuote|SettingsAsList, 5);
	ADD_SETTING_QUOTE(residualvm_Debug, "LIBRARY_SEARCH_PATHS", "");
	ADD_SETTING(residualvm_Debug, "ONLY_ACTIVE_ARCH", "YES");
	ADD_SETTING_QUOTE(residualvm_Debug, "OTHER_CFLAGS", "");
	ADD_SETTING_QUOTE(residualvm_Debug, "OTHER_LDFLAGS", "-lz");
	ADD_SETTING(residualvm_Debug, "PREBINDING", "NO");
	ADD_SETTING(residualvm_Debug, "SDKROOT", "macosx10.6");

	residualvm_Debug_Object->addProperty("name", "Debug", "", SettingsNoValue);
	residualvm_Debug_Object->properties["buildSettings"] = residualvm_Debug;

	// Release
	Object *residualvm_Release_Object = new Object(this, "XCBuildConfiguration_residualvm_Release", "residualvm", "XCBuildConfiguration", "PBXProject", "Release");
	Property residualvm_Release(residualvm_Debug);
	REMOVE_SETTING(residualvm_Release, "GCC_C_LANGUAGE_STANDARD");       // Not sure why we remove that, or any of the other warnings
	REMOVE_SETTING(residualvm_Release, "GCC_WARN_ABOUT_RETURN_TYPE");
	REMOVE_SETTING(residualvm_Release, "GCC_WARN_UNUSED_VARIABLE");
	REMOVE_SETTING(residualvm_Release, "ONLY_ACTIVE_ARCH");

	residualvm_Release_Object->addProperty("name", "Release", "", SettingsNoValue);
	residualvm_Release_Object->properties["buildSettings"] = residualvm_Release;

	_buildConfiguration.add(residualvm_Debug_Object);
	_buildConfiguration.add(residualvm_Release_Object);

	/****************************************
	 * ScummVM-OS X
	 ****************************************/

	// Debug
	Object *residualvmOSX_Debug_Object = new Object(this, "XCBuildConfiguration_ResidualVM-OSX_Debug", _targets[1] /* ResidualVM-OS X */, "XCBuildConfiguration", "PBXNativeTarget", "Debug");
	Property residualvmOSX_Debug;
	ADD_SETTING_QUOTE(residualvmOSX_Debug, "ARCHS", "$(NATIVE_ARCH)");
	ADD_SETTING(residualvmOSX_Debug, "COMPRESS_PNG_FILES", "NO");
	ADD_SETTING(residualvmOSX_Debug, "COPY_PHASE_STRIP", "NO");
	ADD_SETTING_QUOTE(residualvmOSX_Debug, "DEBUG_INFORMATION_FORMAT", "dwarf-with-dsym");
	ADD_SETTING_QUOTE(residualvmOSX_Debug, "FRAMEWORK_SEARCH_PATHS", "");
	ADD_SETTING(residualvmOSX_Debug, "GCC_C_LANGUAGE_STANDARD", "c99");
	ADD_SETTING(residualvmOSX_Debug, "GCC_ENABLE_CPP_EXCEPTIONS", "NO");
	ADD_SETTING(residualvmOSX_Debug, "GCC_ENABLE_CPP_RTTI", "NO");
	ADD_SETTING(residualvmOSX_Debug, "GCC_DYNAMIC_NO_PIC", "NO");
	ADD_SETTING(residualvmOSX_Debug, "GCC_ENABLE_FIX_AND_CONTINUE", "NO");
	ADD_SETTING(residualvmOSX_Debug, "GCC_OPTIMIZATION_LEVEL", "0");
	ADD_SETTING(residualvmOSX_Debug, "GCC_PRECOMPILE_PREFIX_HEADER", "NO");
	ADD_SETTING_QUOTE(residualvmOSX_Debug, "GCC_PREFIX_HEADER", "");
	ValueList residualvmOSX_defines(_defines);
	ADD_DEFINE(residualvmOSX_defines, "SDL_BACKEND");
	ADD_DEFINE(residualvmOSX_defines, "MACOSX");
	ADD_SETTING_LIST(residualvmOSX_Debug, "GCC_PREPROCESSOR_DEFINITIONS", residualvmOSX_defines, SettingsNoQuote|SettingsAsList, 5);
	ADD_SETTING_QUOTE(residualvmOSX_Debug, "GCC_VERSION", "");
	ValueList residualvmOSX_HeaderPaths;
	residualvmOSX_HeaderPaths.push_back("/opt/local/include/SDL");
	residualvmOSX_HeaderPaths.push_back("/opt/local/include");
	residualvmOSX_HeaderPaths.push_back("include/");
	residualvmOSX_HeaderPaths.push_back("../../engines/");
	residualvmOSX_HeaderPaths.push_back("../../");
	ADD_SETTING_LIST(residualvmOSX_Debug, "HEADER_SEARCH_PATHS", residualvmOSX_HeaderPaths, SettingsNoQuote|SettingsAsList, 5);
	ADD_SETTING_QUOTE(residualvmOSX_Debug, "INFOPLIST_FILE", "$(SRCROOT)/../macosx/Info.plist");
	ValueList residualvmOSX_LibPaths;
	residualvmOSX_LibPaths.push_back("/sw/lib");
	residualvmOSX_LibPaths.push_back("/opt/local/lib");
	residualvmOSX_LibPaths.push_back("\"$(inherited)\"");
	residualvmOSX_LibPaths.push_back("\"\\\\\\\"$(SRCROOT)/lib\\\\\\\"\"");  // mmmh, all those slashes, it's almost Christmas \o/
	ADD_SETTING_LIST(residualvmOSX_Debug, "LIBRARY_SEARCH_PATHS", residualvmOSX_LibPaths, SettingsNoQuote|SettingsAsList, 5);
	ADD_SETTING_QUOTE(residualvmOSX_Debug, "OTHER_CFLAGS", "");
	ValueList residualvmOSX_LdFlags;
	residualvmOSX_LdFlags.push_back("-lSDLmain");
	residualvmOSX_LdFlags.push_back("-logg");
	residualvmOSX_LdFlags.push_back("-lvorbisfile");
	residualvmOSX_LdFlags.push_back("-lvorbis");
	residualvmOSX_LdFlags.push_back("-lmad");
	residualvmOSX_LdFlags.push_back("-lFLAC");
	residualvmOSX_LdFlags.push_back("-lSDL");
	residualvmOSX_LdFlags.push_back("-lz");
	ADD_SETTING_LIST(residualvmOSX_Debug, "OTHER_LDFLAGS", residualvmOSX_LdFlags, SettingsAsList, 5);
	ADD_SETTING(residualvmOSX_Debug, "PREBINDING", "NO");
	ADD_SETTING(residualvmOSX_Debug, "PRODUCT_NAME", "ResidualVM");

	residualvmOSX_Debug_Object->addProperty("name", "Debug", "", SettingsNoValue);
	residualvmOSX_Debug_Object->properties["buildSettings"] = residualvmOSX_Debug;

	// Release
	Object *residualvmOSX_Release_Object = new Object(this, "XCBuildConfiguration_ResidualVMOSX_Release", _targets[1] /* ResidualVM-OS X */, "XCBuildConfiguration", "PBXNativeTarget", "Release");
	Property residualvmOSX_Release(residualvmOSX_Debug);
	ADD_SETTING(residualvmOSX_Release, "COPY_PHASE_STRIP", "YES");
	REMOVE_SETTING(residualvmOSX_Release, "GCC_DYNAMIC_NO_PIC");
	REMOVE_SETTING(residualvmOSX_Release, "GCC_OPTIMIZATION_LEVEL");
	ADD_SETTING(residualvmOSX_Release, "WRAPPER_EXTENSION", "app");

	residualvmOSX_Release_Object->addProperty("name", "Release", "", SettingsNoValue);
	residualvmOSX_Release_Object->properties["buildSettings"] = residualvmOSX_Release;

	_buildConfiguration.add(residualvmOSX_Debug_Object);
	_buildConfiguration.add(residualvmOSX_Release_Object);

	/****************************************
	 * ResidualVM-Simulator
	 ****************************************/

	// Debug
	Object *residualvmSimulator_Debug_Object = new Object(this, "XCBuildConfiguration_ResidualVM-Simulator_Debug", _targets[2] /* ResidualVM-Simulator */, "XCBuildConfiguration", "PBXNativeTarget", "Debug");
	Property residualvmSimulator_Debug(iPhone_Debug);
	ADD_SETTING_QUOTE(residualvmSimulator_Debug, "FRAMEWORK_SEARCH_PATHS", "$(inherited)");
	ADD_SETTING_LIST(residualvmSimulator_Debug, "GCC_PREPROCESSOR_DEFINITIONS", residualvm_defines, SettingsNoQuote|SettingsAsList, 5);
	ADD_SETTING(residualvmSimulator_Debug, "SDKROOT", "iphonesimulator3.2");
	REMOVE_SETTING(residualvmSimulator_Debug, "TARGETED_DEVICE_FAMILY");

	residualvmSimulator_Debug_Object->addProperty("name", "Debug", "", SettingsNoValue);
	residualvmSimulator_Debug_Object->properties["buildSettings"] = residualvmSimulator_Debug;

	// Release
	Object *residualvmSimulator_Release_Object = new Object(this, "XCBuildConfiguration_ResidualVM-Simulator_Release", _targets[2] /* ResidualVM-Simulator */, "XCBuildConfiguration", "PBXNativeTarget", "Release");
	Property residualvmSimulator_Release(residualvmSimulator_Debug);
	ADD_SETTING(residualvmSimulator_Release, "COPY_PHASE_STRIP", "YES");
	REMOVE_SETTING(residualvmSimulator_Release, "GCC_DYNAMIC_NO_PIC");
	ADD_SETTING(residualvmSimulator_Release, "WRAPPER_EXTENSION", "app");

	residualvmSimulator_Release_Object->addProperty("name", "Release", "", SettingsNoValue);
	residualvmSimulator_Release_Object->properties["buildSettings"] = residualvmSimulator_Release;

	_buildConfiguration.add(residualvmSimulator_Debug_Object);
	_buildConfiguration.add(residualvmSimulator_Release_Object);

	//////////////////////////////////////////////////////////////////////////
	// Configuration List
	_configurationList.comment = "XCConfigurationList";
	_configurationList.flags = SettingsAsList;

	// Warning: This assumes we have all configurations with a Debug & Release pair
	for (std::vector<Object *>::iterator config = _buildConfiguration.objects.begin(); config != _buildConfiguration.objects.end(); config++) {

		Object *configList = new Object(this, "XCConfigurationList_" + (*config)->name, (*config)->name, "XCConfigurationList", "", "Build configuration list for " +  (*config)->refType + " \"" + (*config)->name + "\"");

		Property buildConfigs;
		buildConfigs.flags = SettingsAsList;

		buildConfigs.settings[getHash((*config)->id)] = Setting("", "Debug", SettingsNoValue, 0, 0);
		buildConfigs.settings[getHash((*(++config))->id)] = Setting("", "Release", SettingsNoValue, 0, 1);

		configList->properties["buildConfigurations"] = buildConfigs;

		configList->addProperty("defaultConfigurationIsVisible", "0", "", SettingsNoValue);
		configList->addProperty("defaultConfigurationName", "Release", "", SettingsNoValue);

		_configurationList.add(configList);
	}
}

//////////////////////////////////////////////////////////////////////////
// Misc
//////////////////////////////////////////////////////////////////////////

// Setup global defines
void XCodeProvider::setupDefines(const BuildSetup &setup) {

	for (StringList::const_iterator i = setup.defines.begin(); i != setup.defines.end(); ++i) {
		if (*i == "HAVE_NASM")	// Not supported on Mac (TODO: change how it's handled in main class or add it only in MSVC/CodeBlocks providers?)
			continue;

		ADD_DEFINE(_defines, *i);
	}
	// Add special defines for Mac support
	ADD_DEFINE(_defines, "CONFIG_H");
	ADD_DEFINE(_defines, "SCUMM_NEED_ALIGNMENT");
	ADD_DEFINE(_defines, "SCUMM_LITTLE_ENDIAN");
	ADD_DEFINE(_defines, "UNIX");
	ADD_DEFINE(_defines, "SCUMMVM");
	ADD_DEFINE(_defines, "USE_TREMOR");
}

//////////////////////////////////////////////////////////////////////////
// Object hash
//////////////////////////////////////////////////////////////////////////

// TODO use md5 to compute a file hash (and fall back to standard key generation if not passed a file)
std::string XCodeProvider::getHash(std::string key) {

#if DEBUG_XCODE_HASH
	return key;
#else
	// Check to see if the key is already in the dictionary
	std::map<std::string, std::string>::iterator hashIterator = _hashDictionnary.find(key);
	if (hashIterator != _hashDictionnary.end())
		return hashIterator->second;

	// Generate a new key from the file hash and insert it into the dictionary
	std::string hash = newHash();
	_hashDictionnary[key] = hash;

	return hash;
#endif
}

bool isSeparator (char s) { return (s == '-'); }

std::string XCodeProvider::newHash() const {
	std::string hash = createUUID();

	// Remove { and - from UUID and resize to 96-bits uppercase hex string
	hash.erase(remove_if(hash.begin(), hash.end(), isSeparator), hash.end());

	hash.resize(24);
	std::transform(hash.begin(), hash.end(), hash.begin(), toupper);

	return hash;
}

//////////////////////////////////////////////////////////////////////////
// Output
//////////////////////////////////////////////////////////////////////////

std::string replace(std::string input, const std::string find, std::string replaceStr) {
	std::string::size_type pos = 0;
	std::string::size_type findLen = find.length();
	std::string::size_type replaceLen = replaceStr.length();

	if (findLen == 0 )
		return input;

	for (;(pos = input.find(find, pos)) != std::string::npos;) {
		input.replace(pos, findLen, replaceStr);
		pos += replaceLen;
	}

	return input;
}

std::string XCodeProvider::writeProperty(const std::string &variable, Property &prop, int flags) const {
	std::string output;

	output += (flags & SettingsSingleItem ? "" : "\t\t\t") + variable + " = ";

	if (prop.settings.size() > 1 || (prop.flags & SettingsSingleItem))
		output += (prop.flags & SettingsAsList) ? "(\n" : "{\n";

	OrderedSettingList settings = prop.getOrderedSettingList();
	for (OrderedSettingList::const_iterator setting = settings.begin(); setting != settings.end(); ++setting) {
		if (settings.size() > 1 || (prop.flags & SettingsSingleItem))
			output += (flags & SettingsSingleItem ? " " : "\t\t\t\t");

		output += writeSetting((*setting).first, (*setting).second);

		if ((prop.flags & SettingsAsList) && prop.settings.size() > 1) {
			output += (prop.settings.size() > 0) ? ",\n" : "\n";
		} else {
			output += ";";
			output += (flags & SettingsSingleItem ? " " : "\n");
		}
	}

	if (prop.settings.size() > 1 || (prop.flags & SettingsSingleItem))
		output += (prop.flags & SettingsAsList) ? "\t\t\t);\n" : "\t\t\t};\n";

	return output;
}

std::string XCodeProvider::writeSetting(const std::string &variable, std::string value, std::string comment, int flags, int indent) const {
	return writeSetting(variable, Setting(value, comment, flags, indent));
}
// Heavily modified (not in a good way) function, imported from QMake XCode project generator (licensed under the QT license)
std::string XCodeProvider::writeSetting(const std::string &variable, const Setting &setting) const {
	std::string output;
	const std::string quote = (setting.flags & SettingsNoQuote) ? "" : "\"";
	const std::string escape_quote = quote.empty() ? "" : "\\" + quote;
	std::string newline = "\n";

	// Get indent level
	for (int i = 0; i < setting.indent; ++i)
		newline += "\t";

	// Setup variable
	std::string var = (setting.flags & SettingsQuoteVariable) ? "\"" + variable + "\"" : variable;

	// Output a list
	if (setting.flags & SettingsAsList) {

		output += var + ((setting.flags & SettingsNoValue) ? "(" : " = (") + newline;

		for (unsigned int i = 0, count = 0; i < setting.entries.size(); ++i) {

			std::string value = setting.entries.at(i).value;
			if(!value.empty()) {
				if (count++ > 0)
					output += "," + newline;

				output += quote + replace(value, quote, escape_quote) + quote;

				std::string comment = setting.entries.at(i).comment;
				if (!comment.empty())
					output += " /* " + comment + " */";
			}

		}
		// Add closing ")" on new line
		newline.resize(newline.size() - 1);
		output += (setting.flags & SettingsNoValue) ? "\t\t\t)" : "," + newline + ")";
	} else {
		output += var;

		output += (setting.flags & SettingsNoValue) ? "" : " = " + quote;

		for(unsigned int i = 0; i < setting.entries.size(); ++i) {
			std::string value = setting.entries.at(i).value;
			if(i)
				output += " ";
			output += value;

			std::string comment = setting.entries.at(i).comment;
			if (!comment.empty())
				output += " /* " + comment + " */";
		}

		output += (setting.flags & SettingsNoValue) ? "" : quote;
	}
	return output;
}

} // End of CreateProjectTool namespace
