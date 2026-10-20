using System;
using Funke.Interop;

namespace Funke;

/// <summary>A .anim keyframe clip. Assign it to <see cref="Animator.Clip"/> or reference it from a controller state.</summary>
public sealed class AnimationClip : IEquatable<AnimationClip>
{
    public ulong UUID { get; }

    internal AnimationClip(ulong assetId) => UUID = assetId;

    public static AnimationClip? FromAssetUUID(ulong assetId) => assetId == 0 ? null : new AnimationClip(assetId);

    public bool Equals(AnimationClip? other) => other is not null && other.UUID == UUID;
    public override bool Equals(object? obj) => obj is AnimationClip other && Equals(other);
    public override int GetHashCode() => UUID.GetHashCode();
}

/// <summary>A .animctrl state machine (states, parameters, transitions) for <see cref="Animator.Controller"/>.</summary>
public sealed class AnimatorController : IEquatable<AnimatorController>
{
    public ulong UUID { get; }

    internal AnimatorController(ulong assetId) => UUID = assetId;

    public static AnimatorController? FromAssetUUID(ulong assetId) => assetId == 0 ? null : new AnimatorController(assetId);

    public bool Equals(AnimatorController? other) => other is not null && other.UUID == UUID;
    public override bool Equals(object? obj) => obj is AnimatorController other && Equals(other);
    public override int GetHashCode() => UUID.GetHashCode();
}

/// <summary>Values must match the native AnimatorUpdateMode.</summary>
public enum AnimatorUpdateMode
{
    Normal = 0,
    /// <summary>Ignores <see cref="Time.TimeScale"/>, for menus shown while the game is paused.</summary>
    UnscaledTime = 1,
    /// <summary>Steps in the fixed update, in sync with physics.</summary>
    Fixed = 2,
}

/// <summary>
/// What an <see cref="Animator"/> or <see cref="SkinnedMeshRenderer"/> does while it is outside the game camera's view.
/// Values must match the native AnimationCullingMode.
/// </summary>
public enum AnimationCullingMode
{
    AlwaysAnimate = 0,
    /// <summary>The clock, transitions and events keep running, but no pose is written (no skinning).</summary>
    CullPose = 1,
    /// <summary>Pauses until it is back in view.</summary>
    CullCompletely = 2,
}

/// <summary>Whether an animation updates less often far from the camera. Values must match the native AnimationDistanceRate.</summary>
public enum AnimationDistanceRate
{
    /// <summary>Follows <see cref="RenderSettings.AnimationDistanceRateEnabled"/> and its distances.</summary>
    ProjectSetting = 0,
    Off = 1,
    /// <summary>Uses the component's own HalfRateDistance and QuarterRateDistance.</summary>
    Custom = 2,
}

/// <summary>
/// Plays keyframe animations on this entity and its children. With a <see cref="Controller"/>, parameters drive the
/// transitions between its states; without one, <see cref="Clip"/> loops on its own.
/// Animation events call public methods of the scripts on this entity by name.
/// </summary>
public class Animator : Component
{
    private const int k_Float = 0, k_Int = 1, k_Bool = 2, k_Trigger = 3, k_ResetTrigger = 4;

    public AnimatorController? Controller
    {
        get => AnimatorController.FromAssetUUID(InternalCalls.Animator_GetAsset(RequireComponent<Animator>(), 0));
        set => InternalCalls.Animator_SetAsset(RequireComponent<Animator>(), 0, value?.UUID ?? 0);
    }

    /// <summary>Played on its own when <see cref="Controller"/> is null.</summary>
    public AnimationClip? Clip
    {
        get => AnimationClip.FromAssetUUID(InternalCalls.Animator_GetAsset(RequireComponent<Animator>(), 1));
        set => InternalCalls.Animator_SetAsset(RequireComponent<Animator>(), 1, value?.UUID ?? 0);
    }

    /// <summary>Playback speed multiplier; 0 freezes the pose.</summary>
    public float Speed
    {
        get => InternalCalls.Animator_GetSpeed(RequireComponent<Animator>());
        set => InternalCalls.Animator_SetSpeed(RequireComponent<Animator>(), value);
    }

    /// <summary>What the Animator does while no renderer below it is in the game camera's view.</summary>
    public AnimationCullingMode CullingMode
    {
        get => (AnimationCullingMode)(int)ReadCulling(0);
        set => WriteCulling(0, (float)value);
    }

    /// <summary>
    /// Every 2nd / 4th frame far from the camera; the skipped time is added to the next update, so tempo, transitions
    /// and events stay right. Ignored in <see cref="AnimatorUpdateMode.Fixed"/>.
    /// </summary>
    public AnimationDistanceRate DistanceRate
    {
        get => (AnimationDistanceRate)(int)ReadCulling(1);
        set => WriteCulling(1, (float)value);
    }

    /// <summary>With <see cref="AnimationDistanceRate.Custom"/>: world units from the camera from which it updates every 2nd frame.</summary>
    public float HalfRateDistance
    {
        get => ReadCulling(2);
        set => WriteCulling(2, value);
    }

    /// <summary>With <see cref="AnimationDistanceRate.Custom"/>: world units from the camera from which it updates every 4th frame.</summary>
    public float QuarterRateDistance
    {
        get => ReadCulling(3);
        set => WriteCulling(3, value);
    }

    public void SetFloat(string name, float value) => InternalCalls.Animator_SetParameter(RequireComponent<Animator>(), name, k_Float, value);
    public float GetFloat(string name) => InternalCalls.Animator_GetParameter(RequireComponent<Animator>(), name, k_Float);

    public void SetInteger(string name, int value) => InternalCalls.Animator_SetParameter(RequireComponent<Animator>(), name, k_Int, value);
    public int GetInteger(string name) => (int)InternalCalls.Animator_GetParameter(RequireComponent<Animator>(), name, k_Int);

    public void SetBool(string name, bool value) => InternalCalls.Animator_SetParameter(RequireComponent<Animator>(), name, k_Bool, value ? 1f : 0f);
    public bool GetBool(string name) => InternalCalls.Animator_GetParameter(RequireComponent<Animator>(), name, k_Bool) != 0f;

    /// <summary>Sets a trigger; it stays set until a transition that checks it fires, or <see cref="ResetTrigger"/>.</summary>
    public void SetTrigger(string name) => InternalCalls.Animator_SetParameter(RequireComponent<Animator>(), name, k_Trigger, 1f);
    public void ResetTrigger(string name) => InternalCalls.Animator_SetParameter(RequireComponent<Animator>(), name, k_ResetTrigger, 0f);

    public bool HasParameter(string name) => InternalCalls.Animator_HasParameter(RequireComponent<Animator>(), name);

    /// <summary>Jumps to a state without blending. normalizedTime 0..1 is the start..end of its clip.</summary>
    public bool Play(string state, float normalizedTime = 0f)
        => InternalCalls.Animator_Play(RequireComponent<Animator>(), state, normalizedTime, -1f);

    /// <summary>Blends from the current pose to a state over duration seconds.</summary>
    public bool CrossFade(string state, float duration)
        => InternalCalls.Animator_Play(RequireComponent<Animator>(), state, 0f, MathF.Max(duration, 0f));

    public string CurrentStateName => InternalCalls.Animator_GetState(RequireComponent<Animator>(), 0);

    /// <summary>Target of the running crossfade; empty when none.</summary>
    public string NextStateName => InternalCalls.Animator_GetState(RequireComponent<Animator>(), 1);

    /// <summary>Time in the current state in clip lengths (1.5 = halfway through the second loop).</summary>
    public float NormalizedTime => ReadPlayback(0);

    public bool IsInTransition => ReadPlayback(1) != 0f;

    /// <summary>0..1 progress of the running crossfade.</summary>
    public float TransitionProgress => ReadPlayback(2);

    private float ReadPlayback(int index)
    {
        Span<float> values = stackalloc float[3];
        InternalCalls.Animator_GetPlayback(RequireComponent<Animator>(), values);
        return values[index];
    }

    private float ReadCulling(int index)
    {
        Span<float> values = stackalloc float[InternalCalls.AnimatorCullingValueCount];
        InternalCalls.Animator_GetCulling(RequireComponent<Animator>(), values);
        return values[index];
    }

    private void WriteCulling(int index, float value)
    {
        ulong id = RequireComponent<Animator>();
        Span<float> values = stackalloc float[InternalCalls.AnimatorCullingValueCount];
        InternalCalls.Animator_GetCulling(id, values);
        values[index] = value;
        InternalCalls.Animator_SetCulling(id, values);
    }
}
