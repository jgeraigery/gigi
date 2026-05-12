// Define any undefined macros
// MCP variants are MCP only
#ifndef FUNCTION_BEGIN
    #define FUNCTION_BEGIN(NAME, DESCRIPTION)
#endif

#ifndef PARAMETER
    #define PARAMETER(TYPE, NAME, DESCRIPTION)
#endif

#ifndef FUNCTION_END
    #define FUNCTION_END()
#endif

#ifndef FUNCTION_BEGIN_MCP
    #define FUNCTION_BEGIN_MCP(NAME, DESCRIPTION)
#endif

#ifndef PARAMETER_MCP
    #define PARAMETER_MCP(TYPE, NAME, DESCRIPTION)
#endif

#ifndef FUNCTION_END_MCP
    #define FUNCTION_END_MCP()
#endif

// =================================== SHARED ===================================
// The functions that are exposed to both python and MCP server

FUNCTION_BEGIN(Exit, "Closes the viewer, with an optional exit code")
    PARAMETER(int, exitCode, "The exit code returned by the viewer")
FUNCTION_END()

FUNCTION_BEGIN(LoadGG, "Loads a .gg file")
    PARAMETER(std::string, fileName, "The file to load. Absolute or relative path.")
FUNCTION_END()

FUNCTION_BEGIN(SetHideUI, "Sets the HideUI flag.")
    PARAMETER(bool, hide, "")
FUNCTION_END()

FUNCTION_BEGIN(SetVSync, "Sets the vsync flag.")
    PARAMETER(bool, vsync, "")
FUNCTION_END()

FUNCTION_BEGIN(SetSyncInterval, "IDXGISwapChain::Present() parameter: Synchronize presentation after the nth vertical blank.")
    PARAMETER(int, interval, "")
FUNCTION_END()

FUNCTION_BEGIN(SetStablePowerState, "Sets the stable power state flag.")
    PARAMETER(bool, stablePower, "")
FUNCTION_END()

FUNCTION_BEGIN(SetProfilingMode, "Turns on or off profiling mode. The viewer does extra resource copies and readback as part of regular operation. Profiling mode reduces that.")
    PARAMETER(bool, enabled, "")
FUNCTION_END()

FUNCTION_BEGIN(SetVariable, "Sets the value of a variable. String for variable name, string for variable value.")
    PARAMETER(std::string, name, "")
    PARAMETER(std::string, value, "")
FUNCTION_END()

FUNCTION_BEGIN(GetVariable, "Gets the value of a variable. String for variable name, returns variable value as a string.")
    PARAMETER(std::string, name, "")
FUNCTION_END()

FUNCTION_BEGIN(DisableGGUserSave, "When the gg file is changed next or the application is closed, this will prevent the gguser file from being saved.")
    PARAMETER(bool, disable, "")
FUNCTION_END()

FUNCTION_BEGIN(SetWantReadback, "Declare that you want to read back a resource, or specify that you no longer want it. After you have set the resources you want to read back, you must RunTechnique() at least once, then call WaitOnGPU() before Readback() will give you the data.")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(bool, wantReadback, "")
    PARAMETER(bool, autoClear, "Pass false for this, if using the MCP or telnet.") // if true, clears this state when the script (aka the single command!) is done running, which makes no sense in the MCP context.
FUNCTION_END()

FUNCTION_BEGIN(ReadbackBase64, "Reads back a resource. Before you can call this, you must call SetWantReadback(), you must RunTechnique() at least once, then call WaitOnGPU(). After that, you can call this function to get the data.")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(int, arrayIndex, "If an array texture type, the index of the array. Else use 0.")
    PARAMETER(int, mipIndex, "If the texture has mips, the mip index. Else use 0.")
FUNCTION_END()

FUNCTION_BEGIN(SaveAsPNG, "Saves a texture. Host.SetWantReadback must have been called prior and the technique must have been executed at least once.")
    PARAMETER(std::string, fileName, "")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(int, arrayIndex, "pass 0 for non array textures")
    PARAMETER(int, mipIndex, "pass 0 for textures without mips")
FUNCTION_END()

FUNCTION_BEGIN(SaveAsDDS_BC4, "Saves a texture. Host.SetWantReadback must have been called prior and the technique must have been executed at least once.")
    PARAMETER(std::string, fileName, "")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(int, arrayIndex, "pass 0 for non array textures")
    PARAMETER(int, mipIndex, "pass 0 for textures without mips")
    PARAMETER(bool, signedData, "Whether it should be signed pixel data or not")
FUNCTION_END()

FUNCTION_BEGIN(SaveAsDDS_BC5, "Saves a texture. Host.SetWantReadback must have been called prior and the technique must have been executed at least once.")
    PARAMETER(std::string, fileName, "")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(int, arrayIndex, "pass 0 for non array textures")
    PARAMETER(int, mipIndex, "pass 0 for textures without mips")
    PARAMETER(bool, signedData, "Whether it should be signed pixel data or not")
FUNCTION_END()

FUNCTION_BEGIN(SaveAsDDS_BC6, "Saves a texture. Host.SetWantReadback must have been called prior and the technique must have been executed at least once.")
    PARAMETER(std::string, fileName, "")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(int, arrayIndex, "pass 0 for non array textures")
    PARAMETER(int, mipIndex, "pass 0 for textures without mips")
    PARAMETER(bool, signedData, "Whether it should be signed pixel data or not")
FUNCTION_END()

FUNCTION_BEGIN(SaveAsDDS_BC7, "Saves a texture. Host.SetWantReadback must have been called prior and the technique must have been executed at least once.")
    PARAMETER(std::string, fileName, "")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(int, arrayIndex, "pass 0 for non array textures")
    PARAMETER(int, mipIndex, "pass 0 for textures without mips")
    PARAMETER(bool, sRGB, "Whether it should be sRGB pixel data or not")
FUNCTION_END()

FUNCTION_BEGIN(SaveAsEXR, "Saves a texture. Host.SetWantReadback must have been called prior and the technique must have been executed at least once.")
    PARAMETER(std::string, fileName, "")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(int, arrayIndex, "pass 0 for non array textures")
    PARAMETER(int, mipIndex, "pass 0 for textures without mips")
FUNCTION_END()

FUNCTION_BEGIN(SaveAsHDR, "Saves a texture. Host.SetWantReadback must have been called prior and the technique must have been executed at least once.")
    PARAMETER(std::string, fileName, "")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(int, arrayIndex, "pass 0 for non array textures")
    PARAMETER(int, mipIndex, "pass 0 for textures without mips")
FUNCTION_END()

FUNCTION_BEGIN(SaveAsCSV, "Saves a texture. Host.SetWantReadback must have been called prior and the technique must have been executed at least once.")
    PARAMETER(std::string, fileName, "")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(int, arrayIndex, "pass 0 for non array textures")
    PARAMETER(int, mipIndex, "pass 0 for textures without mips")
FUNCTION_END()

FUNCTION_BEGIN(SaveAsBinary, "Saves a texture. Host.SetWantReadback must have been called prior and the technique must have been executed at least once.")
    PARAMETER(std::string, fileName, "")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(int, arrayIndex, "pass 0 for non array textures")
    PARAMETER(int, mipIndex, "pass 0 for textures without mips")
FUNCTION_END()

FUNCTION_BEGIN(RunTechnique, "Runs (aka Executes) the technique N times. ")
    PARAMETER(int, N, "The number of times to run the technique.")
FUNCTION_END()

FUNCTION_BEGIN(Log, "Writes a message to the log.")
    PARAMETER(std::string, severity, "Options: info, warn, error")
    PARAMETER(std::string, message, "")
FUNCTION_END()

FUNCTION_BEGIN(Print, "Writes a message to the log as info")
    PARAMETER(std::string, message, "")
FUNCTION_END()

FUNCTION_BEGIN(Warn, "Writes a message to the log as warn")
    PARAMETER(std::string, message, "")
FUNCTION_END()

FUNCTION_BEGIN(Error, "Writes a message to the log as error")
    PARAMETER(std::string, message, "")
FUNCTION_END()

FUNCTION_BEGIN(SetFrameIndex, "Sets the viewer internal frame index. Useful for things like camera jitter patterns which use the frame index, or shaders which use the built in viewer frame index.")
    PARAMETER(int, index, "")
FUNCTION_END()

FUNCTION_BEGIN(WaitOnGPU, "Waits until all GPU work in flight is finished. Useful for ensuring that it's safe to do readback.")
FUNCTION_END()

FUNCTION_BEGIN(Pause, "Can pause or unpause viewer execution. Does not affect RunTechnique(), but useful for pausing then exiting the script to return user control at a specific frame to investigate.")
    PARAMETER(bool, pause, "")
FUNCTION_END()

FUNCTION_BEGIN(PixCaptureNextFrames, "Do a pix capture for the next N frames rendered")
    PARAMETER(std::string, fileName, "The filename to save as. Pix captures usually have a .wpix extension.")
    PARAMETER(int, numFrames, "1 to do a single frame")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedBufferFile, "Set the file name to load an imported buffer from")
    PARAMETER(std::string, bufferName, "The name of the buffer, in the render graph.")
    PARAMETER(std::string, fileName, "The file to load into the buffer.")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedBufferStruct, "Set the struct type of an imported buffer")
    PARAMETER(std::string, bufferName, "The name of the buffer, in the render graph.")
    PARAMETER(std::string, structName, "The name of the struct to use as the type of the buffer.")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedBufferMaterialShaderFile, "Set the file name of an imported buffer material shader file")
    PARAMETER(std::string, bufferName, "The name of the buffer, in the render graph.")
    PARAMETER(std::string, fileName, "The name of the file to write the material shader file out to.")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedBufferType, "Set the file name of an imported buffer material shader file")
    PARAMETER(std::string, bufferName, "The name of the buffer, in the render graph.")
    PARAMETER(int, type, "Set the type of an imported buffer. must be a DataFieldType. See UserDocumentation/PythonTypes.txt")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedBufferCount, "Set the count of an imported buffer")
    PARAMETER(std::string, bufferName, "The name of the buffer, in the render graph.")
    PARAMETER(int, count, "How many of the type to make the buffer big enough to hold.")
FUNCTION_END()

FUNCTION_BEGIN(GetImportedBufferBounds, "Get [minx, miny, minz, maxx, maxy, maxz] of an imported buffer that has a position semantic.")
    PARAMETER(std::string, bufferName, "The name of the buffer, in the render graph.")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedBufferCSVHeaderRow, "Set whether or not the csv file has a header row")
    PARAMETER(std::string, bufferName, "The name of the buffer, in the render graph.")
    PARAMETER(bool, hasHeaderRow, "")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedTextureFile, "Set the file name of an imported texture")
    PARAMETER(std::string, textureName, "The name of the texture, in the render graph.")
    PARAMETER(std::string, fileName, "The name of the file to read the texture from.")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedTextureSourceIsSRGB, "Set whether or not the file on disk is sRGB.")
    PARAMETER(std::string, textureName, "The name of the texture, in the render graph.")
    PARAMETER(bool, sRGB, "")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedTextureMakeMips, "Whether or not to make mips for the imported texture.")
    PARAMETER(std::string, textureName, "The name of the texture, in the render graph.")
    PARAMETER(bool, makeMips, "")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedTextureFormat, "Set the texture format of an imported texture")
    PARAMETER(std::string, textureName, "The name of the texture, in the render graph.")
    PARAMETER(int, format, "Must be a DataFieldType. See UserDocumentation/PythonTypes.txt")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedTextureColor, "Set the color or tint of an imported texture. Colors are in [0, 1]")
    PARAMETER(std::string, textureName, "The name of the texture, in the render graph.")
    PARAMETER(float, R, "")
    PARAMETER(float, G, "")
    PARAMETER(float, B, "")
    PARAMETER(float, A, "")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedTextureSize, "Set the x,y,z dimensions an imported texture. If no file name is specified, this size must be given instead, so the viewer knows how big to make the texture")
    PARAMETER(std::string, textureName, "The name of the texture, in the render graph.")
    PARAMETER(int, x, "")
    PARAMETER(int, y, "")
    PARAMETER(int, z, "")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedTextureBinarySize, "Sets the x,y,z dimensions of a binary imported texture.  If loading pixels from a raw binary file, you must specify these dimensions before the file will be loaded")
    PARAMETER(std::string, textureName, "The name of the texture, in the render graph.")
    PARAMETER(int, x, "")
    PARAMETER(int, y, "")
    PARAMETER(int, z, "")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedBufferDataStream, "Set the data stream of an imported buffer. Some files contain multiple types of data, this selects which data you want. Must be a GGUserFile_SceneDataStream. See UserDocumentation/PythonTypes.txt")
    PARAMETER(std::string, textureName, "The name of the texture, in the render graph.")
    PARAMETER(int, dataStream, "")
FUNCTION_END()

FUNCTION_BEGIN(SetImportedTextureBinaryFormat, "Sets the format of the binary file.")
    PARAMETER(std::string, textureName, "The name of the texture, in the render graph.")
    PARAMETER(int, format, "Must be a DataFieldType. See UserDocumentation/PythonTypes.txt")
FUNCTION_END()

FUNCTION_BEGIN(SetFrameDeltaTime, "Set the frame delta time, in seconds, to a fixed value. Useful for recording videos by setting a fixed frame rate. Clear by setting to 0.")
    PARAMETER(float, deltaTimeSeconds, "")
FUNCTION_END()

FUNCTION_BEGIN(SetCameraPos, "Set the camera position.")
    PARAMETER(float, x, "")
    PARAMETER(float, y, "")
    PARAMETER(float, z, "")
FUNCTION_END()

FUNCTION_BEGIN(SetCameraFOV, "Set the camera vertical field of view. In degrees.")
    PARAMETER(float, FOV, "")
FUNCTION_END()

FUNCTION_BEGIN(SetCameraAltitudeAzimuth, "Set the camera altitude and azimuth")
    PARAMETER(float, altitude, "")
    PARAMETER(float, azimuth, "")
FUNCTION_END()

FUNCTION_BEGIN(SetCameraNearFarZ, "Set the near and far plane")
    PARAMETER(float, nearZ, "")
    PARAMETER(float, farZ, "")
FUNCTION_END()

FUNCTION_BEGIN(SetCameraFlySpeed, "Set the fly speed of the camera. How many units it moves every second")
    PARAMETER(float, flySpeed, "")
FUNCTION_END()

FUNCTION_BEGIN(SetProjMtxTextureName, "Set the texture name used to compute projection matrix resolution")
    PARAMETER(std::string, textureName, "The name of the texture, in the render graph.")
FUNCTION_END()

FUNCTION_BEGIN(SetCameraReverseZInfiniteDepth, "Enable/disable reverseZInfiniteDepth camera option. camera must have reversed Z turned on to also have reversed Z infinite depth.")
    PARAMETER(bool, enable, "")
FUNCTION_END()

FUNCTION_BEGIN(SetCameraJitterLength, "Set jitter sequence length for camera jitter. In number of frames.")
    PARAMETER(int, length, "")
FUNCTION_END()

FUNCTION_BEGIN(GetCameraPos, "Get the camera position.")
FUNCTION_END()

FUNCTION_BEGIN(GetCameraAltitudeAzimuth, "Get the camera altitude and azimuth.")
FUNCTION_END()

FUNCTION_BEGIN(WriteGPUResourceBase64, "Writes a gpu resource the next time the technique runs.")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
    PARAMETER(std::string, bytes, "Base 64 encoded bytes to write. Must be the correct number of bytes for the texture or buffer being written to.")
    PARAMETER(int, subResourceIndex, "For writing to mips and similar. 0 for writing to simple 2d textures, or buffers.")
FUNCTION_END()

FUNCTION_BEGIN(IsResourceCreated, "Returns whether or not a resource is created. A resource is created when it has enough info to be created, and when a frame has executed to run the logic to create it. If it isn't created but should be, it may be in an error state.")
FUNCTION_END()

FUNCTION_BEGIN(SetViewedResource, "Sets the resource being viewed")
    PARAMETER(std::string, viewableResourceName, "The name of the viewable resource, as found in the list returned by GetViewableResourceList(). These are resources at different points in the render graph.")
FUNCTION_END()

FUNCTION_BEGIN(ForceEnableProfiling, "If true, forces profiling on, even when the profiling window isn't being shown. So that you can get profiling data.")
    PARAMETER(bool, forceEnable, "")
FUNCTION_END()

FUNCTION_BEGIN(GetProfilingData, "Gets the profiling data from the last technique execution. ForceEnableProfiling needs to be enabled. Time is in milliseconds. CPU time is first, GPU time is second.")
FUNCTION_END()

FUNCTION_BEGIN(GGEnumValue, "Gets the integer value of an enum defined in the loaded .gg file.")
    PARAMETER(std::string, enumName, "The name of the enum type, specified in the .gg file")
    PARAMETER(std::string, label, "The label in that enum you want to get the integer value of")
FUNCTION_END()

FUNCTION_BEGIN(GGEnumLabel, "Gets the string label of an enum defined in the loaded .gg file.")
    PARAMETER(std::string, enumName, "The name of the enum type, specified in the .gg file")
    PARAMETER(int, value, "The value in that enum you want to get the enum label of")
FUNCTION_END()

FUNCTION_BEGIN(GGEnumCount, "Returns the number of enum values for an enum defined in the loaded .gg file.")
    PARAMETER(std::string, enumName, "The name of the enum type, specified in the .gg file")
FUNCTION_END()

FUNCTION_BEGIN(GetGPUString, "Returns the name of the gpu and driver version.")
FUNCTION_END()

FUNCTION_BEGIN(SetWindowSize, "Sets the window size of the viewer.")
    PARAMETER(int, width, "")
    PARAMETER(int, height, "")
FUNCTION_END()

FUNCTION_BEGIN(MinimizeWindow, "Minimizes the viewer window.")
FUNCTION_END()

FUNCTION_BEGIN(MaximizeWindow, "Maximizes the viewer window.")
FUNCTION_END()

FUNCTION_BEGIN(RestoreWindow, "Restores the viewer window.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_Enabled, "Optionally set whether AMD Frame Gen is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_SleepMS, "Optionally set the number of milliseconds to sleep() each frame, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_Depth, "Optionally set the depth buffer texture for AMD Frame Gen, and return the state before it was optionally changed.")
    PARAMETER(std::string, set, "empty string to not change it, else the name of a texture node in the .gg render graph")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_MotionVectors, "Optionally set the depth buffer texture for AMD Frame Gen, and return the state before it was optionally changed.")
    PARAMETER(std::string, set, "empty string to not change it, else the name of a texture node in the .gg render graph")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_ENABLE_ASYNC_WORKLOAD_SUPPORT, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_ENABLE_HIGH_DYNAMIC_RANGE, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_ENABLE_DEBUG_CHECKING, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_DRAW_DEBUG_TEAR_LINES, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_DRAW_DEBUG_RESET_INDICATORS, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_DRAW_DEBUG_VIEW, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_DRAW_DEBUG_PACING_LINES, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_allowAsyncWorkloads, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_onlyPresentGenerated, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_constrainToRectangle, "Optionally set whether this feature is enabled, and return the state before it was optionally changed.")
    PARAMETER(int, set, "-1 to not change the value. 1 to set it to true. 0 to set it to false.")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_uiTexture, "Optionally set the ui texture for AMD Frame Gen, and return the state before it was optionally changed.")
    PARAMETER(std::string, set, "empty string to not change it, else the name of a texture node in the .gg render graph")
FUNCTION_END()

FUNCTION_BEGIN(AMDFrameGen_hudlessTexture, "Optionally set the hudless texture for AMD Frame Gen, and return the state before it was optionally changed.")
    PARAMETER(std::string, set, "empty string to not change it, else the name of a texture node in the .gg render graph")
FUNCTION_END()

FUNCTION_BEGIN(GetLastPythonError, "The MCP server tool calls translate into python strings sent to the viewer for execution. This returns the python error for the previous command, if there was one.")
FUNCTION_END()

FUNCTION_BEGIN(GetLog, "Gets the contents of the log window. This contains info, warnings and errors about viewer operations")
FUNCTION_END()

FUNCTION_BEGIN(ClearLog, "Gets the contents of the log window.")
FUNCTION_END()

FUNCTION_BEGIN(RunPythonFile, "Tells the viewer to run a python file")
    PARAMETER(std::string, fileName, "The name of the file to run")
FUNCTION_END()

FUNCTION_BEGIN(GetResourceList, "Gets information about the buffers and textures in the render graph. Visibility is a hint to ownership. Imported resources are meant to be an input to the technique and may also be modified during the technique. Exported resources are meant to be an output from the technique, and are managed by the technique itself. Internal resources are managed by the technique itself as an internal detail, such as a ping pong buffer for a blur, and cannot be read or written outside of the technique.")
FUNCTION_END()

FUNCTION_BEGIN(GetVariableList, "Gets information about the variables in the render graph.  Visibility is a hint as to their purpose. User visibility is for settings that users are meant to modify. Host visibility is for settings that calling code is meant to modify. Internal visibility is usually not meant to be modified by anything except the internal technique logic itself.")
FUNCTION_END()

FUNCTION_BEGIN(GetViewableResourceList, "Returns the list of viewable resources. These are the names of resources at different points in the render graph that can be read back and written to.")
FUNCTION_END()

// =================================== MCP ONLY ===================================
// The functions that are exposed to only the MCP server

FUNCTION_BEGIN_MCP(Ping, "Used to see if the MCP server is still working.")
FUNCTION_END_MCP()

FUNCTION_BEGIN_MCP(SetViewerIP, "Sets the IP address and port of the viewer for commands. A server connection is made for every command except this one, Ping, and LaunchViewer, and the connection is closed after the request is finished.")
    PARAMETER_MCP(std::string, IP, "IP address. Commonly 127.0.0.1.")
    PARAMETER_MCP(int, port, "The port to connect to. Commonly 6161.")
FUNCTION_END_MCP()

FUNCTION_BEGIN_MCP(ViewerPing, "Used to see if the MCP server is connected to a viewer")
FUNCTION_END_MCP()

FUNCTION_BEGIN_MCP(LaunchViewer, "Launches GigiViewer.exe with the specified command line parameters")
    PARAMETER_MCP(int, port, "The port to listen for connections on. Use 6161 as default.")
    PARAMETER_MCP(std::string, commandLine, "")
FUNCTION_END_MCP()

FUNCTION_BEGIN_MCP(RunPythonString, "Run a given string as python. This uses a python environment embeded in the GigiViewer.")
    PARAMETER_MCP(std::string, command, "")
FUNCTION_END_MCP()

// Undefine macros for convenience
#undef FUNCTION_BEGIN
#undef FUNCTION_END
#undef PARAMETER
#undef FUNCTION_BEGIN_MCP
#undef FUNCTION_END_MCP
#undef PARAMETER_MCP
