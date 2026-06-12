#@ SimpleGraphic
-- Minimal Misc.lua load probe for POB_MAC_BISECT_MISC (copy to PoB src/test_misc.lua).
-- Faster than full Launch.lua — hits LoadModule("Data/Misc") directly.

launch = {}
SetMainObject(launch)

function launch:OnInit()
	ConPrintf("test_misc: loading Data/Misc...\n")
	local data = {}
	LoadModule("Data/Misc", data)
	ConPrintf("test_misc: Misc OK\n")
end

function launch:OnFrame()
end
