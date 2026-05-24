-- Path of Building — post-Main init (macOS arm64 #8; loaded via __mac_dofile_c after PLoadModule).
local self = launch
local mainPlm = launch._mainPlm
launch._mainPlm = nil

self.versionNumber = "?"
self.versionBranch = "?"
self.versionPlatform = "?"

__mac_loadfile_stash_c("LaunchCallbacks.lua")
local cbChunk = __mac_loadfile_chunk
local cbErr = __mac_loadfile_err
__mac_loadfile_chunk = nil
__mac_loadfile_err = nil
if not cbChunk then
	ConPrintf("Failed to load LaunchCallbacks.lua: %s\n", tostring(cbErr))
	return
end
cbChunk()

local firstRunFile = io.open("first.run", "r")
if firstRunFile then
	firstRunFile:close()
	os.remove("first.run")
	ConClear()
	ConPrintf("Please wait while we complete installation...\n")
	local updatePlm = LoadModule("UpdateCheck")
	local updateMode = updatePlm.r1
	local errMsg = updatePlm.r2
	if not updateMode then
		self.updateErrMsg = errMsg
	elseif updateMode ~= "none" then
		self:ApplyUpdate(updateMode)
		return
	end
end

local installedFile = io.open("installed.cfg", "r")
if installedFile then
	self.installedMode = true
	installedFile:close()
end

self.main = mainPlm.main
if mainPlm.err then
	self:ShowErrMsg("Error loading main script: %s", mainPlm.err)
elseif not self.main then
	self:ShowErrMsg("Error loading main script: no object returned")
elseif self.main.Init then
	local initErr = PCall(self.main.Init, self.main)
	if initErr then
		self:ShowErrMsg("In 'Init': %s", initErr)
	end
end

if not self.devMode and not firstRunFile then
	self:CheckForUpdate(true)
end

local okXml, xml = pcall(require, "xml")
if okXml then
	local localManXML = xml.LoadXMLFile("manifest.xml") or xml.LoadXMLFile("../manifest.xml")
	if localManXML and localManXML[1] and localManXML[1].elem == "PoBVersion" then
		for i = 1, #localManXML[1] do
			local node = localManXML[1][i]
			if node and node.elem == "Version" then
				self.versionNumber = node.attrib.number
				self.versionBranch = node.attrib.branch
				self.versionPlatform = node.attrib.platform
			end
		end
	end
end
