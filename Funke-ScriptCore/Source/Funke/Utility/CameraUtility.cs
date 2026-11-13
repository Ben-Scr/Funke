
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

    public static Vector3 GetWorldMousePosition3D()
    {
        Camera3D? mainCamera = Camera3D.Main;

        if (mainCamera == null) throw new NullReferenceException("Main Camera 3D doesn't exist");
        return default;
    }
    public static Vector3 GetWorldMousePosition3D(Camera3D camera)
    {
        if (camera == null) throw new NullReferenceException("Camera 3D doesn't exist");

        return default;
    }
}
