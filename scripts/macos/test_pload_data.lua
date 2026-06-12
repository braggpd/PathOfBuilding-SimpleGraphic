#@ SimpleGraphic
-- PLoad Modules/Data only — hits __mac_pload_data_after_misc_c (faster than Launch.lua).
-- Copy to PoB src/test_pload_data.lua for POB_MAC_BISECT_DATA_MISC probes.

launch = {}
SetMainObject(launch)

function launch:OnInit()
	RenderInit("DPI_AWARE")
	ConPrintf("test_pload_data: PLoadModule Modules/Data...\n")
	local plm = PLoadModule("Modules/Data")
	if plm.err then
		ConPrintf("test_pload_data: FAIL err=%s\n", tostring(plm.err))
	else
		ConPrintf("test_pload_data: OK\n")
	end
end

function launch:OnFrame()
end
