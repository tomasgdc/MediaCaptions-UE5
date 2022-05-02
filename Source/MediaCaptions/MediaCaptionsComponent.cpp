#include "MediaCaptionsComponent.h"

//Engine Includes
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"

#include "Misc/ScopeLock.h"
#include "Sound/AudioSettings.h"
#include "UObject/UObjectGlobals.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"
#include "IMediaOverlaySample.h"
#include "IMediaAudioSample.h"
#include "IMediaPlayer.h"
#include "MediaAudioResampler.h"



DECLARE_FLOAT_COUNTER_STAT(TEXT("MediaUtils MediaCaptionsComponent SampleTime"), STAT_MediaUtils_MediaCaptionsComponentSampleTime, STATGROUP_Media);
DECLARE_DWORD_COUNTER_STAT(TEXT("MediaUtils MediaCaptionsComponent Queued"), STAT_Media_CaptionsCompQueued, STATGROUP_Media);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(MEDIA_API, MediaStreaming);


/* UIduMediaCaptionsComponent structors
 *****************************************************************************/

UMediaCaptionsComponent::UMediaCaptionsComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CachedRate(0.0f)
	, CachedTime(FTimespan::Zero())
	, LastPlaySampleTime(FTimespan::MinValue())
{
	PrimaryComponentTick.bCanEverTick = true;
	bAutoActivate = true;
}


UMediaCaptionsComponent::~UMediaCaptionsComponent()
{

}

UMediaPlayer* UMediaCaptionsComponent::GetMediaPlayer() const
{
	return CurrentPlayer.Get();
}


void UMediaCaptionsComponent::SetMediaPlayer(UMediaPlayer* NewMediaPlayer)
{
	CurrentPlayer = NewMediaPlayer;
}

#if WITH_EDITOR

void UMediaCaptionsComponent::SetDefaultMediaPlayer(UMediaPlayer* NewMediaPlayer)
{
	MediaPlayer = NewMediaPlayer;
	CurrentPlayer = MediaPlayer;
}

#endif


void UMediaCaptionsComponent::UpdatePlayer()
{
	UMediaPlayer* CurrentPlayerPtr = CurrentPlayer.Get();
	if (CurrentPlayerPtr == nullptr)
	{
		CachedRate = 0.0f;
		CachedTime = FTimespan::Zero();

		FScopeLock Lock(&CriticalSection);
		SampleQueue.Reset();

		return;
	}

	// create a new sample queue if the player changed
	TSharedRef<FMediaPlayerFacade, ESPMode::ThreadSafe> PlayerFacade = CurrentPlayerPtr->GetPlayerFacade();
	if (PlayerFacade != CurrentPlayerFacade)
	{
		if (IsActive())
		{

			const auto NewSampleQueue = MakeShared<FMediaCaptionSampleQueue, ESPMode::ThreadSafe>();
			PlayerFacade->AddCaptionSampleSink(NewSampleQueue);
			{
				FScopeLock Lock(&CriticalSection);
				SampleQueue = NewSampleQueue;
			}

			CurrentPlayerFacade = PlayerFacade;
		}
	}
	else
	{
		// Here, we have a CurrentPlayerFacade set which means are also have a valid FMediaCaptionSampleQueue set
		if (!IsActive())
		{
			FScopeLock Lock(&CriticalSection);
			SampleQueue.Reset();
			CurrentPlayerFacade.Reset();
		}
	}

	// caching play rate and time
	CachedRate = PlayerFacade->GetRate();
	CachedTime = PlayerFacade->GetTime();
}


void UMediaCaptionsComponent::OnGenerateMediaCaptions()
{
	//CSV_SCOPED_TIMING_STAT(MediaStreaming, UMediaCaptionsComponent_OnGenerateMediaCaptions);

	TSharedPtr<FMediaCaptionSampleQueue, ESPMode::ThreadSafe> PinnedSampleQueue;
	{
		FScopeLock Lock(&CriticalSection);
		PinnedSampleQueue = SampleQueue;
	}

	// We have an input queue and are actively playing?
	if (PinnedSampleQueue.IsValid() && (CachedRate != 0.0f))
	{
		const float Rate = CachedRate.Load();
		const FTimespan Time = CachedTime.Load();

		TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe> Sample;
		if (PinnedSampleQueue->Num() > 0)
		{
			PinnedSampleQueue->Dequeue(Sample);

			FMediaTimeStamp timestamp = Sample->GetTime();
			FText text = Sample->GetText();

			LastPlaySampleTime = timestamp.Time;
			OnNewCaptionText.Broadcast(text);

			SET_FLOAT_STAT(STAT_MediaUtils_MediaCaptionsComponentSampleTime, timestamp.Time.GetTotalSeconds());
			SET_DWORD_STAT(STAT_Media_CaptionsCompQueued, PinnedSampleQueue->Num());
		}
	}
	else
	{
		LastPlaySampleTime = FTimespan::MinValue();
	}
}

/* UActorComponent interface
 *****************************************************************************/

void UMediaCaptionsComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	if (SpriteComponent != nullptr)
	{
		SpriteComponent->SpriteInfo.Category = TEXT("Captions");
		SpriteComponent->SpriteInfo.DisplayName = NSLOCTEXT("SpriteCategory", "Captions", "Captions");

		if (bAutoActivate)
		{
			SpriteComponent->SetSprite(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/AudioIcons/S_AudioComponent_AutoActivate.S_AudioComponent_AutoActivate")));
		}
		else
		{
			SpriteComponent->SetSprite(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/AudioIcons/S_AudioComponent.S_AudioComponent")));
		}
	}
#endif
}

void UMediaCaptionsComponent::OnUnregister()
{
	{
		FScopeLock Lock(&CriticalSection);
		SampleQueue.Reset();
	}

	CurrentPlayerFacade.Reset();
	Super::OnUnregister();
}


void UMediaCaptionsComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdatePlayer();
	OnGenerateMediaCaptions();
}


/* USceneComponent interface
 *****************************************************************************/

void UMediaCaptionsComponent::Activate(bool bReset)
{
	if (bReset || ShouldActivate())
	{
		SetComponentTickEnabled(true);
	}

	Super::Activate(bReset);
}


void UMediaCaptionsComponent::Deactivate()
{
	if (!ShouldActivate())
	{
		SetComponentTickEnabled(false);
		{
			FScopeLock Lock(&CriticalSection);
			SampleQueue.Reset();
		}
		CurrentPlayerFacade.Reset();
	}

	Super::Deactivate();
}


/* UObject interface
 *****************************************************************************/

void UMediaCaptionsComponent::PostInitProperties()
{
	Super::PostInitProperties();
}


void UMediaCaptionsComponent::PostLoad()
{
	Super::PostLoad();

	CurrentPlayer = MediaPlayer;
}


#if WITH_EDITOR

void UMediaCaptionsComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName MediaPlayerName = GET_MEMBER_NAME_CHECKED(UMediaCaptionsComponent, MediaPlayer);

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged != nullptr)
	{
		const FName PropertyName = PropertyThatChanged->GetFName();

		if (PropertyName == MediaPlayerName)
		{
			CurrentPlayer = MediaPlayer;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif //WITH_EDITOR