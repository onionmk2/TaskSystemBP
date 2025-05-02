﻿#include "TSBFunctionLibrary.h"

#include "TSBEngineSubsystem.h"
#include "TSBLogChannels.h"
#include "TSBPipe.h"
#include "TSBTask.h"
#include "TSBTaskObject.h"
#include "Blueprint/BlueprintExceptionInfo.h"

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#endif

using namespace UE::Tasks;
using namespace TaskSystemBP;

#define LOCTEXT_NAMESPACE "TaskSystemBP"

FTSBTaskHandle UTSBFunctionLibrary::LaunchTaskClass(UObject* WorldContextObject,
                                                    const TSubclassOf<UTSBTaskObject>& TaskClass,
                                                    const TArray<FTSBTaskHandle>& Prerequisites,
                                                    const FTSBPipe& Pipe,
                                                    const ETSBThreadingPolicy InThreadingPolicy)
{
	UTSBTaskObject* CDO = TaskClass->GetDefaultObject<UTSBTaskObject>();
	if (!IsValid(CDO))
	{
		return FTSBTaskHandle{};
	}

	const ETSBInstancingPolicy& InstancingPolicy = CDO->InstancingPolicy;
	if (InstancingPolicy == ETSBInstancingPolicy::NoInstance)
	{
		return LaunchTaskObject(CDO, Prerequisites, Pipe, InThreadingPolicy);
	}
	if (InstancingPolicy == ETSBInstancingPolicy::InstantiatePerExecution)
	{
		UTSBTaskObject* TaskObject = NewObject<UTSBTaskObject>(WorldContextObject, TaskClass);
		return LaunchTaskObject(TaskObject, Prerequisites, Pipe, InThreadingPolicy);
	}

	return FTSBTaskHandle{};
}

FTSBTaskHandle UTSBFunctionLibrary::LaunchTaskObject(UTSBTaskObject* TaskObject,
                                                     const TArray<FTSBTaskHandle>& Prerequisites,
                                                     const FTSBPipe& Pipe,
                                                     const ETSBThreadingPolicy InThreadingPolicy)
{
	if (!IsValid(TaskObject))
	{
		return FTSBTaskHandle{};
	}

	auto InternalTask = [TaskObject, InThreadingPolicy]
	{
		if (!IsValid(TaskObject))
		{
			return;
		}
#if WITH_EDITOR
		if (UTSBEngineSubsystem::IsPaused())
		{
			UTSBEngineSubsystem* Subsystem = GEngine->GetEngineSubsystem<UTSBEngineSubsystem>();
			AddNested(Launch(*TaskObject->GetName(), [TaskObject]
			{
				if (!IsValid(TaskObject))
				{
					return;
				}
				TaskObject->ExecuteTask();
			}, Subsystem->WaitForUnpauseTask(), ETaskPriority::Normal, ToTaskPriority(InThreadingPolicy)));
			return;
		}
#endif
		TaskObject->ExecuteTask();
	};

	const FTask Task = Pipe.Pipe.IsValid()
		                   ? Pipe.Pipe->Launch(*TaskObject->GetName(), MoveTemp(InternalTask),
		                                       ToTaskArray(Prerequisites), ETaskPriority::Normal,
		                                       ToTaskPriority(InThreadingPolicy))
		                   : Launch(*TaskObject->GetName(), MoveTemp(InternalTask), ToTaskArray(Prerequisites),
		                            ETaskPriority::Normal, ToTaskPriority(InThreadingPolicy));

	const auto ReturnTask = Launch(UE_SOURCE_LOCATION, [TaskObject]
	{
		if (IsValid(TaskObject))
		{
			return TaskObject->GetTaskResult();
		}
		return FTSBTaskResult{};
	}, Task, ETaskPriority::Normal, EExtendedTaskPriority::Inline);

	return FTSBTaskHandle{ReturnTask};
}

FTSBTaskHandle UTSBFunctionLibrary::LaunchTaskEventWithResult(const FTSBTaskWithResult& TaskEvent,
                                                              const TArray<FTSBTaskHandle>& Prerequisites,
                                                              const FTSBPipe& Pipe,
                                                              const ETSBThreadingPolicy InThreadingPolicy)
{
	if (!TaskEvent.IsBound())
	{
		return FTSBTaskHandle{};
	}

	TSharedPtr<FTSBTaskResult> ResultHolder = MakeShared<FTSBTaskResult>();

	auto InternalTask = [TaskEvent, ResultHolder, InThreadingPolicy]() mutable
	{
#if WITH_EDITOR
		if (UTSBEngineSubsystem::IsPaused())
		{
			UTSBEngineSubsystem* Subsystem = GEngine->GetEngineSubsystem<UTSBEngineSubsystem>();
			AddNested(Launch(UE_SOURCE_LOCATION, [TaskEvent, ResultHolder]() mutable
			{
				if (TaskEvent.IsBound())
				{
					*ResultHolder = TaskEvent.Execute();
				}
			}, Subsystem->WaitForUnpauseTask(), ETaskPriority::Normal, ToTaskPriority(InThreadingPolicy)));
			return;
		}
#endif
		*ResultHolder = TaskEvent.Execute();
	};

	const FTask MainTask = Pipe.Pipe.IsValid()
		                       ? Pipe.Pipe->Launch(*TaskEvent.GetFunctionName().ToString(), MoveTemp(InternalTask),
		                                           ToTaskArray(Prerequisites), ETaskPriority::Normal,
		                                           ToTaskPriority(InThreadingPolicy))
		                       : Launch(*TaskEvent.GetFunctionName().ToString(), MoveTemp(InternalTask),
		                                ToTaskArray(Prerequisites), ETaskPriority::Normal,
		                                ToTaskPriority(InThreadingPolicy));

	const auto ReturnTask = Launch(UE_SOURCE_LOCATION, [ResultHolder]
	{
		if (ResultHolder.IsValid())
		{
			return *ResultHolder;
		}
		return FTSBTaskResult{};
	}, MainTask, ETaskPriority::Normal, EExtendedTaskPriority::Inline);

	return FTSBTaskHandle{ReturnTask};
}


FTSBTaskHandle UTSBFunctionLibrary::LaunchTaskEvent(const FTSBTask& TaskEvent,
                                                    const TArray<FTSBTaskHandle>& Prerequisites,
                                                    const FTSBPipe& Pipe,
                                                    const ETSBThreadingPolicy InThreadingPolicy)
{
	if (!TaskEvent.IsBound())
	{
		return FTSBTaskHandle{};
	}

	auto InternalTask = [TaskEvent, InThreadingPolicy, Pipe]()
	{
#if WITH_EDITOR
		if (UTSBEngineSubsystem::IsPaused())
		{
			UTSBEngineSubsystem* Subsystem = GEngine->GetEngineSubsystem<UTSBEngineSubsystem>();
			if (Pipe.Pipe.IsValid())
			{
				Pipe.Pipe->Launch(UE_SOURCE_LOCATION, [TaskEvent]
				{
					TaskEvent.ExecuteIfBound();
				}, Subsystem->WaitForUnpauseTask(), ETaskPriority::Normal, ToTaskPriority(InThreadingPolicy));
			}
			else
			{
				Launch(UE_SOURCE_LOCATION, [TaskEvent]
				{
					TaskEvent.ExecuteIfBound();
				}, Subsystem->WaitForUnpauseTask(), ETaskPriority::Normal, ToTaskPriority(InThreadingPolicy));
			}
		}
		else
#endif
		{
			TaskEvent.Execute();
		}
	};

	if (Pipe.Pipe.IsValid())
	{
		const FTask Task = Pipe.Pipe->Launch(*TaskEvent.GetFunctionName().ToString(),
		                                     MoveTemp(InternalTask),
		                                     ToTaskArray(Prerequisites),
		                                     ETaskPriority::Normal, ToTaskPriority(InThreadingPolicy));
		return FTSBTaskHandle{Task};
	}

	const FTask Task = Launch(*TaskEvent.GetFunctionName().ToString(), MoveTemp(InternalTask),
	                          ToTaskArray(Prerequisites),
	                          ETaskPriority::Normal, ToTaskPriority(InThreadingPolicy));
	return FTSBTaskHandle{Task};
}

void UTSBFunctionLibrary::AddNestedTask(const FTSBTaskHandle& ChildTask)
{
	AddNested(*ChildTask.Handle);
}

void UTSBFunctionLibrary::AddPrerequisite(FTSBTaskHandle& Event, const FTSBTaskHandle& Prerequisite)
{
	if (Event.TaskType == ETSBTaskType::Event && Event.Handle.IsValid() && Prerequisite.Handle.IsValid())
	{
		static_cast<FTaskEvent*>(Event.Handle.Get())->AddPrerequisites(*Prerequisite.Handle);
	}
}

void UTSBFunctionLibrary::Trigger(FTSBTaskHandle& Event)
{
	if (Event.TaskType == ETSBTaskType::Event && Event.Handle.IsValid())
	{
		static_cast<FTaskEvent*>(Event.Handle.Get())->Trigger();
	}
}

void UTSBFunctionLibrary::BindCompletion(const FTSBTaskHandle& Task, const FTSBOnTaskCompleted& OnTaskCompleted)
{
	if (!Task.Handle.IsValid())
	{
		UE_LOG(LogTaskSystemBP, Warning, TEXT("UTSBFunctionLibrary::BindCompletion: Task is invalid"));
		return;
	}

	Launch(UE_SOURCE_LOCATION, [OnTaskCompleted, Task]
	{
		if (OnTaskCompleted.IsBound())
		{
			OnTaskCompleted.Execute(Task);
		}
	}, *Task.Handle, ETaskPriority::Normal, EExtendedTaskPriority::GameThreadNormalPri);
}

bool UTSBFunctionLibrary::GetTaskResult(const FTSBTaskHandle& Task, FTSBTaskResult& OutResult)
{
	if (!Task.Handle.IsValid())
	{
		UE_LOG(LogTaskSystemBP, Warning, TEXT("UTSBFunctionLibrary::GetTaskResult: Task is invalid"));
		return false;
	}

	if (Task.TaskType != ETSBTaskType::TSBResultTask)
	{
		UE_LOG(LogTaskSystemBP, Warning,
		       TEXT("UTSBFunctionLibrary::GetTaskResult: Task is not having a result."));
		return false;
	}

	const TSharedRef<TTask<FTSBTaskResult>> TaskRef = StaticCastSharedRef<TTask<FTSBTaskResult>>(
		Task.Handle.ToSharedRef());
	if (!TaskRef->IsCompleted())
	{
		UE_LOG(LogTaskSystemBP, Warning, TEXT("UTSBFunctionLibrary::GetTaskResult: Task is not completed"));
		return false;
	}

	OutResult = TaskRef->GetResult();
	return true;
}

// FTSBTaskHandle UTSBFunctionLibrary::Any(const TArray<FTSBTaskHandle>& Tasks)
// {
// 	return FTSBTaskHandle{UE::Tasks::Any(Tasks)};
// }

TArray<FTSBTaskHandle> UTSBFunctionLibrary::Conv_HandleToHandleArray(const FTSBTaskHandle& InHandle)
{
	return TArray{InHandle};
}

FTSBTaskHandle UTSBFunctionLibrary::MakeTaskEvent(const FString& InDebugName)
{
	return FTSBTaskHandle{FTaskEvent{*InDebugName}};
}

FTSBPipe UTSBFunctionLibrary::MakePipe(const FString& InDebugName)
{
	return FTSBPipe{*InDebugName};
}

// FTSBCancellationToken UTSBFunctionLibrary::MakeCancellationToken()
// {
// 	return FTSBCancellationToken::MakeCancellationToken();
// }

DEFINE_FUNCTION(UTSBFunctionLibrary::execMakeTaskStructResult)
{
	// Read wildcard Value input.
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FStructProperty* ValueProp = CastField<FStructProperty>(Stack.MostRecentProperty);
	const void* ValuePtr = Stack.MostRecentPropertyAddress;

	P_FINISH;

	if (!ValueProp || !ValuePtr)
	{
		const FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("InstancedStruct_MakeInvalidValueWarning", "Invalid value passed to MakeTaskResult")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);

		P_NATIVE_BEGIN;
			static_cast<FTSBTaskResult*>(RESULT_PARAM)->ResultValue.Reset();
		P_NATIVE_END;
	}
	else
	{
		P_NATIVE_BEGIN;
			static_cast<FTSBTaskResult*>(RESULT_PARAM)->ResultValue.InitializeAs(
				ValueProp->Struct,
				static_cast<const uint8*>(ValuePtr));
		P_NATIVE_END;
	}
}

DEFINE_FUNCTION(UTSBFunctionLibrary::execGetTaskStructResult)
{
	P_GET_ENUM_REF(ETSBTaskResultStatus, ExecResult);
	P_GET_STRUCT_REF(FTSBTaskHandle, TaskHandle);

	// Read wildcard Value input.
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FStructProperty* ValueProp = CastField<FStructProperty>(Stack.MostRecentProperty);
	void* ValuePtr = Stack.MostRecentPropertyAddress;

	P_FINISH;

	ExecResult = ETSBTaskResultStatus::NotValid;

	if (!ValueProp || !ValuePtr)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("InstancedStruct_GetInvalidValueWarning",
			        "Failed to resolve the Value for Get Instanced Struct Value")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else
	{
		P_NATIVE_BEGIN;
			FTSBTaskResult TaskResult;
			if (!UTSBFunctionLibrary::GetTaskResult(TaskHandle, TaskResult))
			{
				ExecResult = ETSBTaskResultStatus::NotValid;
				return;
			}

			if (const FInstancedStruct& InstancedStruct = TaskResult.ResultValue;
				InstancedStruct.IsValid() && InstancedStruct.GetScriptStruct()->IsChildOf(ValueProp->Struct))
			{
				ValueProp->Struct->CopyScriptStruct(ValuePtr, InstancedStruct.GetMemory());
				ExecResult = ETSBTaskResultStatus::Valid;
			}
			else
			{
				ExecResult = ETSBTaskResultStatus::NotValid;
			}
		P_NATIVE_END;
	}
}

#undef LOCTEXT_NAMESPACE
