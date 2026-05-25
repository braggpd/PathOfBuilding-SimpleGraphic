#@ SimpleGraphic
APP_NAME = "Path of Building (PoE2)"
SetWindowTitle(APP_NAME)
ConExecute("set vid_mode 8")
ConExecute("set vid_resizable 3")
launch = { }
jit.opt.start('maxtrace=4000','maxmcode=8192')
collectgarbage("setpause", 400)
function launch:OnInit()
	self.devMode = false
	self.installedMode = false
	self.lastUpdateCheck = GetTime()
	self.subScripts = { }
	self.startTime = GetTime()
	if io.open("Modules/Main.lua", "r") then
		self.devMode = true
	end
	RenderInit("DPI_AWARE")
	ConPrintf("OnInit end\n")
end
function launch:OnFrame() end
SetMainObject(launch)
