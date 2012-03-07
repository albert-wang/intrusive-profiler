#ifdef _WIN32
#	include <Windows.h>
#else
#	include <ctime.h>
#endif

#include <boost/cstdint.hpp>
#include <boost/variant.hpp>
#include <boost/array.hpp>
#include <boost/preprocessor.hpp>
#include <boost/thread.hpp>
#include <string>

namespace Engine
{
	namespace Profiler
	{
		typedef boost::uint32_t Handle;
		namespace Detail
		{
			struct RawEntry
			{
				enum Type
				{
					ENTRY_FUNCTION_IDENTIFIER = 0,
					ENTRY_EVENT_IDENTIFIER,
					ENTRY_FUNCTION_ENTER,
					ENTRY_FUNCTION_LEAVE,
					ENTRY_EVENT,
					ENTRY_FREQUENCY,
					ENTRY_MAIN_THREAD_IDENTIFIER
				};

				union VariantData
				{
					//Set in IDENTIFIER types.
					const char * name;

					//set in FUNCTION and EVENT types.
					boost::uint64_t time;
				};

				boost::uint8_t type;

				boost::uint32_t integralData;
				VariantData variantData;
			};

			//Handles a thread's profiling data.
			class ThreadProfiler
			{
			public:
				ThreadProfiler();
				ThreadProfiler(const std::string& output);
				~ThreadProfiler();

				void enter(Handle id, boost::uint64_t time);
				void leave(Handle id, boost::uint64_t time);
				void raise(Handle id, boost::uint64_t time);
				
				void write(const RawEntry& entry);

				void lock();
				void unlock();
				
				void flush();
				void unprotectedFlush();
			private:
				void predictWriteEntry(size_t dataBytes);

				template<typename T>
				void unprotectedWrite(const T& t)
				{
					memcpy(outputBuffer + currentBytesWritten, &t, sizeof(t));
					currentBytesWritten += sizeof(t);
				}

				template<typename T>
				void unprotectedWrite(const T * b, const T * e)
				{
					memcpy(outputBuffer + currentBytesWritten, b, sizeof(T) * (e - b));
					currentBytesWritten += sizeof(T) * (e - b);
				}

				HANDLE outputFile;
				boost::uint8_t * outputBuffer;
				size_t currentBytesWritten;
				size_t bufferSize;

				CRITICAL_SECTION flushLock;
				size_t threadIdentifier;
			};
		}

		struct Settings
		{
			std::string outputDirectory;
			size_t outputBufferLength;
			bool useCompression;
		};

		class Entry
		{
			friend class ScopedEntry;
		public:
			Entry(const char * name);
		private:
			void start();
			void stop();

			Handle identifier;
		};

		class ScopedEntry
		{
		public:
			explicit ScopedEntry(Entry& entry);
			~ScopedEntry();

		private:
			Entry * target;
		};

		//Controlling the profiler comes here.
		void initialize(const Settings& settings);
		void deinitalize();

		Handle allocateEvent(const char * eventName);
		void raiseGlobalEvent(Handle ev);
		void raiseThreadLocalEvent(Handle ev);
		
		void flush();
	}
}

#define PROFILE(name) \
	static ::Engine::Profiler::Entry BOOST_PP_CAT(RAWR_RESERVED_IDENTIFIER, name)(BOOST_PP_STRINGIZE(name)); \
	::Engine::Profiler::ScopedEntry BOOST_PP_CAT(RAWR_RESERVED_IDENTIFIER_, __COUNTER__)(BOOST_PP_CAT(RAWR_RESERVED_IDENTIFIER, name))

#define START_PROFILE(name) \
	{ static ::Engine::Profiler::Entry BOOST_PP_CAT(RAWR_RESERVED_IDENTIFIER, name)(BOOST_PP_STRINGIZE(name)); \
	::Engine::Profiler::ScopedEntry BOOST_PP_CAT(RAWR_RESERVED_IDENTIFIER_, __COUNTER__)(BOOST_PP_CAT(RAWR_RESERVED_IDENTIFIER, name))

#define END_PROFILE() \
	}



