using Funke.Interop;
using System;

namespace Funke.Graphics;

public static class Gizmo
{
    /// <summary>
    /// Master switch for gizmo drawing. When false, every Gizmo.Draw* call is a
    /// no-op for the rest of the session until set back to true.
    /// </summary>
    public static bool IsEnabled
    {
        get => InternalCalls.Gizmo_GetEnabled();
        set => InternalCalls.Gizmo_SetEnabled(value);
    }

    public static Color Color
    {
        get => GetColor();
        set => InternalCalls.Gizmo_SetColor(value.R, value.G, value.B, value.A);
    }

    internal static Color GetColor()
    {
        InternalCalls.Gizmo_GetColor(out float r, out float g, out float b, out float a);
        return new Color(r, g, b, a);
    }

    public static float LineWidth
    {
        get => InternalCalls.Gizmo_GetLineWidth();
        set => InternalCalls.Gizmo_SetLineWidth(value);
    }

    /// <summary>
    /// Per-frame render budget, summed across gizmo draws (lines, squares, circles,
    /// text). Enforced per view on the VISIBLE gizmos only — off-screen gizmos are
    /// frustum-culled BEFORE counting, so drawing thousands of off-screen gizmos can't
    /// starve the on-screen ones. If a single view's on-screen gizmos exceed the
    /// budget, the overflow is dropped (with a one-time log warning). Default: 100,000.
    /// Cost: a line = 1, a (wire or filled) square = 4, a (wire or filled) circle = its
    /// segment count (default 32), text = its length.
    /// 3D views have a separate line budget: line = 1, box = 12, sphere = 3 * segments.
    /// Visible 3D lines are kept in submission order up to that budget.
    /// </summary>
    public static int MaxVertices
    {
        get => InternalCalls.Gizmo_GetMaxVertices();
        set => InternalCalls.Gizmo_SetMaxVertices(value);
    }

    /// <summary>
    /// Total gizmo draws accumulated so far this frame (read-only, pre-cull). This is
    /// the raw count across all draws; the render budget (<see cref="MaxVertices"/>) is
    /// enforced separately, per view, on the on-screen subset.
    /// </summary>
    public static int RegisteredVertices => InternalCalls.Gizmo_GetRegisteredVertices();


    public static void DrawLine(Vector2 start, Vector2 end)
        => InternalCalls.Gizmo_DrawLine(start.X, start.Y, end.X, end.Y);

    /// <summary>World-space 3D line, depth-tested against scene meshes.</summary>
    public static void DrawLine(Vector3 start, Vector3 end)
        => InternalCalls.Gizmo_DrawLine3D(start.X, start.Y, start.Z, end.X, end.Y, end.Z);

    public static void DrawRay(Ray ray, float distance) => DrawLine(ray.Origin, ray.Direction, distance);

    public static void DrawAxes(Vector3 origin, Quaternion rotation, float length)
    {
        DrawLine(origin, rotation * Vector3.Right, length);
        DrawLine(origin, rotation * Vector3.Up, length);
        DrawLine(origin, rotation * Vector3.Forward, length);
    }

    public static void DrawArrow(Vector3 start, Vector3 end, float headLength)
    {
        if (!float.IsFinite(headLength) || headLength < 0) throw new ArgumentOutOfRangeException(nameof(headLength));
        Vector3 displacement = end - start;
        if (!Vector3.TryNormalize(displacement, out Vector3 direction)) return;
        DrawLine(start, end);
        float size = Math.Min(headLength, displacement.Length());
        if (size == 0) return;
        Vector3 side = Perpendicular(direction) * (size * 0.5f);
        Vector3 baseCenter = end - size * direction;
        DrawLine(end, baseCenter + side);
        DrawLine(end, baseCenter - side);
    }

    /// <summary>Wire capsule between the centers of its two hemispheres.</summary>
    public static void DrawCapsule(Vector3 a, Vector3 b, float radius, int segments = 24)
    {
        if (!float.IsFinite(radius) || radius < 0) throw new ArgumentOutOfRangeException(nameof(radius));
        if (segments < 4 || segments > 128) throw new ArgumentOutOfRangeException(nameof(segments));
        if (!Vector3.TryNormalize(b - a, out Vector3 axis)) { DrawSphere(a, radius, segments); return; }
        if (radius == 0) { DrawLine(a, b); return; }
        Vector3 u = Perpendicular(axis), v = Vector3.Cross(axis, u);
        Vector3 last = radius * u;
        for (int i = 1; i <= segments; i++)
        {
            float t = MathF.Tau * i / segments;
            Vector3 next = radius * (u * MathF.Cos(t) + v * MathF.Sin(t));
            DrawLine(a + last, a + next);
            DrawLine(b + last, b + next);
            last = next;
        }
        foreach (Vector3 radial in (ReadOnlySpan<Vector3>)[u, v])
        {
            DrawLine(a + radius * radial, b + radius * radial);
            DrawLine(a - radius * radial, b - radius * radial);
            Vector3 previousA = a + radius * radial, previousB = b + radius * radial;
            for (int i = 1; i <= segments / 2; i++)
            {
                float t = MathF.PI * i / (segments / 2);
                Vector3 offset = (radius * MathF.Cos(t)) * radial;
                Vector3 cap = (radius * MathF.Sin(t)) * axis;
                Vector3 nextA = a + offset - cap, nextB = b + offset + cap;
                DrawLine(previousA, nextA);
                DrawLine(previousB, nextB);
                previousA = nextA;
                previousB = nextB;
            }
        }
    }

    private static Vector3 Perpendicular(Vector3 axis) =>
        Vector3.Cross(axis, MathF.Abs(axis.Y) < 0.9f ? Vector3.Up : Vector3.Right).Normalized();

    /// <summary>Near and far loops consist of four matching world-space corners in cyclic order.</summary>
    public static void DrawFrustum(ReadOnlySpan<Vector3> nearCorners, ReadOnlySpan<Vector3> farCorners)
    {
        if (nearCorners.Length != 4 || farCorners.Length != 4) throw new ArgumentException("Four corners per plane are required.");
        for (int i = 0; i < 4; i++)
        {
            int next = (i + 1) % 4;
            DrawLine(nearCorners[i], nearCorners[next]);
            DrawLine(farCorners[i], farCorners[next]);
            DrawLine(nearCorners[i], farCorners[i]);
        }
    }

    /// <summary>Draws a 3D line of the given distance along direction (need not be normalized). A zero direction draws nothing.</summary>
    public static void DrawLine(Vector3 start, Vector3 direction, float distance)
    {
        float length = direction.Length();
        if (length <= 0f || !float.IsFinite(length) || !float.IsFinite(distance)) return;
        DrawLine(start, start + direction * (distance / length));
    }

    /// <summary>Wireframe box in world space. Size is the full extent; Euler rotation is in degrees, as in Transform3D.</summary>
    public static void DrawBox(Vector3 center, Vector3 size, Vector3 rotation = default)
        => InternalCalls.Gizmo_DrawBox(center.X, center.Y, center.Z, size.X, size.Y, size.Z, rotation.X, rotation.Y, rotation.Z);

    /// <summary>Wireframe sphere made of three perpendicular great circles. Segments per circle are clamped to at least 3; nonpositive radii draw nothing.</summary>
    public static void DrawSphere(Vector3 center, float radius, int segments = 32)
        => InternalCalls.Gizmo_DrawSphere(center.X, center.Y, center.Z, radius, segments);

    /// <summary>Draws <paramref name="distance"/> units from <paramref name="start"/> along <paramref name="direction"/> (need not be normalized). A zero direction draws nothing.</summary>
    public static void DrawLine(Vector2 start, Vector2 direction, float distance)
    {
        float length = direction.Length();
        if (length <= 0f || float.IsNaN(length)) return;
        DrawLine(start, start + direction * (distance / length));
    }

    /// <summary>Outline-only square. <paramref name="degrees"/> rotates about the center.</summary>
    public static void DrawWireSquare(Vector2 center, Vector2 size, float degrees = 0.0f)
        => InternalCalls.Gizmo_DrawWireSquare(center.X, center.Y, size.X, size.Y, degrees);

    /// <summary>Outline-only circle approximated with <paramref name="segments"/> edges.</summary>
    public static void DrawWireCircle(Vector2 center, float radius, int segments = 32)
        => InternalCalls.Gizmo_DrawWireCircle(center.X, center.Y, radius, segments);

    /// <summary>Solid (filled) square. <paramref name="degrees"/> rotates about the center.</summary>
    public static void DrawSquare(Vector2 center, Vector2 size, float degrees = 0.0f)
        => InternalCalls.Gizmo_DrawSquare(center.X, center.Y, size.X, size.Y, degrees);

    /// <summary>Solid (filled) circle approximated with <paramref name="segments"/> triangles.</summary>
    public static void DrawCircle(Vector2 center, float radius, int segments = 32)
        => InternalCalls.Gizmo_DrawCircle(center.X, center.Y, radius, segments);

    /// <summary>
    /// World-space text centered on <paramref name="position"/>, using the project's
    /// default font and the current <see cref="Color"/>. <paramref name="rotation"/> is
    /// in degrees (about the position); <paramref name="size"/> is in pixels (same
    /// convention as a Text component's font size).
    /// </summary>
    public static void DrawText(Vector2 position, string text, float rotation = 0.0f, float size = 28.0f)
        => InternalCalls.Gizmo_DrawText(text, position.X, position.Y, rotation, size);
}
