// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "MediaSampleQueue.h"
#include "Misc/Timespan.h"
#include "Templates/Atomic.h"
#include "Templates/SharedPointer.h"
#include "MediaCaptionsComponent.generated.h"

class FMediaPlayerFacade;
class IMediaAudioSample;
class IMediaPlayer;
class UMediaPlayer;


/** Type definition for overlay sample queue. */
typedef TMediaSampleQueue<class IMediaOverlaySample> FMediaCaptionSampleQueue;

// ------------------------------------------------------------------------                       
// Events and Delegates
// ------------------------------------------------------------------------
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMediaCaptionEventDelegate_OnNewCaptionText, FText, CaptionText);

UCLASS(ClassGroup = Media, editinlinenew, meta = (BlueprintSpawnableComponent))
class MEDIACAPTIONS_API UMediaCaptionsComponent : public USceneComponent
{
	GENERATED_BODY()

public:

	/**
	 * Create and initialize a new instance.
	 *
	 * @param ObjectInitializer Initialization parameters.
	 */
	UMediaCaptionsComponent(const FObjectInitializer& ObjectInitializer);

	/** Virtual destructor. */
	~UMediaCaptionsComponent();

public:


	/**
	 * Get the media player that provides the overlay samples.
	 *
	 * @return The component's media player, or nullptr if not set.
	 * @see SetMediaPlayer
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaCaptionsComponent")
	UMediaPlayer* GetMediaPlayer() const;

	/**
	 * Set the media player that provides the overlay samples.
	 *
	 * @param NewMediaPlayer The player to set.
	 * @see GetMediaPlayer
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaCaptionsComponent")
	void SetMediaPlayer(UMediaPlayer* NewMediaPlayer);

public:

	void UpdatePlayer();

	void OnGenerateMediaCaptions();

#if WITH_EDITOR
	/**
	 * Set the component's default media player property.
	 *
	 * @param NewMediaPlayer The player to set.
	 * @see SetMediaPlayer
	 */
	void SetDefaultMediaPlayer(UMediaPlayer* NewMediaPlayer);
#endif

protected:

	//~ UActorComponent interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:

	//~ USceneComponent interface

	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;

public:

	//~ UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:

	// ------------------------------------------------------------------------                       
	// Events and Delegates
	// ------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Media|MediaCaptionsComponent|Assignable Events")
	FMediaCaptionEventDelegate_OnNewCaptionText OnNewCaptionText;

protected:

	/**
	 * The media player asset associated with this component.
	 *
	 * This property is meant for design-time convenience. To change the
	 * associated media player at run-time, use the SetMediaPlayer method.
	 *
	 * @see SetMediaPlayer
	 */
	UPROPERTY(EditAnywhere, Category = "Media")
	UMediaPlayer* MediaPlayer;

private:

	/** The player's current play rate (cached for use on audio thread). */
	TAtomic<float> CachedRate;

	/** The player's current time (cached for use on audio thread). */
	TAtomic<FTimespan> CachedTime;

	/** Critical section for synchronizing access to PlayerFacadePtr. */
	FCriticalSection CriticalSection;

	/** The player that is currently associated with this component. */
	TWeakObjectPtr<UMediaPlayer> CurrentPlayer;

	/** The player facade that's currently providing texture samples. */
	TWeakPtr<FMediaPlayerFacade, ESPMode::ThreadSafe> CurrentPlayerFacade;

	/** Audio sample queue. */
	TSharedPtr<FMediaCaptionSampleQueue, ESPMode::ThreadSafe> SampleQueue;

	/* Time of last sample played. */
	TAtomic<FTimespan> LastPlaySampleTime;
};
