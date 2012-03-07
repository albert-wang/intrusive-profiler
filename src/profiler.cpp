#include "profiler.h"

#include <boost/lexical_cast.hpp>

namespace Engine
{
	namespace Profiler
	{
		namespace 
		{
			Settings globalSettings;
			volatile long long currentIdentifier;

			boost::thread_specific_ptr<Detail::ThreadProfiler> tspProfiler;
			Detail::ThreadProfiler * eventFile;

			CRITICAL_SECTION activeLock;
			std::vector<Detail::ThreadProfiler *> activeThreads;

			struct ScopedCriticalSection
			{
				ScopedCriticalSection(CRITICAL_SECTION * c)
					:target(c)
				{
					EnterCriticalSection(c);
				}

				~ScopedCriticalSection()
				{
					LeaveCriticalSection(target);
				}

				CRITICAL_SECTION * target;
			};


			Handle allocateID()
			{
				long long v = InterlockedIncrement64(&currentIdentifier);
				return static_cast<Handle>(v);
			}
		}

		namespace Detail
		{
			ThreadProfiler::ThreadProfiler()
				:outputBuffer(nullptr)
				,currentBytesWritten(0)
				,threadIdentifier(0)
				,bufferSize(0)
			{
				threadIdentifier = GetCurrentThreadId();

				std::string outputFilePath = globalSettings.outputDirectory + "/" + boost::lexical_cast<std::string>(threadIdentifier);
				outputFile = CreateFileA(outputFilePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

				InitializeCriticalSection(&flushLock);
				outputBuffer = new boost::uint8_t[globalSettings.outputBufferLength];
				bufferSize = globalSettings.outputBufferLength;

				EnterCriticalSection(&activeLock);
				activeThreads.push_back(this);
				LeaveCriticalSection(&activeLock);

				LARGE_INTEGER freq;
				QueryPerformanceFrequency(&freq);
				RawEntry freqEntry;
				freqEntry.type = RawEntry::ENTRY_FREQUENCY;
				freqEntry.integralData = 0;
				freqEntry.variantData.time = freq.QuadPart;
				write(freqEntry);
			}

			ThreadProfiler::ThreadProfiler(const std::string& output)
				:outputBuffer(nullptr)
				,currentBytesWritten(0)
				,threadIdentifier(0)
				,bufferSize(0)
			{
				threadIdentifier = GetCurrentThreadId();

				std::string outputFilePath = globalSettings.outputDirectory + "/" + output;
				outputFile = CreateFileA(outputFilePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

				InitializeCriticalSection(&flushLock);
				outputBuffer = new boost::uint8_t[globalSettings.outputBufferLength];
				bufferSize = globalSettings.outputBufferLength;

				LARGE_INTEGER freq;
				QueryPerformanceFrequency(&freq);
				RawEntry freqEntry;
				freqEntry.type = RawEntry::ENTRY_FREQUENCY;
				freqEntry.integralData = 0;
				freqEntry.variantData.time = freq.QuadPart;
				write(freqEntry);
			}

			ThreadProfiler::~ThreadProfiler()
			{
				flush();

				EnterCriticalSection(&activeLock);
				auto it = std::find(activeThreads.begin(), activeThreads.end(), this);
				if (it != activeThreads.end())
				{
					activeThreads.erase(it);
				}
				LeaveCriticalSection(&activeLock);
			
				delete [] outputBuffer;
				DeleteCriticalSection(&flushLock);
				CloseHandle(outputFile);
			}

			void ThreadProfiler::enter(Handle id, boost::uint64_t time)
			{
				Detail::RawEntry entry;
				entry.type = Detail::RawEntry::ENTRY_FUNCTION_ENTER;
				entry.integralData = id;
				entry.variantData.time = time;

				write(entry);
			}

			void ThreadProfiler::leave(Handle id, boost::uint64_t time)
			{
				Detail::RawEntry entry;
				entry.type = Detail::RawEntry::ENTRY_FUNCTION_LEAVE;
				entry.integralData = id;
				entry.variantData.time = time;

				write(entry);
			}

			void ThreadProfiler::raise(Handle id, boost::uint64_t time)
			{
				Detail::RawEntry entry;
				entry.type = Detail::RawEntry::ENTRY_EVENT;
				entry.integralData = id;
				entry.variantData.time = time;

				write(entry);
			}

			void ThreadProfiler::lock()
			{
				EnterCriticalSection(&flushLock);
			}

			void ThreadProfiler::unlock()
			{
				LeaveCriticalSection(&flushLock);
			}

			void ThreadProfiler::unprotectedFlush()
			{
				DWORD count = 0;
				size_t offset = 0;
				while (offset + count != currentBytesWritten)
				{
					offset += count;
					WriteFile(outputFile, outputBuffer + offset, currentBytesWritten - offset, &count, nullptr);
				}
				currentBytesWritten = 0;
			}

			void ThreadProfiler::flush()
			{
				lock();
				unprotectedFlush();
				unlock();
			}

			void ThreadProfiler::predictWriteEntry(size_t bytes)
			{
				if (currentBytesWritten + 
					sizeof(boost::uint8_t) + 
					sizeof(boost::uint32_t) + 
					bytes >= bufferSize)
				{
					flush();
				}
			}

			void ThreadProfiler::write(const RawEntry& entry)
			{
				switch(entry.type) 
				{
				//These events use the name field.
				case RawEntry::ENTRY_FUNCTION_IDENTIFIER:
				case RawEntry::ENTRY_EVENT_IDENTIFIER:
					{
						size_t len = strlen(entry.variantData.name);
						if (len > 255)
						{
							//Error case.
						}

						//Predict writing the string.
						predictWriteEntry(sizeof(boost::uint8_t) + len);

						ScopedCriticalSection lock(&flushLock);
						boost::uint8_t byteLength = static_cast<boost::uint8_t>(len);
						unprotectedWrite(entry.type);
						unprotectedWrite(entry.integralData);
						unprotectedWrite(byteLength);
						unprotectedWrite(entry.variantData.name, entry.variantData.name + len);
						
						break;
					}
				//Everything else uses the time field.
				default:
					{
						//Predict writing the time
						predictWriteEntry(sizeof(boost::uint64_t));

						ScopedCriticalSection lock(&flushLock);
						unprotectedWrite(entry);
						break;
					}
				}
			}
		}
		
		Entry::Entry(const char * name)
		{
			using namespace Detail;
			identifier = allocateID();
			if (!tspProfiler.get())
			{
				tspProfiler.reset(new ThreadProfiler());
			}

			RawEntry entry;
			entry.type = RawEntry::ENTRY_FUNCTION_IDENTIFIER;
			entry.integralData = identifier;
			entry.variantData.name = name;
			eventFile->write(entry);
		}

		void Entry::start()
		{
			LARGE_INTEGER time;
			QueryPerformanceCounter(&time);
			tspProfiler->enter(identifier, time.QuadPart);
		}

		void Entry::stop()
		{
			LARGE_INTEGER time;
			QueryPerformanceCounter(&time);
			tspProfiler->leave(identifier, time.QuadPart);
		}

		ScopedEntry::ScopedEntry(Entry& ent)
			:target(&ent)
		{
			target->start();
		}

		ScopedEntry::~ScopedEntry()
		{
			target->stop();
		}

		void initialize(const Settings& settings)
		{
			globalSettings = settings;

			CreateDirectoryA(globalSettings.outputDirectory.c_str(), nullptr);
			globalSettings.outputDirectory += "/" + boost::lexical_cast<std::string>(time(nullptr));

			CreateDirectoryA(globalSettings.outputDirectory.c_str(), nullptr);

			currentIdentifier = 0;
			InitializeCriticalSection(&activeLock);

			eventFile = new Detail::ThreadProfiler("events");

			Detail::RawEntry mainThreadID;
			mainThreadID.type = Detail::RawEntry::ENTRY_MAIN_THREAD_IDENTIFIER;
			mainThreadID.integralData = GetCurrentThreadId();
			mainThreadID.variantData.time = 0;
			eventFile->write(mainThreadID);
		}

		void deinitalize()
		{
			flush();
			delete eventFile;
		}

		Handle allocateEvent(const char * eventName)
		{
			Handle id = allocateID();

			Detail::RawEntry eventDescriptor;
			eventDescriptor.type = Detail::RawEntry::ENTRY_EVENT_IDENTIFIER;
			eventDescriptor.integralData = id;
			eventDescriptor.variantData.name = eventName;

			eventFile->write(eventDescriptor);
			eventFile->flush();

			return id;
		}

		void raiseGlobalEvent(Handle ev)
		{
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);

			eventFile->raise(ev, now.QuadPart);
		}

		void raiseThreadLocalEvent(Handle ev)
		{
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			tspProfiler->raise(ev, now.QuadPart);
		}

		void flush()
		{
			EnterCriticalSection(&activeLock);
			std::vector<Detail::ThreadProfiler *> ats = activeThreads;
			LeaveCriticalSection(&activeLock);

			for (size_t i = 0; i < ats.size(); ++i)
			{
				ats[i]->flush();
			}
		}
	}
}

