using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Index.Interop;

/// <summary>
/// Byte offsets of an unmanaged struct's fields as the runtime lays them out (the raw bytes a C# IComponent keeps in
/// native storage). Nested structs appear under dotted paths ("Vel", "Vel.X"). Returns null whenever the computed
/// size disagrees with the runtime's, so callers never write through a guessed layout.
/// </summary>
internal static class StructFieldLayout
{
    internal readonly record struct Slot(int Offset, int Size);

    internal static Dictionary<string, Slot>? Compute(Type type)
    {
        if (!type.IsValueType || type.IsPrimitive || type.IsEnum) return null;
        var slots = new Dictionary<string, Slot>(StringComparer.Ordinal);
        if (!TryLayout(type, "", 0, slots, out int size, out _)) return null;
        return size == RuntimeSize(type) ? slots : null;
    }

    internal static int RuntimeSize(Type type) =>
        (int)typeof(Unsafe).GetMethod(nameof(Unsafe.SizeOf))!.MakeGenericMethod(type).Invoke(null, null)!;

    private static bool TryLayout(Type type, string prefix, int baseOffset, Dictionary<string, Slot> slots,
        out int size, out int alignment)
    {
        size = 0;
        alignment = 1;
        StructLayoutAttribute? layout = type.StructLayoutAttribute;
        if (layout == null || layout.Value == LayoutKind.Auto) return false;
        int pack = layout.Pack > 0 ? layout.Pack : 8;
        bool isExplicit = layout.Value == LayoutKind.Explicit;

        int offset = 0;
        int end = 0;
        // Metadata order is declaration order, which sequential layout follows.
        foreach (FieldInfo field in type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
                     .OrderBy(f => f.MetadataToken))
        {
            Type fieldType = field.FieldType.IsEnum ? Enum.GetUnderlyingType(field.FieldType) : field.FieldType;
            int fieldSize;
            int fieldAlign;
            var nested = new Dictionary<string, Slot>(StringComparer.Ordinal);
            if (fieldType.IsPrimitive)
            {
                fieldSize = PrimitiveSize(fieldType);
                if (fieldSize == 0) return false;
                fieldAlign = fieldSize;
            }
            else if (fieldType.IsValueType)
            {
                if (!TryLayout(fieldType, field.Name + ".", 0, nested, out fieldSize, out fieldAlign)) return false;
            }
            else
            {
                return false; // references have no stable byte layout
            }

            fieldAlign = Math.Min(fieldAlign, pack);
            int fieldOffset;
            if (isExplicit)
            {
                FieldOffsetAttribute? explicitOffset = field.GetCustomAttribute<FieldOffsetAttribute>();
                if (explicitOffset == null) return false;
                fieldOffset = explicitOffset.Value;
            }
            else
            {
                fieldOffset = (offset + fieldAlign - 1) / fieldAlign * fieldAlign;
            }

            slots[prefix + field.Name] = new Slot(baseOffset + fieldOffset, fieldSize);
            foreach (var (path, slot) in nested)
                slots[prefix + path] = new Slot(baseOffset + fieldOffset + slot.Offset, slot.Size);

            offset = fieldOffset + fieldSize;
            end = Math.Max(end, offset);
            alignment = Math.Max(alignment, fieldAlign);
        }

        size = (end + alignment - 1) / alignment * alignment;
        if (layout.Size > size) size = layout.Size;
        if (size == 0) size = 1; // an empty struct still occupies a byte
        return true;
    }

    private static int PrimitiveSize(Type t)
    {
        if (t == typeof(bool) || t == typeof(byte) || t == typeof(sbyte)) return 1;
        if (t == typeof(short) || t == typeof(ushort) || t == typeof(char)) return 2;
        if (t == typeof(int) || t == typeof(uint) || t == typeof(float)) return 4;
        if (t == typeof(long) || t == typeof(ulong) || t == typeof(double)) return 8;
        return 0; // nint/nuint: platform-sized, not supported here
    }
}
