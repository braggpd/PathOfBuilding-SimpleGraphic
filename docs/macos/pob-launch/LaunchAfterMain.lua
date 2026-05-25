-- Path of Building — post-Main init (macOS arm64 #8; no top-level locals from C API / stash globals).
ConPrintf("LaunchAfterMain start\n")

launch.versionNumber = "?"
launch.versionBranch = "?"
launch.versionPlatform = "?"

ConPrintf("before PLoadModule\n")
PLoadModule("Modules/Main")
ConPrintf("after PLoadModule\n")

launch._firstRun = io.open("first.run", "r") ~= nil
if launch._firstRun then
	io.open("first.run", "r"):close()
	os.remove("first.run")
	ConClear()
	ConPrintf("Please wait while we complete installation...\n")
	launch._updatePlm = LoadModule("UpdateCheck")
	if not launch._updatePlm.r1 then
		launch.updateErrMsg = launch._updatePlm.r2
	elseif launch._updatePlm.r1 ~= "none" then
		launch:ApplyUpdate(launch._updatePlm.r1)
		return
	end
end

if io.open("installed.cfg", "r") then
	launch.installedMode = true
	io.open("installed.cfg", "r"):close()
end

launch.main = launch._mainPlm.main
if launch._mainPlm.err then
	launch:ShowErrMsg("Error loading main script: %s", launch._mainPlm.err)
elseif not launch.main then
	launch:ShowErrMsg("Error loading main script: no object returned")
elseif launch.main.Init then
	launch._initErr = PCall(launch.main.Init, launch.main)
	if launch._initErr then
		launch:ShowErrMsg("In 'Init': %s", launch._initErr)
	end
end

if not launch.devMode and not launch._firstRun then
	launch:CheckForUpdate(true)
end

require("xml")
launch._xml = package.loaded["xml"]
if launch._xml then
	launch._manXML = launch._xml.LoadXMLFile("manifest.xml") or launch._xml.LoadXMLFile("../manifest.xml")
	if launch._manXML and launch._manXML[1] and launch._manXML[1].elem == "PoBVersion" then
		for i = 1, #launch._manXML[1] do
			launch._verNode = launch._manXML[1][i]
			if launch._verNode and launch._verNode.elem == "Version" then
				launch.versionNumber = launch._verNode.attrib.number
				launch.versionBranch = launch._verNode.attrib.branch
				launch.versionPlatform = launch._verNode.attrib.platform
			end
		end
	end
end

ConPrintf("LaunchAfterMain end\n")
