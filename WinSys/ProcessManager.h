#pragma once

#include <memory>
#include <vector>
#include <type_traits>
#include "Keys.h"
#include "ProcessInfo.h"
#include "ThreadInfo.h"
#include "Processes.h"
#include "Enums.h"

#ifdef __cplusplus
#if _MSC_VER >= 1300
#define TYPE_ALIGNMENT( t ) __alignof(t)
#endif
#else
#define TYPE_ALIGNMENT( t ) \
	FIELD_OFFSET( struct { char x; t test; }, test )
#endif

#ifdef WINSYS_NAMESPACE
namespace WinSys {
#endif
	template<typename TProcessInfo = ProcessInfo, typename TThreadInfo = ThreadInfo>
	class ProcessManager {
		static_assert(std::is_base_of<ProcessInfo, TProcessInfo>);
		static_assert(std::is_base_of<ThreadInfo, TThreadInfo>);
	public:
		ProcessManager(const ProcessManager&) = delete;
		ProcessManager& operator=(const ProcessManager&) = delete;

		size_t EnumProcesses() {
			return EnumProcesses(false);
		}
		size_t EnumProcessesAndThreads(uint32_t pid = 0) {
			return EnumProcesses(true, pid);
		}

		[[nodiscard]] std::vector<std::shared_ptr<TProcessInfo>> const& GetTerminatedProcesses() const {
			return _terminatedProcesses;
		}
		[[nodiscard]] std::vector<std::shared_ptr<TProcessInfo>> const& GetNewProcesses() const {
			return _newProcesses;
		}

		[[nodiscard]] std::wstring GetProcessNameById(uint32_t pid) const {
			if (pid == 0)
				return L"";
			auto pi = GetProcessById(pid);
			return pi ? pi->GetImageName() : L"";
		}

		ProcessManager() {
			if (s_totalProcessors == 0) {
				s_totalProcessors = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
				s_isElevated = Process::OpenById(::GetCurrentProcessId())->IsElevated();
			}
		}

		size_t EnumProcesses(bool includeThreads, uint32_t pid) {
			std::vector<std::shared_ptr<TProcessInfo>> processes;
			processes.reserve(_processes.empty() ? 512 : _processes.size() + 10);
			ProcessMap processesByKey;
			processesByKey.reserve(_processes.size() == 0 ? 512 : _processes.size() + 10);
			_processesById.clear();
			_processesById.reserve(_processes.capacity());

			_newProcesses.clear();

			ThreadMap threadsByKey;
			if (includeThreads) {
				threadsByKey.reserve(4096);
				_newThreads.clear();
				if (_threads.empty())
					_newThreads.reserve(4096);
				_threads.clear();
				_threadsById.clear();
			}

			int size = 1 << 22;
			wil::unique_virtualalloc_ptr<BYTE> buffer((BYTE*)::VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			if (!buffer)
				return 0;

			ULONG len;

			// get timing info as close as possible to the API call

			LARGE_INTEGER ticks;
			::QueryPerformanceCounter(&ticks);
			auto delta = ticks.QuadPart - _prevTicks.QuadPart;

			NTSTATUS status;
			bool extended;
			if (s_isElevated && IsWindows8OrGreater()) {
				status = NtQuerySystemInformation(SystemFullProcessInformation, buffer.get(), size, &len);
				extended = true;
			}
			else {
				extended = false;
				status = NtQuerySystemInformation(SystemExtendedProcessInformation, buffer.get(), size, &len);
			}
			if (NT_SUCCESS(status)) {
				auto p = reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>(buffer.get());

				for (;;) {
					if (pid == 0 || pid == HandleToULong(p->UniqueProcessId)) {
						ProcessOrThreadKey key = { p->CreateTime.QuadPart, HandleToULong(p->UniqueProcessId) };
						std::shared_ptr<TProcessInfo> pi;
						if (auto it = _processesByKey.find(key); it == _processesByKey.end()) {
							// new process
							pi = BuildProcessInfo(p, includeThreads, threadsByKey, delta, pi, extended);
							_newProcesses.push_back(pi);
							pi->CPU = 0;
						}
						else {
							const auto& pi2 = it->second;
							auto cpu = delta == 0 ? 0 : (int32_t)((p->KernelTime.QuadPart + p->UserTime.QuadPart - pi2->UserTime - pi2->KernelTime) * 1000000 / delta / s_totalProcessors);
							pi = BuildProcessInfo(p, includeThreads, threadsByKey, delta, pi2, extended);
							pi->CPU = cpu;

							// remove from known processes
							_processesByKey.erase(key);
						}
						processes.push_back(pi);
						//
						// add process to maps
						//
						processesByKey.insert({ key, pi });
						_processesById.insert({ pi->Id, pi });
					}
					if (p->NextEntryOffset == 0)
						break;
					p = reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>((BYTE*)p + p->NextEntryOffset);
				}
			}
			_processes = std::move(processes);

			//
			// remaining processes are terminated ones
			//
			_terminatedProcesses.clear();
			_terminatedProcesses.reserve(_processesByKey.size());
			for (const auto& [key, pi] : _processesByKey)
				_terminatedProcesses.push_back(pi);

			_processesByKey = std::move(processesByKey);

			if (includeThreads) {
				_terminatedThreads.clear();
				_terminatedThreads.reserve(_threadsByKey.size());
				for (const auto& [key, ti] : _threadsByKey)
					_terminatedThreads.push_back(ti);

				_threadsByKey = std::move(threadsByKey);
			}

			_prevTicks = ticks;

			return static_cast<uint32_t>(_processes.size());
		}

		[[nodiscard]] std::vector<std::shared_ptr<TProcessInfo>> const& GetProcesses() const {
			return _processes;
		}

		[[nodiscard]] std::shared_ptr<TProcessInfo> GetProcessInfo(int index) const {
			return _processes[index];
		}

		[[nodiscard]] std::shared_ptr<TProcessInfo> GetProcessById(uint32_t pid) const {
			auto it = _processesById.find(pid);
			return it == _processesById.end() ? nullptr : it->second;
		}

		[[nodiscard]] std::shared_ptr<TProcessInfo> GetProcessByKey(const ProcessOrThreadKey& key) const {
			auto it = _processesByKey.find(key);
			return it == _processesByKey.end() ? nullptr : it->second;
		}

		std::vector<std::shared_ptr<TThreadInfo>> const& GetThreads() const {
			return _threads;
		}

		[[nodiscard]] size_t GetProcessCount() const {
			return _processes.size();
		}

		[[nodiscard]] std::shared_ptr<TThreadInfo> GetThreadInfo(int index) const {
			return _threads[index];
		}

		[[nodiscard]] std::shared_ptr<TThreadInfo> GetThreadByKey(const ProcessOrThreadKey& key) const {
			auto it = _threadsByKey.find(key);
			return it == _threadsByKey.end() ? nullptr : it->second;
		}

		[[nodiscard]] const std::vector<std::shared_ptr<TThreadInfo>>& GetTerminatedThreads() const {
			return _terminatedThreads;
		}

		[[nodiscard]] const std::vector<std::shared_ptr<TThreadInfo>>& GetNewThreads() const {
			return _newThreads;
		}

		[[nodiscard]] size_t GetThreadCount() const {
			return _threads.size();
		}

		[[nodiscard]] std::vector<std::pair<std::shared_ptr<TProcessInfo>, int>> BuildProcessTree() {
			std::vector<std::pair<std::shared_ptr<TProcessInfo>, int>> tree;
			auto count = EnumProcesses(false, 0);
			tree.reserve(count);

			auto map = _processesById;
			for (auto& p : _processes) {
				auto it = _processesById.find(p->ParentId);
				if (p->ParentId == 0 || it == _processesById.end() || (it != _processesById.end() && it->second->CreateTime > p->CreateTime)) {
					// root
					DbgPrint((PSTR)"Root: %ws (%u) (Parent: %u)\n", p->GetImageName().c_str(), p->Id, p->ParentId);
					tree.push_back(std::make_pair(p, 0));
					map.erase(p->Id);
					if (p->Id == 0)
						continue;
					auto children = FindChildren(map, p.get(), 1);
					for (auto& child : children)
						tree.push_back(std::make_pair(_processesById[child.first], child.second));
				}
			}
			return tree;
		}

		std::vector<std::pair<uint32_t, int>> FindChildren(std::unordered_map<uint32_t, std::shared_ptr<TProcessInfo>>& map, TProcessInfo* parent, int indent) {
			std::vector<std::pair<uint32_t, int>> children;
			for (auto& p : _processes) {
				if (p->ParentId == parent->Id && p->CreateTime > parent->CreateTime) {
					children.push_back(std::make_pair(p->Id, indent));
					map.erase(p->Id);
					auto children2 = FindChildren(map, p.get(), indent + 1);
					children.insert(children.end(), children2.begin(), children2.end());
				}
			}
			return children;
		}

		private:
			using ProcessMap = std::unordered_map<ProcessOrThreadKey, std::shared_ptr<TProcessInfo>>;
			using ThreadMap = std::unordered_map<ProcessOrThreadKey, std::shared_ptr<TThreadInfo>>;

			std::shared_ptr<TProcessInfo> BuildProcessInfo(const SYSTEM_PROCESS_INFORMATION* info, bool includeThreads,
				ThreadMap& threadsByKey, int64_t delta, std::shared_ptr<TProcessInfo> pi, bool extended) {
				if (pi == nullptr) {
					pi = std::make_shared<TProcessInfo>();
					pi->Id = HandleToULong(info->UniqueProcessId);
					pi->SessionId = info->SessionId;
					pi->CreateTime = info->CreateTime.QuadPart;
					pi->Key.Created = pi->CreateTime;
					pi->Key.Id = pi->Id;
					pi->ParentId = HandleToULong(info->InheritedFromUniqueProcessId);
					pi->ClearThreads();
					auto name = info->UniqueProcessId == 0 ? L"(Idle)" : std::wstring(info->ImageName.Buffer, info->ImageName.Length / sizeof(WCHAR));
					if (extended && info->UniqueProcessId) {
						auto ext = (SYSTEM_PROCESS_INFORMATION_EXTENSION*)((BYTE*)info +
							FIELD_OFFSET(SYSTEM_PROCESS_INFORMATION, Threads) + sizeof(SYSTEM_EXTENDED_THREAD_INFORMATION) * info->NumberOfThreads);
						pi->JobObjectId = ext->JobObjectId;
						auto index = name.rfind(L'\\');
						::memcpy(pi->UserSid, (BYTE*)info + ext->UserSidOffset, sizeof(pi->UserSid));
						pi->_processName = index == std::wstring::npos ? name : name.substr(index + 1);
						pi->_nativeImagePath = name;
						if (ext->PackageFullNameOffset > 0) {
							pi->_packageFullName = (const wchar_t*)((BYTE*)ext + ext->PackageFullNameOffset);
						}
					}
					else {
						pi->_processName = name;
						pi->JobObjectId = 0;
					}
				}

				pi->ThreadCount = info->NumberOfThreads;
				pi->BasePriority = info->BasePriority;
				pi->UserTime = info->UserTime.QuadPart;
				pi->KernelTime = info->KernelTime.QuadPart;
				pi->HandleCount = info->HandleCount;
				pi->PageFaultCount = info->PageFaultCount;
				pi->PeakThreads = info->NumberOfThreadsHighWatermark;
				pi->PeakVirtualSize = info->PeakVirtualSize;
				pi->VirtualSize = info->VirtualSize;
				pi->WorkingSetSize = info->WorkingSetSize;
				pi->PeakWorkingSetSize = info->PeakWorkingSetSize;
				pi->PagefileUsage = info->PagefileUsage;
				pi->OtherOperationCount = info->OtherOperationCount.QuadPart;
				pi->ReadOperationCount = info->ReadOperationCount.QuadPart;
				pi->WriteOperationCount = info->WriteOperationCount.QuadPart;
				pi->HardFaultCount = info->HardFaultCount;
				pi->OtherTransferCount = info->OtherTransferCount.QuadPart;
				pi->ReadTransferCount = info->ReadTransferCount.QuadPart;
				pi->WriteTransferCount = info->WriteTransferCount.QuadPart;
				pi->PeakPagefileUsage = info->PeakPagefileUsage;
				pi->CycleTime = info->CycleTime;
				pi->NonPagedPoolUsage = info->QuotaNonPagedPoolUsage;
				pi->PagedPoolUsage = info->QuotaPagedPoolUsage;
				pi->PeakNonPagedPoolUsage = info->QuotaPeakNonPagedPoolUsage;
				pi->PeakPagedPoolUsage = info->QuotaPeakPagedPoolUsage;
				pi->PrivatePageCount = info->PrivatePageCount;

				if (includeThreads && pi->Id > 0) {
					auto threadCount = info->NumberOfThreads;
					for (ULONG i = 0; i < threadCount; i++) {
						auto tinfo = (SYSTEM_EXTENDED_THREAD_INFORMATION*)info->Threads + i;
						const auto& baseInfo = tinfo->ThreadInfo;
						ProcessOrThreadKey key = { baseInfo.CreateTime.QuadPart, HandleToULong(baseInfo.ClientId.UniqueThread) };
						std::shared_ptr<TThreadInfo> thread;
						std::shared_ptr<TThreadInfo> ti2;
						bool newobject = true;
						int64_t cpuTime;
						if (auto it = _threadsByKey.find(key); it != _threadsByKey.end()) {
							thread = it->second;
							cpuTime = thread->UserTime + thread->KernelTime;
							newobject = false;
						}
						if (newobject) {
							thread = std::make_shared<TThreadInfo>();
							thread->_processName = pi->GetImageName();
							thread->Id = HandleToULong(baseInfo.ClientId.UniqueThread);
							thread->ProcessId = HandleToULong(baseInfo.ClientId.UniqueProcess);
							thread->CreateTime = baseInfo.CreateTime.QuadPart;
							thread->StartAddress = baseInfo.StartAddress;
							thread->StackBase = tinfo->StackBase;
							thread->StackLimit = tinfo->StackLimit;
							thread->Win32StartAddress = tinfo->Win32StartAddress;
							thread->TebBase = tinfo->TebBase;
							thread->Key = key;
						}
						thread->KernelTime = baseInfo.KernelTime.QuadPart;
						thread->UserTime = baseInfo.UserTime.QuadPart;
						thread->Priority = baseInfo.Priority;
						thread->BasePriority = baseInfo.BasePriority;
						thread->ThreadState = (ThreadState)baseInfo.ThreadState;
						thread->WaitReason = (WaitReason)baseInfo.WaitReason;
						thread->WaitTime = baseInfo.WaitTime;
						thread->ContextSwitches = baseInfo.ContextSwitches;

						pi->AddThread(thread);

						_threads.push_back(thread);
						if (newobject) {
							// new thread
							thread->CPU = 0;
							_newThreads.push_back(thread);
						}
						else {
							thread->CPU = delta == 0 ? 0 : (int32_t)((thread->KernelTime + thread->UserTime - cpuTime) * 1000000LL / delta / 1/*_totalProcessors*/);
							_threadsByKey.erase(thread->Key);
						}
						threadsByKey.insert(std::make_pair(thread->Key, thread));
						_threadsById.insert(std::make_pair(thread->Id, thread));
					}
				}
				return pi;
			}

			// processes

			std::unordered_map<uint32_t, std::shared_ptr<TProcessInfo>> _processesById;
			std::vector<std::shared_ptr<TProcessInfo>> _processes;
			std::vector<std::shared_ptr<TProcessInfo>> _terminatedProcesses;
			std::vector<std::shared_ptr<TProcessInfo>> _newProcesses;
			ProcessMap _processesByKey;

			// threads

			std::vector<std::shared_ptr<TThreadInfo>> _threads;
			std::vector<std::shared_ptr<TThreadInfo>> _newThreads;
			std::vector<std::shared_ptr<TThreadInfo>> _terminatedThreads;
			std::unordered_map<uint32_t, std::shared_ptr<TThreadInfo>> _threadsById;
			ThreadMap _threadsByKey;

			LARGE_INTEGER _prevTicks{};
			inline static uint32_t s_totalProcessors;
			inline static bool s_isElevated;

	};
#ifdef WINSYS_NAMESPACE
}
#endif

