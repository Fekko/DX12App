#include "Main.h"

#include <filesystem>
#include "MyApp.h"

using namespace DxUtil;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*prevInstance*/, PSTR /*cmdLine*/, int /*showCmd*/) 
{
	std::filesystem::path cwd = std::filesystem::current_path();



	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try {
		BoxApp app(hInstance);
		if (not app.Initialize()) return 0;
		return app.Run();
	}
	catch (DxException& e) {
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}