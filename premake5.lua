-- premake5.lua

-- you can redefine dependencies
SDL2_DIR = os.getenv("SDL2_DIR") or "dependencies/SDL2"


workspace "OpenDriver2Tools"
    configurations { "Debug", "Release" }
	characterset "ASCII"
    defines { VERSION } 
	
	includedirs {
		"./"
	}
	
    filter "system:linux"
        buildoptions {
            "-Wno-narrowing",
            "-fpermissive",
        }

	filter "system:Windows"
		disablewarnings { "4996", "4554", "4244", "4101", "4838", "4309" }

    filter "configurations:Debug"
        defines { 
            "DEBUG", 
        }
        symbols "On"

    filter "configurations:Release"
        defines {
            "NDEBUG",
        }

-- NoSTD
project "libnstd"
    kind "StaticLib"
    language "C++"
	filter "system:Windows"
		defines { "_CRT_SECURE_NO_WARNINGS" }
    
	includedirs {
		"dependencies/libnstd/include"
	}
	
    files {
        "dependencies/libnstd/src/**.cpp",
        "dependencies/libnstd/src/**.h",
    }
	
-- ImGui
project "ImGui"
    kind "StaticLib"
    language "C++"
	filter "system:Windows"
		defines { "_CRT_SECURE_NO_WARNINGS" }
    
	includedirs {
		"dependencies/imgui"
	}
	
    files {
        "dependencies/imgui/*.cpp",
        "dependencies/imgui/*.h",
    }
	
-- little framework
project "frameworkLib"
    kind "StaticLib"
    language "C++"
	
	dependson { "libnstd" }
	
	filter "system:Windows"
		defines { "_CRT_SECURE_NO_WARNINGS" }
    
	includedirs {
		"math",
		"core",
		"util",
		"dependencies/libnstd/include",
	}
	
	files {
		"math/**.cpp",
		"math/**.h",
		"core/**.cpp",
		"core/**.h",
		"util/**.cpp",
		"util/**.h",
    }
	
	links { "libnstd" }

include "DriverLevelTool"
include "DriverSoundTool"
include "DriverImageTool"
include "Driver2CutsceneTool"
include "Driver2MissionTool"