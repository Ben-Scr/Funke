using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Threading;
using Funke;
using Funke.Coroutines;

namespace Funke.Interop;


/// <summary>Per-frame EntityScript callbacks native may skip; mirrors ScriptCallback in ScriptInstance.hpp.</summary>
internal static class ScriptCallbacks
{
    internal const int Update = 1 << 0;
    internal const int FixedUpdate = 1 << 1;
    internal const int CollisionStay2D = 1 << 2;
    internal const int TriggerStay2D = 1 << 3;
    internal const int CollisionStay3D = 1 << 4;
    internal const int TriggerStay3D = 1 << 5;
    internal const int All = -1;

    internal static int ComputeMask(Type type, bool hasUpdateMethod)
    {
        try
        {
            int mask = 0;
            if (hasUpdateMethod || Overrides(type, nameof(EntityScript.OnUpdate), Type.EmptyTypes)) mask |= Update;
            if (Overrides(type, nameof(EntityScript.OnFixedUpdate), Type.EmptyTypes)) mask |= FixedUpdate;
            if (Overrides(type, nameof(EntityScript.OnCollisionStay2D), new[] { typeof(Collision2D) })) mask |= CollisionStay2D;
            if (Overrides(type, nameof(EntityScript.OnTriggerStay2D), new[] { typeof(Collision2D) })) mask |= TriggerStay2D;
            if (Overrides(type, nameof(EntityScript.OnCollisionStay3D), new[] { typeof(Collision3D) })) mask |= CollisionStay3D;
            if (Overrides(type, nameof(EntityScript.OnTriggerStay3D), new[] { typeof(Collision3D) })) mask |= TriggerStay3D;
            return mask;
        }
        catch
        {
            // Unusual metadata: keep every callback rather than risk skipping one.
            return All;
        }
    }

    private static bool Overrides(Type type, string name, Type[] parameters)
    {
        MethodInfo? method = type.GetMethod(name, BindingFlags.Public | BindingFlags.Instance, null, parameters, null);
        return method == null || method.DeclaringType != typeof(EntityScript);
    }
}
