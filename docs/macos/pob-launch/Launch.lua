#@ SimpleGraphic
local startTime = GetTime()
APP_NAME = "Path of Building (PoE2)"
SetWindowTitle(APP_NAME)
ConExecute("set vid_mode 8")
ConExecute("set vid_resizable 3")
launch = { }
SetMainObject(launch)
jit.opt.start('maxtrace=4000','maxmcode=8192')
collectgarbage("setpause", 400)
function launch:OnInit()
	self.devMode = false
	self.installedMode = false
	self.lastUpdateCheck = GetTime()
	self.subScripts = { }
	self.startTime = startTime
	do
		local mf = io.open("Modules/Main.lua", "r")
		if mf then
			mf:close()
			self.devMode = true
		end
	end
	RenderInit("DPI_AWARE")
	ConPrintf("before PLoadModule\n")
	local mainPlm = PLoadModule("Modules/Main")
	if mainPlm.err then
		ConPrintf("PLoadModule err=%s\n", tostring(mainPlm.err))
	else
		ConPrintf("PLoadModule main type=%s\n", type(mainPlm.main))
	end
	launch._mainPlm = mainPlm
	launch._runAfterMain = true
end
function launch:OnFrame() end
