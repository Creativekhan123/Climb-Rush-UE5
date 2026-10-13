// ============================================================================
// StylizedPostProcessVolume.h
//
// A mobile-optimized, designer-friendly Post Process Volume that lets level
// designers instantly switch between 14 distinct visual styles via an Enum
// in the Details Panel or Blueprints.
//
// Compatible with Unreal Engine 4.27 and Unreal Engine 5.x
//
// Usage:
//   1. Drag AStylizedPostProcessVolume into your level.
//   2. The volume is bUnbound by default, so it affects the entire world.
//   3. Pick a preset from the "Stylized Presets" category in the Details Panel.
//   4. The viewport updates immediately without pressing Play.
//   5. Select "Custom" to leave all values untouched and tweak manually.
//
// Mobile Notes:
//   - Auto Exposure is locked (Min/Max EV100 = 1.0) on all presets to avoid
//     GPU overhead from the exposure adaptation pass on mobile hardware.
//   - Motion Blur and Lens Flares are forced to 0 on all presets.
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/PostProcessVolume.h"
#include "StylizedPostProcessVolume.generated.h"

// ============================================================================
// EPostProcessPreset — the 14 visual style presets exposed to Blueprints and
// the Details Panel via the UENUM reflection system.
// ============================================================================
UENUM(BlueprintType)
enum class EPostProcessPreset : uint8
{
    /** Vibrant candy colours, 1.35x saturation, warm highlights, soft bloom.
     *  Inspired by bright, playful party-game aesthetics. */
    FallGuysCandy       UMETA(DisplayName = "Fall Guys Candy"),

    /** Soft pastel palette, elevated gain, low contrast.
     *  Great for dreamy, child-friendly worlds. */
    PastelToon          UMETA(DisplayName = "Pastel Toon"),

    /** High contrast, boosted magenta + cyan gain, dark shadows.
     *  Ideal for retro synthwave or 80s-inspired levels. */
    NeonSynthwave       UMETA(DisplayName = "Neon Synthwave"),

    /** Warm peach/orange colour tint, soft contrast.
     *  Perfect for golden-hour outdoor scenes. */
    SunsetGlow          UMETA(DisplayName = "Sunset Glow"),

    /** Cool blue shadows, electric highlights, punchy contrast.
     *  For rain-slicked urban cyberpunk environments. */
    CyberpunkNeon       UMETA(DisplayName = "Cyberpunk Neon"),

    /** Primary-colour pop, locked exposure, maximum punch.
     *  Works well with flat-shaded stylized art. */
    HighContrastPop     UMETA(DisplayName = "High Contrast Pop"),

    /** Heavy bloom, bright white gain, whisper-soft vignette.
     *  Ethereal, hazy, romantic look. */
    SoftDream           UMETA(DisplayName = "Soft Dream"),

    /** Cool blue/grey shadows, reduced overall gain.
     *  Moody, foggy, late-night atmosphere. */
    DarkHorizon         UMETA(DisplayName = "Dark Horizon"),

    /** Flat bright tones, zero vignette, zero exposure fluctuation.
     *  Mirrors the clean look of low-poly / toon games. */
    LowPolyBright       UMETA(DisplayName = "Low Poly Bright"),

    /** Bright, energetic arcade look with high clarity, punchy contrast, and clean highlights.
     *  Perfect for fast-paced competitive and obstacle-course gameplay. */
    CartoonPop          UMETA(DisplayName = "Cartoon Pop"),

    /** Soft, magical pastel atmosphere with gentle bloom, airy highlights, and subtle lavender tint.
     *  Ideal for cloud levels, celebration sequences, and whimsical fantasy worlds. */
    DreamyCloud         UMETA(DisplayName = "Dreamy Cloud"),

    /** Rich glossy colors, saturated primary tones, and crisp specular toy-like appearance.
     *  Gives surfaces a colorful molded plastic toy and miniature playground feel. */
    JuicyToyWorld       UMETA(DisplayName = "Juicy Toy World"),

    /** Bold comic-book adventure look with strong light/shadow separation and warm/cool contrast.
     *  Combines 3D animation clarity with dramatic graphic-novel color separation. */
    ComicAdventure      UMETA(DisplayName = "Comic Adventure"),

    /** Designer-controlled — all PostProcessSettings are left untouched so
     *  you can manually dial in exactly the values you need. */
    Custom              UMETA(DisplayName = "Custom (Manual)")
};

// ============================================================================
// AStylizedPostProcessVolume
//
// Inherits from APostProcessVolume so it slots directly into the existing
// UE post-process pipeline with zero overhead compared to the native volume.
// ============================================================================
UCLASS(ClassGroup = "Rendering", meta = (DisplayName = "Stylized Post Process Volume"))
class CAR_GAME_API AStylizedPostProcessVolume : public APostProcessVolume
{
    GENERATED_BODY()

public:
    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------
    AStylizedPostProcessVolume();

    // =========================================================================
    // Designer-Facing Properties
    // =========================================================================

    /** Choose a visual preset. Changing this in the Details Panel or via
     *  Blueprints immediately applies the corresponding post-process settings.
     *  Select "Custom" to leave all values unmodified. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylized Presets",
        meta = (DisplayPriority = 0))
    EPostProcessPreset SelectedPreset;

    // -------------------------------------------------------------------------
    // Fine-tune overrides — only meaningful when SelectedPreset == Custom,
    // but exposed for Blueprint tweaking on any preset at runtime.
    // -------------------------------------------------------------------------

    /** Overall colour saturation multiplier.
     *  1.0 = neutral.  >1.0 = more vivid.  0.0 = greyscale. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylized Presets|Manual Overrides",
        meta = (ClampMin = "0.0", ClampMax = "3.0", DisplayPriority = 1))
    float ManualSaturation;

    /** Contrast (S-curve midpoint push).
     *  1.0 = neutral.  >1.0 = more punchy.  <1.0 = flatter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylized Presets|Manual Overrides",
        meta = (ClampMin = "0.0", ClampMax = "3.0", DisplayPriority = 2))
    float ManualContrast;

    /** Overall linear gain applied on top of scene colour.
     *  1.0 = neutral.  >1.0 = brighter.  <1.0 = darker. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylized Presets|Manual Overrides",
        meta = (ClampMin = "0.0", ClampMax = "3.0", DisplayPriority = 3))
    float ManualGain;

    /** Bloom pass intensity.  0 = off.  1 = standard.  >1 = glowing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylized Presets|Manual Overrides",
        meta = (ClampMin = "0.0", ClampMax = "8.0", DisplayPriority = 4))
    float ManualBloomIntensity;

    /** Screen-edge vignette darkening amount.  0 = none.  1 = heavy. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylized Presets|Manual Overrides",
        meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayPriority = 5))
    float ManualVignetteIntensity;

    // =========================================================================
    // Overrides
    // =========================================================================

    /** Called whenever this actor is placed or its properties change in the
     *  Editor.  Triggers an immediate viewport refresh so designers see the
     *  preset change in real-time without pressing Play. */
    virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
    /** Called by the Details Panel after any property is changed.
     *  Re-applies the current preset so the viewport stays in sync. */
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

    // =========================================================================
    // Blueprint-Callable API
    // =========================================================================

    /** Apply a preset programmatically at runtime (e.g. from a Blueprint
     *  cutscene or a game-mode colour-shift event).
     *  @param NewPreset  The preset to switch to immediately. */
    UFUNCTION(BlueprintCallable, Category = "Stylized Presets",
        meta = (DisplayName = "Apply Post Process Preset"))
    void ApplyPreset(EPostProcessPreset NewPreset);

private:
    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /** Core helper — reads SelectedPreset and writes the matching values into
     *  this->Settings (FPostProcessSettings).  Called from OnConstruction and
     *  PostEditChangeProperty so both Editor and runtime paths share one path. */
    void ApplyPresetSettings();

    /** Writes the mobile-mandatory overrides that must be set regardless of
     *  which preset is active:
     *    - Auto Exposure locked to EV100 = 1.0  (no GPU adaptation overhead)
     *    - Motion Blur = 0                       (expensive on mobile)
     *    - Lens Flare  = 0                       (expensive on mobile)
     *  Called at the end of every preset branch. */
    void ApplyMobileOptimizations();

    // -------------------------------------------------------------------------
    // Convenience setter — writes identical XYZ values plus W into a FVector4
    // colour channel struct (used for Saturation / Contrast / Gain which are
    // stored as FVector4 in FPostProcessSettings where W is the "all channel"
    // multiplier used by the colour grading pipeline).
    // -------------------------------------------------------------------------

    /** Build a FVector4 colour grading vector.
     *  @param RGB   Per-channel tint multiplier (1,1,1 = neutral).
     *  @param W     Overall scalar applied to all channels by the grading math. */
    static FORCEINLINE FVector4 MakeGradingVector(FVector RGB, float W)
    {
        return FVector4(RGB.X, RGB.Y, RGB.Z, W);
    }
};
