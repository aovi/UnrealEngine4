// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AsyncLoadingThread.h: Unreal async loading code.
=============================================================================*/
#pragma once

/**
 * Async loading thread. Preloads/serializes packages on async loading thread. Postloads objects on the game thread.
 */
class FAsyncLoadingThread : public FRunnable
{
	friend class FArchiveAsync;

	/** Thread to run the worker FRunnable on */
	FRunnableThread* Thread;
	/** Stops this thread */
	FThreadSafeCounter StopTaskCounter;

	/** [ASYNC/GAME THREAD] Event used to signal there's queued packages to stream */
	FEvent* QueuedRequestsEvent;
	/** [ASYNC/GAME THREAD] Event used to signal loading should be cancelled */
	FEvent* CancelLoadingEvent;
	/** [ASYNC/GAME THREAD] List of queued packages to stream */
	TArray<FAsyncPackageDesc*> QueuedPackages;
	/** [ASYNC/GAME THREAD] Package queue critical section */
	FCriticalSection QueueCritical;
	/** [ASYNC/GAME THREAD] True if the async loading thread received a request to cancel async loading **/
	FThreadSafeBool bShouldCancelLoading;
	/** [ASYNC/GAME THREAD] Event used to signal there's queued packages to stream */
	TArray<FAsyncPackage*> LoadedPackages;	
	/** [ASYNC/GAME THREAD] Critical section for LoadedPackages list */
	FCriticalSection LoadedPackagesCritical;
	/** [GAME THREAD] Event used to signal there's queued packages to stream */
	TArray<FAsyncPackage*> LoadedPackagesToProcess;
	
	/** [ASYNC THREAD] Array of packages that are being preloaded */
	TArray<FAsyncPackage*> AsyncPackages;
	/** We only lock AsyncPackages array to make GetAsyncLoadPercentage thread safe, so we only care about locking Add/Remove operations on the async thread */
	FCriticalSection AsyncPackagesCritical;

	/** [ASYNC/GAME THREAD] Number of package load requests in the async loading queue */
	FThreadSafeCounter QueuedPackagesCounter;
	/** [ASYNC/GAME THREAD] Number of packages being loaded on the async thread and post loaded on the game thread */
	FThreadSafeCounter AsyncLoadingCounter;
	/** [ASYNC/GAME THREAD] Number of packages being loaded on the async thread */
	FThreadSafeCounter AsyncPackagesCounter;

	FThreadSafeCounter AsyncThreadReady;

	/** Async loading thread ID */
	static uint32 AsyncLoadingThreadID;

	/** Thread safe counter used to accumulate cycles spent on blocking. Using stats may generate to many stats messages. */
	static FThreadSafeCounter BlockingCycles;

	FAsyncLoadingThread();
	virtual ~FAsyncLoadingThread();

	/**
	* [ASYNC/GAME THREAD] Finds an existing async package by its name.
	*/
	FORCEINLINE int32 FindPackageByName(TArray<FAsyncPackage*>& InPackages, const FName& PackageName)
	{
		for (int32 PackageIndex = 0; PackageIndex < InPackages.Num(); ++PackageIndex)
		{
			FAsyncPackage* Package = InPackages[PackageIndex];
			if (Package->GetPackageName() == PackageName)
			{
				return PackageIndex;
			}
		}
		return INDEX_NONE;
	}

public:

	// Begin FRunnable interface.
	virtual bool Init();
	virtual uint32 Run();
	virtual void Stop();
	// End FRunnable interface

	/** Returns the async loading thread singleton */
	static FAsyncLoadingThread& Get();

	/** True if multithreaded async loading should be used. */
	static FORCEINLINE bool IsMultithreaded()
	{
		static struct FAsyncLoadingThreadEnabled
		{
			bool Value;
			FAsyncLoadingThreadEnabled()
			{
				if (FPlatformProperties::RequiresCookedData())
				{
					check(GConfig);
					bool bConfigValue = true;
					GConfig->GetBool(TEXT("Core.System"), TEXT("AsyncLoadingThreadEnabled"), bConfigValue, GEngineIni);
					bool bCommandLineNoAsyncThread = false;
					Value = bConfigValue && FApp::ShouldUseThreadingForPerformance() && !FParse::Param(FCommandLine::Get(), TEXT("NoAsyncLoadingThread"));
				}
				else
				{
					Value = false;
				}
			}
		} AsyncLoadingThreadEnabled;
		return AsyncLoadingThreadEnabled.Value;
	}

	/** Sets the current state of async loading */
	FORCEINLINE void SetIsInAsyncLoadingTick(bool InTick)
	{
		bIsInAsyncLoadingTick = InTick;
	}

	/** Gets the current state of async loading */
	FORCEINLINE bool GetIsInAsyncLoadingTick() const
	{
		return bIsInAsyncLoadingTick;
	}

	/** Returns true if packages are currently being loaded on the async thread */
	FORCEINLINE bool IsAsyncLoadingPackages()
	{
		FPlatformMisc::MemoryBarrier();
		return QueuedPackagesCounter.GetValue() != 0 || AsyncLoadingCounter.GetValue() != 0;
	}

	/** Returns true this codes runs on the async loading thread */
	static FORCEINLINE bool IsInAsyncLoadThread()
	{
		bool bResult = false;
		if (IsMultithreaded())
		{
			// We still need to report we're in async loading thread even if 
			// we're on game thread but inside of async loading code (PostLoad mostly)
			// to make it behave exactly like the non-threaded version
			bResult = FPlatformTLS::GetCurrentThreadId() == AsyncLoadingThreadID || 
				(IsInGameThread() && FAsyncLoadingThread::Get().bIsInAsyncLoadingTick);
		}
		else
		{
			bResult = IsInGameThread() && FAsyncLoadingThread::Get().bIsInAsyncLoadingTick;
		}
		return bResult;
	}

	/** Returns the number of async packages that are currently being processed */
	FORCEINLINE int32 GetAsyncPackagesCount()
	{
		FPlatformMisc::MemoryBarrier();
		return AsyncLoadingCounter.GetValue();
	}

	/**
	* [ASYNC THREAD] Finds an existing async package in the AsyncPackages by its name.
	*
	* @param PackageName async package name.
	* @return Index of the async package in AsyncPackages array or INDEX_NONE if not found.
	*/
	FORCEINLINE int32 FindAsyncPackage(const FName& PackageName)
	{
		checkSlow(IsInAsyncLoadThread());
		return FindPackageByName(AsyncPackages, PackageName);
	}

	/**
	* [ASYNC THREAD] Inserts package to queue according to priority.
	*
	* @param PackageName async package name.
	*/
	void InsertPackage(FAsyncPackage* Package)
	{
		checkSlow(IsInAsyncLoadThread());

		// Incremented on the Async Thread, decremented on the game thread
		AsyncLoadingCounter.Increment();

		// Incemented and decremented on the AsyncThread
		AsyncPackagesCounter.Increment();

		// Insert new package keeping descending priority order in AsyncPackages
		auto InsertIndex = AsyncPackages.IndexOfByPredicate([Package](const FAsyncPackage* Element)
		{
			return Element->GetPriority() <= Package->GetPriority();
		});

		InsertIndex = InsertIndex == -1 ? AsyncPackages.Num() : InsertIndex;
		{
			FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
			AsyncPackages.InsertUninitialized(InsertIndex);
			AsyncPackages[InsertIndex] = Package;
		}
	}

	/**
	* [ASYNC THREAD] Finds an existing async package in the LoadedPackages by its name.
	*
	* @param PackageName async package name.
	* @return Index of the async package in LoadedPackages array or INDEX_NONE if not found.
	*/
	FORCEINLINE int32 FindLoadedPackage(const FName& PackageName)
	{
		checkSlow(IsInAsyncLoadThread());
		return FindPackageByName(LoadedPackages, PackageName);
	}

	/**
	* [ASYNC/GAME THREAD] Queues a package for streaming.
	*
	* @param Package package descriptor.
	*/
	void QueuePackage(const FAsyncPackageDesc& Package);

	/**
	* [GAME THREAD] Cancels streaming
	*/
	void CancelAsyncLoading();

	/**
	* [ASYNC/GAME THREAD] Queues a package for streaming.
	*
	* @param Package package descriptor.
	*/
	FORCEINLINE FAsyncPackage* GetPackage(int32 PackageIndex)
	{
		checkSlow(IsInAsyncLoadThread());
		return AsyncPackages[PackageIndex];
	}

	/**
	* [ASYNC* THREAD] Loads all packages
	*
	* @param OutPackagesProcessed Number of packages processed in this call.
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param ExcludeType Packages to exclude.
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type ProcessAsyncLoading(int32& OutPackagesProcessed, bool bUseTimeLimit = false, bool bUseFullTimeLimit = false, float TimeLimit = 0.0f, FName ExcludeType = NAME_None);

	/**
	* [GAME THREAD] Ticks game thread side of async loading.
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param ExcludeType Packages to exclude.
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type TickAsyncLoading(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit, FName ExcludeType);

	/**
	* [ASYNC THREAD] Main thread loop
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	*/
	void TickAsyncThread(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit);

	/** Initializes async loading thread */
	void InitializeAsyncThread();

	/** 
	 * [GAME THREAD] Gets the load percentage of the specified package
	 * @param PackageName Name of the package to return async load percentage for
	 * @return Percentage (0-100) of the async package load or -1 of package has not been found
	 */
	float GetAsyncLoadPercentage(const FName& PackageName);

private:

	/**
	* [GAME THREAD] Performs game-thread specific operations on loaded packages (not-thread-safe PostLoad, callbacks)
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param ExcludeType Packages to exclude.
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type ProcessLoadedPackages(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit, FName ExcludeType);

	/**
	* [ASYNC THREAD] Creates async packages from the queued requests
	*/
	int32 CreateAsyncPackagesFromQueue();

	/**
	* [ASYNC THREAD] Adds a package to a list of packages that have finished loading on the async thread
	*/
	FORCEINLINE void AddToLoadedPackages(FAsyncPackage* Package)
	{
		FScopeLock LoadedLock(&LoadedPackagesCritical);
		LoadedPackages.Add(Package);
	}

	/** Cancels async loading internally */
	void CancelAsyncLoadingInternal();

	/** True if async loading is currently being ticked, mostly used by singlethreaded ticking */
	bool bIsInAsyncLoadingTick;
};
