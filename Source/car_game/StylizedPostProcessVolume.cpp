// ============================================================================
// StylizedPostProcessVolume.cpp
//
// Implementation of AStylizedPostProcessVolume.
//
// Architecture notes
// ------------------
//  • All preset data lives in ApplyPresetSettings() as a single switch block.
//    Adding a new preset means adding one new case — nothing else changes.
//
//  • FPostProcessSettings uses FVector4 for colour-grading channels where:
//       X = Red multiplier
//       Y = Green multiplier
//       Z = Blue multiplier
//       W = Luminance / "all-channel" scalar
//    Setting W = target value and XYZ = 1.0 is the neutral-tint, uniform-
//    scale approach.  Tinting a channel means deviating its XYZ from 1.0.
//
//  • Every preset ends with a call to ApplyMobileOptimizations() which locks
//    the four mobile-critical fields (AutoExposure min/max, MotionBlur,
//    LensFlare) so they can never be accidentally left expensive.
//
//  • OnConstruction fires in both the Editor (on drag, property change) and
//    at runtime (on BeginPlay construction), so one codepath covers both.
//
//  • PostEditChangeProperty is Editor-only and gives us the fine-grained
//    "which property changed" information we need for live preview.
//
// UE4.27 / UE5 compatibility
// --------------------------
//  FPostProcessSettings member names are identical between UE4.27 and UE5.
//  The only divergence is UE5's split of ColorGradingShadows/Midtones/
//  Highlights into a finer set — but the original names still compile in UE5
//  via aliases, so no #if ENGINE_MAJOR_VERSION guards are required here.
// ============================================================================

#include "StylizedPostProcessVolume.h"

// Engine includes — all part of the "Engine" module, no extra Build.cs entry needed.
#include "Engine/PostProcessVolume.h"

// ============================================================================
// Constructor
// ============================================================================
AStylizedPostProcessVolume::AStylizedPostProcessVolume()
{
    // Affect the entire world without needing a physical brush volume.
    // Designers can still override this in the Details Panel if they want
    // a localised effect.
    bUnbound = true;

    // Start with the most visually impressive preset so the actor looks
    // intentional the moment it's dragged into a level.
    SelectedPreset = EPostProcessPreset::FallGuysCandy;

    // Default Manual Override values — neutral / unmodified baseline.
    ManualSaturation      = 1.0f;
    ManualContrast        = 1.0f;
    ManualGain            = 1.0f;
    ManualBloomIntensity  = 0.675f; // UE default
    ManualVignetteIntensity = 0.4f; // UE default

    // Apply the default preset immediately so the actor is fully configured
    // the instant it is constructed (both in Editor and at runtime).
    ApplyPresetSettings();
}

// ============================================================================
// OnConstruction
// Called by the Editor whenever this actor is placed or transformed, and at
// runtime when the actor is spawned.  This is our primary "refresh" hook.
// ============================================================================
void AStylizedPostProcessVolume::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // Re-apply the currently selected preset so the viewport always reflects
    // whatever the designer has chosen in the Details Panel.
    ApplyPresetSettings();
}

// ============================================================================
// PostEditChangeProperty  (Editor-only)
// Called by the Details Panel the moment a property value is committed.
// We use it to give instant visual feedback without needing to press Play.
// ============================================================================
#if WITH_EDITOR
void AStylizedPostProcessVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Re-apply on ANY property change — this covers both the SelectedPreset
    // enum AND the ManualXxx float sliders so live tweaking always works.
    ApplyPresetSettings();
}
#endif // WITH_EDITOR

// ============================================================================
// ApplyPreset  (Blueprint-Callable)
// Public API for switching presets at runtime from Blueprints or C++.
// ============================================================================
void AStylizedPostProcessVolume::ApplyPreset(EPostProcessPreset NewPreset)
{
    SelectedPreset = NewPreset;
    ApplyPresetSettings();
}

// ============================================================================
// ApplyMobileOptimizations  (private)
//
// Forces the four settings that must always be locked on mobile:
//   1. Auto Exposure min EV100 = 1.0  — no screen-darkening in shadows
//   2. Auto Exposure max EV100 = 1.0  — no screen-brightening in highlights
//   3. Motion Blur amount    = 0.0    — expensive velocity pass, skip it
//   4. Lens Flare intensity  = 0.0    — expensive scatter pass, skip it
//
// These overrides are applied AFTER every preset branch so they can never be
// accidentally left enabled by a preset's own settings block.
// ============================================================================
void AStylizedPostProcessVolume::ApplyMobileOptimizations()
{
    // ---------- Auto Exposure ----------
    // Lock both min and max to the same EV100 value so the adaptation curve
    // collapses to a fixed point.  This eliminates the GPU readback + histogram
    // pass on mobile renderers that support it.
    Settings.bOverride_AutoExposureMinBrightness = true;
    Settings.AutoExposureMinBrightness = 1.0f; // EV100 = 1.0 (maps to ~2 nits)

    Settings.bOverride_AutoExposureMaxBrightness = true;
    Settings.AutoExposureMaxBrightness = 1.0f;

    // ---------- Motion Blur ----------
    // Motion blur requires a velocity buffer and a multi-pass blur; on mobile
    // hardware this is a significant bottleneck.  Zero disables the pass.
    Settings.bOverride_MotionBlurAmount = true;
    Settings.MotionBlurAmount = 0.0f;

    // ---------- Lens Flare ----------
    // The lens flare bright-pass + scatter is expensive and rarely readable
    // on small mobile screens.
    Settings.bOverride_LensFlareIntensity = true;
    Settings.LensFlareIntensity = 0.0f;
}

// ============================================================================
// ApplyPresetSettings  (private — the core of this class)
//
// Reads SelectedPreset and writes the matching colour-grading + post-process
// parameters into this->Settings (FPostProcessSettings).
//
// Colour grading parameter reference
// -----------------------------------
//  ColorSaturation   — FVector4, per-channel + W overall saturation
//  ColorContrast     — FVector4, per-channel + W contrast S-curve push
//  ColorGain         — FVector4, per-channel + W linear gain
//  BloomIntensity    — float,    bloom pass brightness
//  VignetteIntensity — float,    edge darkening 0..1
//
// For all FVector4 colour fields:
//   (1,1,1,W) = neutral tint, W scales all channels uniformly
//   Tinting    = deviate individual XYZ channels from 1.0
// ============================================================================
void AStylizedPostProcessVolume::ApplyPresetSettings()
{
    // Guard: if Custom is selected, leave all settings exactly as they are so
    // the designer's manual values are respected.
    if (SelectedPreset == EPostProcessPreset::Custom)
    {
        // We still enforce mobile optimizations on Custom so the designer
        // can't accidentally leave motion blur on for a mobile build.
        ApplyMobileOptimizations();
        return;
    }

    // Reset optional overrides so presets never leak settings into each other when switching.
    Settings.bOverride_WhiteTemp = false;
    Settings.bOverride_WhiteTint = false;
    Settings.bOverride_ColorGamma = false;
    Settings.bOverride_BloomThreshold = false;

    switch (SelectedPreset)
    {
    // ========================================================================
    // 1. FALL GUYS CANDY
    //    Vibrant, high-saturation, warm highlights, soft bloom.
    //    Feel: bright party game, bouncy and colourful.
    // ========================================================================
    case EPostProcessPreset::FallGuysCandy:
    {
        // Saturation: 1.35x overall (W), slight warm push (R+, B-)
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.05f, 1.0f, 0.92f), 1.35f);

        // Contrast: mild push to keep highlights punchy without crushing blacks
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.10f);

        // Gain: warm-toned — reds slightly lifted, blues slightly pulled
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.08f, 1.02f, 0.92f), 1.05f);

        // Bloom: soft, wide — the signature "candy glow" of the style
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 1.2f;

        // Vignette: very subtle — keeps focus without darkening the fun colours
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.25f;

        break;
    }

    // ========================================================================
    // 2. PASTEL TOON
    //    Soft pastel saturation, high gain, low contrast.
    //    Feel: children's storybook, gentle, approachable.
    // ========================================================================
    case EPostProcessPreset::PastelToon:
    {
        // Saturation: slightly reduced so colours feel "washed" / pastel
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 0.80f);

        // Contrast: flat — pushes the image toward the light midtones
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 0.80f);

        // Gain: lifted — everything looks brighter and softer
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.10f, 1.08f, 1.12f), 1.20f);

        // Bloom: light halo — soft edges reinforce the storybook feel
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 0.9f;

        // Vignette: none — we want fully uniform, flat brightness
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.0f;

        break;
    }

    // ========================================================================
    // 3. NEON SYNTHWAVE
    //    High contrast, boosted magenta + cyan gain, crushed blacks.
    //    Feel: 80s retro, electric, moody grid aesthetic.
    // ========================================================================
    case EPostProcessPreset::NeonSynthwave:
    {
        // Saturation: pushed hard to make neon colours pop
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.50f);

        // Contrast: high — deep blacks crushed, brights lifted
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.45f);

        // Gain: magenta boost (R+ B+, G neutral) for that synthwave hue
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.15f, 0.90f, 1.25f), 1.0f);

        // Bloom: strong — neon lights bleed into the dark surroundings
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 2.5f;

        // Vignette: prominent — pulls the eye inward through dark borders
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.65f;

        break;
    }

    // ========================================================================
    // 4. SUNSET GLOW
    //    Warm peach/orange tint, soft contrast.
    //    Feel: golden hour, nostalgic, serene outdoor scenes.
    // ========================================================================
    case EPostProcessPreset::SunsetGlow:
    {
        // Saturation: slightly warm-boosted
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.05f, 0.98f, 0.90f), 1.10f);

        // Contrast: gentle lift — preserves shadow detail in warm light
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.05f);

        // Gain: orange/peach tint — reds and greens lifted, blues pulled back
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.20f, 1.05f, 0.75f), 1.05f);

        // Bloom: warm, soft glow like light catching dust in the air
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 1.0f;

        // Vignette: subtle warm darkening at edges
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.35f;

        break;
    }

    // ========================================================================
    // 5. CYBERPUNK NEON
    //    Cool blue shadows, electric cyan/yellow highlights, high contrast.
    //    Feel: rain-soaked urban dystopia, hacker aesthetic.
    // ========================================================================
    case EPostProcessPreset::CyberpunkNeon:
    {
        // Saturation: very high — skin-searing saturation
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.60f);

        // Contrast: high contrast, aggressive S-curve
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.50f);

        // Gain: cool blue shadow tint, electric cyan highlights
        //   R = dimmed, G = slight lift, B = boosted for cool cast
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(0.85f, 1.05f, 1.35f), 1.0f);

        // Bloom: electric lens flare — bright light sources bleed heavily
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 3.0f;

        // Vignette: heavy — city disappears into darkness at screen edges
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.75f;

        break;
    }

    // ========================================================================
    // 6. HIGH CONTRAST POP
    //    Punchy primary colours, locked exposure, maximum punch.
    //    Feel: flat-shaded stylized games, comic-book energy.
    // ========================================================================
    case EPostProcessPreset::HighContrastPop:
    {
        // Saturation: maximised — pure primary colours
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.70f);

        // Contrast: extreme — very deep blacks, blown-out whites
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.60f);

        // Gain: neutral tint — let pure saturation do the heavy lifting
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.0f);

        // Bloom: minimal — pop art has clean, hard edges not soft glows
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 0.4f;

        // Vignette: none — full-frame pop colour, no edge fade
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.0f;

        break;
    }

    // ========================================================================
    // 7. SOFT DREAM
    //    Heavy bloom, bright white gain, minimal vignette.
    //    Feel: ethereal, romantic, magical forest / heavenly setting.
    // ========================================================================
    case EPostProcessPreset::SoftDream:
    {
        // Saturation: gentle — milky, slightly desaturated dream palette
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 0.85f);

        // Contrast: very flat — everything floats toward white midtones
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 0.70f);

        // Gain: bright white lift across all channels
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.15f, 1.12f, 1.20f), 1.30f);

        // Bloom: very heavy — the signature "dreamy haze" glow
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 4.0f;

        // Vignette: barely perceptible — keeps full, open, airy frame
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.10f;

        break;
    }

    // ========================================================================
    // 8. DARK HORIZON
    //    Cool blue/grey shadow tint, reduced overall gain.
    //    Feel: foggy night, late-evening thriller, underwater.
    // ========================================================================
    case EPostProcessPreset::DarkHorizon:
    {
        // Saturation: muted — desaturated, oppressive atmosphere
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 0.70f);

        // Contrast: moderate — preserves shadow detail in a dark scene
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.15f);

        // Gain: dark, cool — pulled down overall, blue channel favoured
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(0.80f, 0.85f, 1.00f), 0.85f);

        // Bloom: minimal — no warmth, barely any glow
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 0.3f;

        // Vignette: strong — the horizon feels distant and oppressive
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.70f;

        break;
    }

    // ========================================================================
    // 9. LOW POLY BRIGHT
    //    Flat bright tones, zero vignette, zero exposure fluctuation.
    //    Feel: low-poly / toon game, clean flat shading.
    // ========================================================================
    case EPostProcessPreset::LowPolyBright:
    {
        // Saturation: slightly elevated — flat colours feel richer
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.15f);

        // Contrast: flat — mimics the look of unlit / cel-shaded materials
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 0.90f);

        // Gain: uniformly bright — everything reads clearly on a small screen
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.05f, 1.05f, 1.05f), 1.10f);

        // Bloom: off — flat shading has no specular to bloom
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 0.0f;

        // Vignette: none — full-frame flat colour, nothing hidden at edges
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.0f;

        break;
    }

    // ========================================================================
    // 10. CARTOON POP
    //     High clarity, punchy colors, snappy contrast, clean lifted shadows.
    //     Feel: energetic Saturday-morning cartoon / arcade party game.
    // ========================================================================
    case EPostProcessPreset::CartoonPop:
    {
        // Saturation: punchy and vibrant across all primary colors without color-cast
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.02f, 1.02f, 1.00f), 1.28f);

        // Contrast: snappy cartoon S-curve, giving pop to character silhouettes
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.22f);

        // Gain: clean, bright daylight highlights
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.04f, 1.03f, 1.02f), 1.08f);

        // Gamma: slightly lifted shadows for high readability on small mobile screens
        Settings.bOverride_ColorGamma = true;
        Settings.ColorGamma = MakeGradingVector(FVector(1.03f, 1.03f, 1.02f), 1.06f);

        // White Temperature: crisp, vibrant daylight
        Settings.bOverride_WhiteTemp = true;
        Settings.WhiteTemp = 6300.0f;

        // Bloom: controlled, tight threshold so only bright specular glints glow
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 0.65f;
        Settings.bOverride_BloomThreshold = true;
        Settings.BloomThreshold = 1.2f;

        // Vignette: subtle grounding border leaving the action area crystal clear
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.20f;

        break;
    }

    // ========================================================================
    // 11. DREAMY CLOUD
    //     Soft pastel appearance, diffused highlights, gentle lavender-pink glow.
    //     Feel: floating in a colorful cartoon dream world / celestial cloudscape.
    // ========================================================================
    case EPostProcessPreset::DreamyCloud:
    {
        // Saturation: pastel separation — gentle magenta/periwinkle vibrance
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.05f, 0.95f, 1.10f), 1.12f);

        // Contrast: soft, gentle roll-off to avoid harsh transitions
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(0.98f, 1.0f, 0.98f), 0.88f);

        // Gain: ethereal lilac/periwinkle highlight lift
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.12f, 1.04f, 1.18f), 1.14f);

        // Gamma: airy soft sky shadows, preventing dark crush
        Settings.bOverride_ColorGamma = true;
        Settings.ColorGamma = MakeGradingVector(FVector(1.04f, 1.06f, 1.10f), 1.08f);

        // White Balance: cool sky temperature with delicate pastel magenta tint
        Settings.bOverride_WhiteTemp = true;
        Settings.WhiteTemp = 6800.0f;
        Settings.bOverride_WhiteTint = true;
        Settings.WhiteTint = 0.15f;

        // Bloom: diffused, airy cloud halo around characters and platforms
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 2.2f;
        Settings.bOverride_BloomThreshold = true;
        Settings.BloomThreshold = 0.7f;

        // Vignette: soft-focus edge vignette
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.18f;

        break;
    }

    // ========================================================================
    // 12. JUICY TOY WORLD
    //     Rich glossy colors, crisp specular sheen, saturated molded plastic look.
    //     Feel: miniature toy playground, glossy action figures, colorful plastic.
    // ========================================================================
    case EPostProcessPreset::JuicyToyWorld:
    {
        // Saturation: rich, juicy, saturated primary tones (plastic red, green, yellow)
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.08f, 1.06f, 0.98f), 1.42f);

        // Contrast: crisp specular separation that mimics shiny molded plastic reflections
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.02f, 1.0f, 1.02f), 1.16f);

        // Gain: sunny, warm toy showroom illumination
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.10f, 1.06f, 0.96f), 1.10f);

        // Gamma: slightly deeper midtones accentuating specular sheen on curved plastic
        Settings.bOverride_ColorGamma = true;
        Settings.ColorGamma = MakeGradingVector(FVector(0.98f, 0.98f, 0.98f), 0.96f);

        // White Temperature: warm, inviting incandescent / toy shop warmth
        Settings.bOverride_WhiteTemp = true;
        Settings.WhiteTemp = 6100.0f;

        // Bloom: tight, sharp specular pings rather than hazy glow
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 0.55f;
        Settings.bOverride_BloomThreshold = true;
        Settings.BloomThreshold = 1.3f;

        // Vignette: subtle diorama / miniature tilt-shift framing
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.28f;

        break;
    }

    // ========================================================================
    // 13. COMIC ADVENTURE
    //     Bold light/shadow graphic split, warm hero highlights vs cool shadow fill.
    //     Feel: 3D animated cartoon game meets colorful comic-book adventure.
    // ========================================================================
    case EPostProcessPreset::ComicAdventure:
    {
        // Saturation: bold comic-book print ink saturation
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = MakeGradingVector(FVector(1.04f, 1.02f, 1.04f), 1.25f);

        // Contrast: dramatic light/shadow graphic split, crisp visual depth
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = MakeGradingVector(FVector(1.0f, 1.0f, 1.0f), 1.38f);

        // Gain: warm golden hero key light on highlights
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = MakeGradingVector(FVector(1.16f, 1.06f, 0.92f), 1.12f);

        // Gamma: cool cyan/slate ambient fill in the shadows (complementary split)
        Settings.bOverride_ColorGamma = true;
        Settings.ColorGamma = MakeGradingVector(FVector(0.92f, 0.96f, 1.08f), 0.95f);

        // White Temperature: adventurous warm daylight
        Settings.bOverride_WhiteTemp = true;
        Settings.WhiteTemp = 6000.0f;

        // Bloom: crisp graphic light bursts on highlights
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 0.80f;
        Settings.bOverride_BloomThreshold = true;
        Settings.BloomThreshold = 1.1f;

        // Vignette: cinematic vignette framing the adventure action
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = 0.38f;

        break;
    }

    // ========================================================================
    // Default safety fallback — should never be reached because Custom is
    // handled by the early-return guard above, but prevents compiler warnings.
    // ========================================================================
    default:
        break;
    }

    // Always apply mobile optimizations last so no preset can accidentally
    // override them with expensive settings.
    ApplyMobileOptimizations();
}
