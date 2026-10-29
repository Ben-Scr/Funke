using System;
using Funke.Interop;

namespace Funke;

internal static class DataAssetRuntime
{
    internal static DataAsset? LoadByGuid(ulong guid, Type subtype)
    {
        if (guid == 0 || !InternalCalls.DataAsset_EnsureLoaded(guid))
            return null;

        return GetLoadedByGuid(guid, subtype);
    }

    // Native loading owns construction and validation; retrieving the shared instance must not repeat either.
    internal static DataAsset? GetLoadedByGuid(ulong guid, Type subtype)
    {
        if (guid == 0) return null;
        return ScriptInstanceManager.GetDataAssetObject(guid) is DataAsset instance
            && subtype.IsInstanceOfType(instance) ? instance : null;
    }
}
