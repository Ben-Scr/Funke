using System;
using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public sealed class DataAssetRuntimeTests : IDisposable
{
    private const ulong k_Guid = 0xEAD0000000000001;
    private readonly NativeBindingsStruct m_PreviousBindings;
    private static readonly ConcurrentDictionary<ulong, object> s_Instances =
        (ConcurrentDictionary<ulong, object>)typeof(ScriptInstanceManager)
            .GetField("s_DataAssets", BindingFlags.Static | BindingFlags.NonPublic)!.GetValue(null)!;
    private static int s_EnsureCalls;
    private static bool s_LoadSucceeds;

    public unsafe DataAssetRuntimeTests()
    {
        m_PreviousBindings = NativeCallbacks.Bindings;
        var bindings = m_PreviousBindings;
        bindings.DataAsset_EnsureLoaded = &EnsureLoaded;
        NativeCallbacks.Bindings = bindings;
        s_Instances.TryRemove(k_Guid, out _);
        s_EnsureCalls = 0;
        s_LoadSucceeds = true;
    }

    public void Dispose()
    {
        s_Instances.TryRemove(k_Guid, out _);
        NativeCallbacks.Bindings = m_PreviousBindings;
    }

    [UnmanagedCallersOnly]
    private static int EnsureLoaded(ulong guid)
    {
        ++s_EnsureCalls;
        return s_LoadSucceeds && s_Instances.ContainsKey(guid) ? 1 : 0;
    }

    private sealed class ValidatedAsset : DataAsset
    {
        public int ValidationCount;
        public override void OnValidate() => ++ValidationCount;
    }

    private sealed class OtherAsset : DataAsset { }

    [Fact]
    public void LoadedLookupPreservesSharedInstanceWithoutNativeReentryOrValidation()
    {
        var asset = new ValidatedAsset();
        asset._SetUUID(k_Guid);
        asset.OnValidate();
        s_Instances[k_Guid] = asset;

        Assert.Same(asset, DataAssetRuntime.GetLoadedByGuid(k_Guid, typeof(ValidatedAsset)));
        Assert.Same(asset, DataAssetRuntime.GetLoadedByGuid(k_Guid, typeof(DataAsset)));
        Assert.Equal(1, asset.ValidationCount);
        Assert.Equal(0, s_EnsureCalls);
    }

    [Fact]
    public void LoadedLookupRejectsMissingAssetsAndWrongSubtypesWithoutLoading()
    {
        s_Instances[k_Guid] = new ValidatedAsset();

        Assert.Null(DataAssetRuntime.GetLoadedByGuid(0, typeof(DataAsset)));
        Assert.Null(DataAssetRuntime.GetLoadedByGuid(k_Guid + 1, typeof(DataAsset)));
        Assert.Null(DataAssetRuntime.GetLoadedByGuid(k_Guid, typeof(OtherAsset)));
        Assert.Equal(0, s_EnsureCalls);
    }

    [Fact]
    public void SerializedReferenceLoadingStillEnsuresNativeLoadBeforeUsingTheCache()
    {
        var asset = new ValidatedAsset();
        s_Instances[k_Guid] = asset;

        Assert.Null(DataAssetRuntime.LoadByGuid(0, typeof(DataAsset)));
        Assert.Equal(0, s_EnsureCalls);
        Assert.Same(asset, DataAssetRuntime.LoadByGuid(k_Guid, typeof(ValidatedAsset)));
        Assert.Equal(1, s_EnsureCalls);
        Assert.Null(DataAssetRuntime.LoadByGuid(k_Guid, typeof(OtherAsset)));
        Assert.Equal(2, s_EnsureCalls);
        s_LoadSucceeds = false;
        Assert.Null(DataAssetRuntime.LoadByGuid(k_Guid, typeof(DataAsset)));
        Assert.Equal(3, s_EnsureCalls);
        Assert.Equal(0, asset.ValidationCount);
    }
}
