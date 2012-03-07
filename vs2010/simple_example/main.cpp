#include "profiler.h"

int main(int argc, char * argv[])
{
	namespace Profiler = Engine::Profiler;

	Profiler::Settings settings;
	settings.outputBufferLength = 16384;
	settings.outputDirectory = "profile";

	Profiler::initialize(settings);

	Profiler::Handle eventID = Profiler::allocateEvent("Hello, World event");

	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);
	for (size_t i = 0; i < 10000; ++i)
	{
		PROFILE(InnerLoop);
		if (i % 100 == 0)
		{
			Profiler::raiseGlobalEvent(eventID);
		}
	}
	
	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);

	std::cout << ((double)end.QuadPart - start.QuadPart) / freq.QuadPart * 1000 << "\n";

	Profiler::deinitalize();
}