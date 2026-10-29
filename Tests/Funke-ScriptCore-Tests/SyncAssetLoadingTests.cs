using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public sealed class SyncAssetLoadingTests : IDisposable
{
    private readonly NativeBindingsStruct m_PreviousBindings;
    private static readonly Dictionary<ulong, int> s_Kinds = new();
    private static int s_TypedCalls;
    private static int s_UnexpectedCalls;
    private static int s_LastKind;
    private static ulong s_LastGuid;
    private static string s_LastPath = "";
    private static ulong s_ResolvedGuid;
    private static bool s_FailLoad;

    public unsafe SyncAssetLoadingTests()
    {
        m_PreviousBindings = NativeCallbacks.Bindings;
        var bindings = m_PreviousBindings;
        bindings.Asset_LoadTyped = &LoadTyped;
        bindings.Asset_IsValid = &UnexpectedLookup;
        bindings.Asset_GetKind = &UnexpectedLookup;
        bindings.Asset_GetOrCreateUUIDFromPath = &UnexpectedPathLookup;
        bindings.Texture_LoadAsset = &UnexpectedLookup;
        bindings.Texture_IsRuntime = &UnexpectedLookup;
        bindings.Audio_LoadAsset = &UnexpectedLookup;
        bindings.Font_LoadAsset = &UnexpectedLookup;
        bindings.DataAsset_EnsureLoaded = &UnexpectedLookup;
        NativeCallbacks.Bindings = bindings;
        s_Kinds.Clear();
        foreach (int kind in new[] { 1, 2, 4, 6, 8, 10, 11, 12, 13 })
            s_Kinds.Add((ulong)(1000 + kind), kind);
        s_TypedCalls = 0;
        s_UnexpectedCalls = 0;
        s_LastKind = 0;
        s_LastGuid = 0;
        s_LastPath = "";
        s_ResolvedGuid = 1001;
        s_FailLoad = false;
    }

    public void Dispose() => NativeCallbacks.Bindings = m_PreviousBindings;

    [UnmanagedCallersOnly]
    private static unsafe ulong LoadTyped(byte* path, ulong guid, int kind)
    {
        ++s_TypedCalls;
        s_LastPath = path == null ? "" : Marshal.PtrToStringUTF8((IntPtr)path)!;
        s_LastGuid = guid;
        s_LastKind = kind;
        ulong resolved = string.IsNullOrEmpty(s_LastPath) ? guid : s_ResolvedGuid;
        return !s_FailLoad && resolved != 0 && s_Kinds.GetValueOrDefault(resolved) == kind ? resolved : 0;
    }

    [UnmanagedCallersOnly]
    private static int UnexpectedLookup(ulong guid)
    {
        ++s_UnexpectedCalls;
        return s_Kinds.GetValueOrDefault(guid);
    }

    [UnmanagedCallersOnly]
    private static unsafe ulong UnexpectedPathLookup(byte* path)
    {
        ++s_UnexpectedCalls;
        return s_ResolvedGuid;
    }

    private static ulong GuidOf(object asset) => asset switch
    {
        Texture texture => texture.UUID,
        Audio audio => audio.UUID,
        Font font => font.UUID,
        Material material => material.UUID,
        Mesh mesh => mesh.UUID,
        AnimationClip clip => clip.UUID,
        AnimatorController controller => controller.UUID,
        Entity entity => entity.PrefabGUID,
        DataAsset data => data.UUID,
        _ => throw new InvalidOperationException("Unexpected asset wrapper.")
    };

    private static void CheckSingleCall<T>(int kind, bool byPath) where T : class
    {
        ulong guid = (ulong)(1000 + kind);
        s_ResolvedGuid = guid;
        int previous = s_TypedCalls;
        T? asset = byPath ? AssetManager.Load<T>("Assets/Wüste.asset") : AssetManager.LoadByGUID<T>(guid);
        Assert.Equal(guid, GuidOf(Assert.IsType<T>(asset)));
        Assert.Equal(previous + 1, s_TypedCalls);
        Assert.Equal(0, s_UnexpectedCalls);
        Assert.Equal(kind, s_LastKind);
        Assert.Equal(byPath ? 0UL : guid, s_LastGuid);
        Assert.Equal(byPath ? "Assets/Wüste.asset" : "", s_LastPath);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void AssetWrappersUseOneTypedNativeCallWithoutExtraValidation(bool byPath)
    {
        CheckSingleCall<Texture>(1, byPath);
        CheckSingleCall<Audio>(2, byPath);
        CheckSingleCall<Entity>(4, byPath);
        CheckSingleCall<Font>(6, byPath);
        CheckSingleCall<Material>(10, byPath);
        CheckSingleCall<Mesh>(11, byPath);
        CheckSingleCall<AnimationClip>(12, byPath);
        CheckSingleCall<AnimatorController>(13, byPath);
        Assert.Equal(8, s_TypedCalls);
    }

    [Fact]
    public void StringGuidParsingDoesNotRequireAnotherNativeCall()
    {
        s_Kinds.Add(0xABCD, 1);
        Texture? texture = AssetManager.LoadByGUID<Texture>("AB-CD");
        Assert.Equal(0xABCDUL, Assert.IsType<Texture>(texture).UUID);
        Assert.Equal(0xABCDUL, s_LastGuid);
        Assert.Equal(1, s_TypedCalls);
        Assert.Equal(0, s_UnexpectedCalls);
    }

    [Fact]
    public void DirectAssetFactoriesUseOneTypedCallWithoutRuntimeOrValidityProbes()
    {
        Assert.Equal(1001UL, Assert.IsType<Texture>(Texture.FromAssetUUID(1001)).UUID);
        Assert.Equal(1, s_TypedCalls);
        Assert.Equal(1, s_LastKind);
        Assert.Equal(0, s_UnexpectedCalls);

        Assert.Equal(1002UL, Assert.IsType<Audio>(Audio.FromAssetUUID(1002)).UUID);
        Assert.Equal(2, s_TypedCalls);
        Assert.Equal(2, s_LastKind);
        Assert.Equal(0, s_UnexpectedCalls);

        Assert.Equal(1006UL, Assert.IsType<Font>(Font.FromAssetUUID(1006)).UUID);
        Assert.Equal(3, s_TypedCalls);
        Assert.Equal(6, s_LastKind);
        Assert.Equal(0, s_UnexpectedCalls);

        const ulong runtimeGuid = 0xF000000000000123;
        s_Kinds.Add(runtimeGuid, 1);
        Assert.Equal(runtimeGuid, Assert.IsType<Texture>(Texture.FromAssetUUID(runtimeGuid)).UUID);
        Assert.Equal(4, s_TypedCalls);
        Assert.Equal(1, s_LastKind);
        Assert.Equal(0, s_UnexpectedCalls);

        Assert.Null(Texture.FromAssetUUID(0));
        Assert.Null(Audio.FromAssetUUID(0));
        Assert.Null(Font.FromAssetUUID(0));
        Assert.Equal(4, s_TypedCalls);
        Assert.Equal(0, s_UnexpectedCalls);
    }

    [Fact]
    public void EmptyReferencesAndUnsupportedManagedTypesDoNotCrossTheNativeBoundary()
    {
        Assert.Null(AssetManager.Load<Texture>(""));
        Assert.Null(AssetManager.Load<Texture>(" "));
        Assert.Null(AssetManager.LoadByGUID<Texture>(0));
        Assert.Null(AssetManager.LoadByGUID<Texture>("invalid-guid"));
        Assert.Null(AssetManager.Load<object>("anything"));
        Assert.Null(AssetManager.LoadByGUID<object>(1001));
        Assert.Equal(0, s_TypedCalls);
        Assert.Equal(0, s_UnexpectedCalls);
    }

    [Fact]
    public void MissingMismatchedAndFailedAssetsReturnNullFromOneNativeCallEach()
    {
        Assert.Null(AssetManager.LoadByGUID<Texture>(9999));
        Assert.Equal(1, s_TypedCalls);
        Assert.Null(AssetManager.LoadByGUID<Texture>(1002));
        Assert.Equal(2, s_TypedCalls);
        s_ResolvedGuid = 0;
        Assert.Null(AssetManager.Load<Texture>("missing.png"));
        Assert.Equal(3, s_TypedCalls);
        s_FailLoad = true;
        Assert.Null(AssetManager.LoadByGUID<Texture>(1001));
        Assert.Equal(4, s_TypedCalls);
        Assert.Equal(0, s_UnexpectedCalls);
    }

    [Fact]
    public void DataAssetsReuseTheManagedInstanceWithoutASecondEnsureLoadedCall()
    {
        var instances = (ConcurrentDictionary<ulong, object>)typeof(ScriptInstanceManager)
            .GetField("s_DataAssets", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
        instances.TryGetValue(1008, out object? previous);
        var expected = new TestDataAsset();
        expected._SetUUID(1008);
        instances[1008] = expected;
        try
        {
            Assert.Same(expected, AssetManager.LoadByGUID<TestDataAsset>(1008));
            Assert.Equal(1, s_TypedCalls);
            s_ResolvedGuid = 1008;
            Assert.Same(expected, AssetManager.Load<TestDataAsset>("Items/shared.dataasset"));
            Assert.Equal(2, s_TypedCalls);
            Assert.Null(AssetManager.LoadByGUID<OtherDataAsset>(1008));
            Assert.Equal(3, s_TypedCalls);
            Assert.Equal(8, s_LastKind);
            Assert.Equal(0, s_UnexpectedCalls);
        }
        finally
        {
            if (previous != null) instances[1008] = previous;
            else instances.TryRemove(1008, out _);
        }
    }

    private sealed class TestDataAsset : DataAsset { }
    private sealed class OtherDataAsset : DataAsset { }
}
