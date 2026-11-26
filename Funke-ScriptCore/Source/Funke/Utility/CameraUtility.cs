using Funke.Physics;

namespace Funke;

public static class CameraUtility
{
    public static Vector2 GetWorldMousePosition2D()
    {
        Camera2D? mainCamera = Camera2D.Main;

        if (mainCamera == null) throw new NullReferenceException("Main Camera 2D doesn't exist");

        return mainCamera.ScreenToWorld(Input.MousePosition);
    }
    public static Vector2 GetWorldMousePosition2D(Camera2D camera)
    {
        if (camera == null) throw new NullReferenceException("Camera 2D doesn't exist");

        return camera.ScreenToWorld(Input.MousePosition);
    }

    /// <summary>Closest non-trigger collider within 1000 units from the near plane; throws without a main camera or hit.</summary>
    public static Vector3 GetWorldMousePosition3D()
    {
        if (!TryPickMouse3D(out RaycastHit3D hit)) throw new InvalidOperationException("No 3D collider under the mouse or no main 3D camera.");
        return hit.Point;
    }

    /// <summary>Closest non-trigger collider, within 1000 units from the near plane; throws when nothing is hit.</summary>
    public static Vector3 GetWorldMousePosition3D(Camera3D camera)
    {
        ArgumentNullException.ThrowIfNull(camera);
        if (!TryPickMouse3D(camera, out RaycastHit3D hit)) throw new InvalidOperationException("No 3D collider under the mouse.");
        return hit.Point;
    }

    public static Ray GetMouseRay3D() => GetMouseRay3D(Camera3D.Main ?? throw new InvalidOperationException("Main Camera 3D doesn't exist."));

    /// <summary>Starts on the near plane; throws if the camera cannot project the mouse position.</summary>
    public static Ray GetMouseRay3D(Camera3D camera)
    {
        ArgumentNullException.ThrowIfNull(camera);
        if (!camera.TryScreenPointToRay(Input.MousePosition, out Ray ray)) throw new InvalidOperationException("Camera cannot project the mouse position.");
        return ray;
    }

    public static bool TryGetMouseWorldPosition3D(Plane plane, out Vector3 point)
        => TryGetMouseWorldPosition3D(Camera3D.Main, plane, out point);

    /// <summary>False without a camera or a forward plane intersection; a hit at the world origin is valid.</summary>
    public static bool TryGetMouseWorldPosition3D(Camera3D? camera, Plane plane, out Vector3 point)
    {
        point = default;
        return camera != null && TryScreenPointToWorld3D(camera, Input.MousePosition, plane, out point);
    }

    public static bool TryScreenPointToWorld3D(Camera3D? camera, Vector2 screenPoint, Plane plane, out Vector3 point)
    {
        point = default;
        if (camera == null || !camera.TryScreenPointToRay(screenPoint, out Ray ray) || !plane.TryRaycast(ray, out float distance)) return false;
        Vector3 candidate = ray.GetPoint(distance);
        if (!Vector3.IsFinite(candidate)) return false;
        point = candidate;
        return true;
    }

    public static bool TryPickMouse3D(out RaycastHit3D hit, float maxDistance = 1000, PhysicsLayer? layers = null, bool includeTriggers = false)
        => TryPickMouse3D(Camera3D.Main, out hit, maxDistance, layers, includeTriggers);

    /// <summary>Play-mode query; distance starts at the near plane, and the mask filters collider body owners.</summary>
    public static bool TryPickMouse3D(Camera3D? camera, out RaycastHit3D hit, float maxDistance = 1000,
        PhysicsLayer? layers = null, bool includeTriggers = false)
    {
        hit = default;
        return camera != null && camera.TryScreenPointToRay(Input.MousePosition, out Ray ray)
            && Physics3D.Raycast(ray, out hit, layers ?? PhysicsLayer.All, maxDistance, includeTriggers);
    }
}
