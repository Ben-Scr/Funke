using System.Runtime.InteropServices;

namespace Funke.Interop;
/// <summary>
/// Layout must match the C++ NativeBindings struct exactly (Sequential, blittable).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeBindingsStruct
{
    // ── Application ──────────────────────────────────────────────
    public delegate* unmanaged<float> Application_GetDeltaTime;
    public delegate* unmanaged<float> Application_GetElapsedTime;
    public delegate* unmanaged<int> Application_GetScreenWidth;
    public delegate* unmanaged<int> Application_GetScreenHeight;
    public delegate* unmanaged<float> Application_GetTargetFrameRate;
    public delegate* unmanaged<float, void> Application_SetTargetFrameRate;
    public delegate* unmanaged<void> Application_Quit;
    public delegate* unmanaged<void> Application_Reload;
    public delegate* unmanaged<float> Application_GetFixedDeltaTime;
    public delegate* unmanaged<float, void> Application_SetFixedDeltaTime;
    public delegate* unmanaged<float> Application_GetUnscaledDeltaTime;
    public delegate* unmanaged<float> Application_GetFixedUnscaledDeltaTime;
    public delegate* unmanaged<float> Application_GetTimeScale;
    public delegate* unmanaged<float, void> Application_SetTimeScale;
    public delegate* unmanaged<int> Application_IsEditor; // 1 = host is editor, 0 = standalone runtime
    public delegate* unmanaged<byte*, int, int> Application_GetClipboardStringBuffer;
    public delegate* unmanaged<byte*, void> Application_SetClipboardString;
    public delegate* unmanaged<int> Application_GetVsyncEnabled;
    public delegate* unmanaged<int, void> Application_SetVsyncEnabled;

    // ── Window ───────────────────────────────────────────────────
    // Layout must match the Window_* block in ScriptGlue.hpp exactly.
    public delegate* unmanaged<int> Window_GetWidth;
    public delegate* unmanaged<int> Window_GetHeight;
    public delegate* unmanaged<byte*, int, int> Window_GetTitleBuffer;
    public delegate* unmanaged<byte*, void> Window_SetTitle;
    public delegate* unmanaged<void> Window_Minimize;
    public delegate* unmanaged<void> Window_Maximize;
    public delegate* unmanaged<int> Window_IsMaximized;
    public delegate* unmanaged<int> Window_IsFullScreen;
    public delegate* unmanaged<int, void> Window_SetFullScreen;
    public delegate* unmanaged<int*, int*, void> Window_GetPosition;
    public delegate* unmanaged<int, int, void> Window_SetPosition;
    public delegate* unmanaged<void> Window_Focus;
    public delegate* unmanaged<int*, int*, void> Window_GetScreenSize;

    // ── Engine ───────────────────────────────────────────────────
    public delegate* unmanaged<byte*, int, int> Engine_GetVersionBuffer;
    public delegate* unmanaged<byte*, int, int> Engine_GetVersionLongBuffer;
    public delegate* unmanaged<int> Engine_GetBuildConfiguration; // 0=Debug, 1=Development, 2=Release
    public delegate* unmanaged<byte*, int, int> Engine_GetPlatformBuffer;
    public delegate* unmanaged<byte*, int, int> Engine_GetGraphicsApiBuffer;
    public delegate* unmanaged<byte*, int, int> Engine_GetGpuVendorBuffer;
    public delegate* unmanaged<byte*, int, int> Engine_GetGpuRendererBuffer;

    // ── Time ─────────────────────────────────────────────────────
    public delegate* unmanaged<int> Time_GetFrameCount;
    public delegate* unmanaged<float> Time_GetTimeSinceStartup;
    public delegate* unmanaged<float> Time_GetRealtimeSinceStartup;

    // ── Log ──────────────────────────────────────────────────────
    public delegate* unmanaged<byte*, void> Log_Trace;
    public delegate* unmanaged<byte*, void> Log_Info;
    public delegate* unmanaged<byte*, void> Log_Warn;
    public delegate* unmanaged<byte*, void> Log_Error;

    // ── Input ────────────────────────────────────────────────────
    public delegate* unmanaged<int, int> Input_GetKey;
    public delegate* unmanaged<int, int> Input_GetKeyDown;
    public delegate* unmanaged<int, int> Input_GetKeyUp;
    public delegate* unmanaged<int> Input_GetAnyKey;
    public delegate* unmanaged<int, int> Input_GetMouseButton;
    public delegate* unmanaged<int, int> Input_GetMouseButtonDown;
    public delegate* unmanaged<int, int> Input_GetMouseButtonUp;
    public delegate* unmanaged<float*, float*, void> Input_GetMousePosition;
    public delegate* unmanaged<float*, float*, void> Input_GetAxis;
    public delegate* unmanaged<float*, float*, void> Input_GetMouseDelta;
    public delegate* unmanaged<float> Input_GetScrollWheelDelta;

    // ── Entity ───────────────────────────────────────────────────
    public delegate* unmanaged<ulong, int> Entity_IsValid;
    public delegate* unmanaged<byte*, ulong> Entity_FindByName;
    public delegate* unmanaged<ulong, void> Entity_Destroy;
    public delegate* unmanaged<byte*, ulong> Entity_Create;
    public delegate* unmanaged<ulong, ulong> Entity_Clone;
    public delegate* unmanaged<ulong, ulong> Entity_InstantiatePrefab;
    public delegate* unmanaged<ulong, int> Entity_GetOrigin;
    public delegate* unmanaged<ulong, ulong> Entity_GetRuntimeID;
    public delegate* unmanaged<ulong, ulong> Entity_GetSceneGUID;
    public delegate* unmanaged<ulong, ulong> Entity_GetPrefabGUID;
    public delegate* unmanaged<ulong, byte*, int> Entity_HasComponent;
    public delegate* unmanaged<ulong, byte*, int> Entity_AddComponent;
    public delegate* unmanaged<ulong, byte*, int> Entity_RemoveComponent;
    public delegate* unmanaged<ulong, byte*, int> Entity_AddScript;
    public delegate* unmanaged<ulong, byte*, int> Entity_HasScript;
    public delegate* unmanaged<ulong, byte*, int> Entity_RemoveScript;
    public delegate* unmanaged<ulong, float, void> Entity_DestroyDelayed;
    public delegate* unmanaged<ulong, byte*, byte*, int, int> Entity_GetManagedComponentFieldsBuffer;
    public delegate* unmanaged<ulong, int> Entity_GetIsStatic;
    public delegate* unmanaged<ulong, int, void> Entity_SetIsStatic;
    public delegate* unmanaged<ulong, int> Entity_GetIsEnabled;
    public delegate* unmanaged<ulong, int> Entity_GetIsEnabledInHierarchy;
    public delegate* unmanaged<ulong, int, void> Entity_SetIsEnabled;

    // ── NameComponent ────────────────────────────────────────────
    public delegate* unmanaged<ulong, byte*, int, int> NameComponent_GetNameBuffer;
    public delegate* unmanaged<ulong, byte*, void> NameComponent_SetName;

    // ── Transform2D ──────────────────────────────────────────────
    public delegate* unmanaged<ulong, float*, float*, void> Transform2D_GetPosition;
    public delegate* unmanaged<ulong, float, float, void> Transform2D_SetPosition;
    public delegate* unmanaged<ulong, float> Transform2D_GetRotation;
    public delegate* unmanaged<ulong, float, void> Transform2D_SetRotation;
    public delegate* unmanaged<ulong, float*, float*, void> Transform2D_GetScale;
    public delegate* unmanaged<ulong, float, float, void> Transform2D_SetScale;
    public delegate* unmanaged<ulong, ulong> Transform2D_GetEntity;
    public delegate* unmanaged<ulong, float*, float*, void> Transform2D_GetLocalPosition;
    public delegate* unmanaged<ulong, float, float, void> Transform2D_SetLocalPosition;
    public delegate* unmanaged<ulong, float> Transform2D_GetLocalRotation;
    public delegate* unmanaged<ulong, float, void> Transform2D_SetLocalRotation;
    public delegate* unmanaged<ulong, float*, float*, void> Transform2D_GetLocalScale;
    public delegate* unmanaged<ulong, float, float, void> Transform2D_SetLocalScale;
    // Hierarchy is entity-level (HierarchyComponent); slots stay here to match the locked native struct order.
    public delegate* unmanaged<ulong, ulong> Entity_GetParent;
    public delegate* unmanaged<ulong, ulong, int, int> Entity_SetParent;
    public delegate* unmanaged<ulong, int> Entity_GetChildCount;
    public delegate* unmanaged<ulong, int, ulong> Entity_GetChildAt;
    public delegate* unmanaged<ulong, ulong*, int, int> Entity_GetChildren;

    // ── SpriteRenderer ───────────────────────────────────────────
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> SpriteRenderer_GetColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> SpriteRenderer_SetColor;
    public delegate* unmanaged<ulong, ulong> SpriteRenderer_GetTexture;
    public delegate* unmanaged<ulong, ulong, void> SpriteRenderer_SetTexture;
    public delegate* unmanaged<ulong, int> SpriteRenderer_GetSortingOrder;
    public delegate* unmanaged<ulong, int, void> SpriteRenderer_SetSortingOrder;
    public delegate* unmanaged<ulong, int> SpriteRenderer_GetSortingLayer;
    public delegate* unmanaged<ulong, int, void> SpriteRenderer_SetSortingLayer;

    // ── TextRenderer ─────────────────────────────────────────────
    public delegate* unmanaged<ulong, byte*, int, int> TextRenderer_GetTextBuffer;
    public delegate* unmanaged<ulong, byte*, void> TextRenderer_SetText;
    public delegate* unmanaged<ulong, ulong> TextRenderer_GetFont;
    public delegate* unmanaged<ulong, ulong, void> TextRenderer_SetFont;
    public delegate* unmanaged<ulong, float> TextRenderer_GetFontSize;
    public delegate* unmanaged<ulong, float, void> TextRenderer_SetFontSize;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> TextRenderer_GetColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> TextRenderer_SetColor;
    public delegate* unmanaged<ulong, float> TextRenderer_GetLetterSpacing;
    public delegate* unmanaged<ulong, float, void> TextRenderer_SetLetterSpacing;
    public delegate* unmanaged<ulong, float> TextRenderer_GetLineSpacing;
    public delegate* unmanaged<ulong, float, void> TextRenderer_SetLineSpacing;
    public delegate* unmanaged<ulong, int> TextRenderer_GetHAlign;
    public delegate* unmanaged<ulong, int, void> TextRenderer_SetHAlign;
    public delegate* unmanaged<ulong, int> TextRenderer_GetVAlign;
    public delegate* unmanaged<ulong, int, void> TextRenderer_SetVAlign;
    public delegate* unmanaged<ulong, int> TextRenderer_GetWrapMode;
    public delegate* unmanaged<ulong, int, void> TextRenderer_SetWrapMode;
    // WrapWidth slots removed — wrap area now comes from the host
    // RectTransform2D's width minus the TextRenderer's Margin (.x + .z).
    public delegate* unmanaged<ulong, int> TextRenderer_GetSortingOrder;
    public delegate* unmanaged<ulong, int, void> TextRenderer_SetSortingOrder;
    public delegate* unmanaged<ulong, int> TextRenderer_GetSortingLayer;
    public delegate* unmanaged<ulong, int, void> TextRenderer_SetSortingLayer;

    // ── Camera2D ─────────────────────────────────────────────────
    public delegate* unmanaged<ulong, float> Camera2D_GetOrthographicSize;
    public delegate* unmanaged<ulong, float, void> Camera2D_SetOrthographicSize;
    public delegate* unmanaged<ulong, float> Camera2D_GetZoom;
    public delegate* unmanaged<ulong, float, void> Camera2D_SetZoom;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Camera2D_GetClearColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Camera2D_SetClearColor;
    public delegate* unmanaged<ulong, float, float, float*, float*, void> Camera2D_ScreenToWorld;
    public delegate* unmanaged<ulong, float> Camera2D_GetViewportWidth;
    public delegate* unmanaged<ulong, float> Camera2D_GetViewportHeight;
    public delegate* unmanaged<ulong> Camera2D_GetMainEntity;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Camera2D_GetFrustum;

    // ── Rigidbody2D ──────────────────────────────────────────────
    public delegate* unmanaged<ulong, float, float, int, void> Rigidbody2D_ApplyForce;
    public delegate* unmanaged<ulong, float, float, int, void> Rigidbody2D_ApplyImpulse;
    public delegate* unmanaged<ulong, float*, float*, void> Rigidbody2D_GetLinearVelocity;
    public delegate* unmanaged<ulong, float, float, void> Rigidbody2D_SetLinearVelocity;
    public delegate* unmanaged<ulong, float> Rigidbody2D_GetAngularVelocity;
    public delegate* unmanaged<ulong, float, void> Rigidbody2D_SetAngularVelocity;
    public delegate* unmanaged<ulong, int> Rigidbody2D_GetBodyType;
    public delegate* unmanaged<ulong, int, void> Rigidbody2D_SetBodyType;
    public delegate* unmanaged<ulong, float> Rigidbody2D_GetGravityScale;
    public delegate* unmanaged<ulong, float, void> Rigidbody2D_SetGravityScale;
    public delegate* unmanaged<ulong, float> Rigidbody2D_GetMass;
    public delegate* unmanaged<ulong, float, void> Rigidbody2D_SetMass;

    // ── BoxCollider2D ────────────────────────────────────────────
    public delegate* unmanaged<ulong, float*, float*, void> BoxCollider2D_GetScale;
    public delegate* unmanaged<ulong, float*, float*, void> BoxCollider2D_GetCenter;
    public delegate* unmanaged<ulong, int, void> BoxCollider2D_SetEnabled;

    // ── CircleCollider2D ─────────────────────────────────────────
    public delegate* unmanaged<ulong, float> CircleCollider2D_GetRadius;
    public delegate* unmanaged<ulong, float, void> CircleCollider2D_SetRadius;
    public delegate* unmanaged<ulong, float*, float*, void> CircleCollider2D_GetCenter;
    public delegate* unmanaged<ulong, float, float, void> CircleCollider2D_SetCenter;
    public delegate* unmanaged<ulong, int, void> CircleCollider2D_SetEnabled;

    // ── PolygonCollider2D ────────────────────────────────────────
    public delegate* unmanaged<ulong, int> PolygonCollider2D_GetVertexCount;
    public delegate* unmanaged<ulong, float*, int, int> PolygonCollider2D_GetWorldPoints;
    public delegate* unmanaged<ulong, float*, int, void> PolygonCollider2D_SetPoints;
    public delegate* unmanaged<ulong, int, void> PolygonCollider2D_SetSides;
    public delegate* unmanaged<ulong, float*, float*, void> PolygonCollider2D_GetCenter;
    public delegate* unmanaged<ulong, float, float, void> PolygonCollider2D_SetCenter;
    public delegate* unmanaged<ulong, float*, float*, void> PolygonCollider2D_GetSize;
    public delegate* unmanaged<ulong, float, float, void> PolygonCollider2D_SetSize;
    public delegate* unmanaged<ulong, int, void> PolygonCollider2D_SetEnabled;

    // ── AudioSource ──────────────────────────────────────────────
    public delegate* unmanaged<ulong, void> AudioSource_Play;
    public delegate* unmanaged<ulong, void> AudioSource_Pause;
    public delegate* unmanaged<ulong, void> AudioSource_Stop;
    public delegate* unmanaged<ulong, void> AudioSource_Resume;
    public delegate* unmanaged<ulong, float> AudioSource_GetVolume;
    public delegate* unmanaged<ulong, float, void> AudioSource_SetVolume;
    public delegate* unmanaged<ulong, float> AudioSource_GetPitch;
    public delegate* unmanaged<ulong, float, void> AudioSource_SetPitch;
    public delegate* unmanaged<ulong, int> AudioSource_GetLoop;
    public delegate* unmanaged<ulong, int, void> AudioSource_SetLoop;
    public delegate* unmanaged<ulong, int> AudioSource_IsPlaying;
    public delegate* unmanaged<ulong, int> AudioSource_IsPaused;
    public delegate* unmanaged<ulong, ulong> AudioSource_GetAudio;
    public delegate* unmanaged<ulong, ulong, void> AudioSource_SetAudio;


    // ── Scene Query ──────────────────────────────────────────────
    public delegate* unmanaged<byte*, int, int> Scene_GetActiveSceneNameBuffer;
    public delegate* unmanaged<int> Scene_GetEntityCount;
    public delegate* unmanaged<byte*, int> Scene_GetEntityCountByName;
    public delegate* unmanaged<byte*, int> Scene_LoadAdditive;
    public delegate* unmanaged<byte*, int> Scene_Load;
    public delegate* unmanaged<byte*, void> Scene_Unload;
    public delegate* unmanaged<byte*, int> Scene_SetActive;
    public delegate* unmanaged<byte*, int> Scene_Reload;
    public delegate* unmanaged<byte*, byte*, int, int> Scene_SetSceneSystemEnabled;
    public delegate* unmanaged<byte*, byte*, int> Scene_IsSceneSystemEnabled;
    public delegate* unmanaged<byte*, int, void> Scene_SetGlobalSystemEnabled;
    public delegate* unmanaged<byte*, int> Scene_DoesSceneExist;
    public delegate* unmanaged<int> Scene_GetLoadedCount;
    public delegate* unmanaged<int, byte*, int, int> Scene_GetLoadedSceneNameAtBuffer;
    public delegate* unmanaged<ulong, byte*, int, int> Scene_GetEntityNameByUUIDBuffer;
    public delegate* unmanaged<byte*, ulong*, int, int> Scene_QueryEntities;
    public delegate* unmanaged<byte*, byte*, byte*, int, ulong*, int, int> Scene_QueryEntitiesFiltered;
    public delegate* unmanaged<byte*, byte*, ulong*, int, int> Scene_QueryEntitiesInScene;
    public delegate* unmanaged<byte*, byte*, byte*, byte*, int, ulong*, int, int> Scene_QueryEntitiesFilteredInScene;
    public delegate* unmanaged<ulong, int> Asset_IsValid;
    public delegate* unmanaged<byte*, ulong> Asset_GetOrCreateUUIDFromPath;
    public delegate* unmanaged<ulong, byte*, int, int> Asset_GetPathBuffer;
    public delegate* unmanaged<ulong, byte*, int, int> Asset_GetDisplayNameBuffer;
    public delegate* unmanaged<ulong, int> Asset_GetKind;
    public delegate* unmanaged<byte*, int, byte*, int, int> Asset_FindAllBuffer;
    public delegate* unmanaged<ulong, int> Texture_LoadAsset;
    public delegate* unmanaged<ulong, int> Texture_GetWidth;
    public delegate* unmanaged<ulong, int> Texture_GetHeight;
    public delegate* unmanaged<byte, ulong> Texture_GetDefaultAssetUUID;
    // Runtime/procedural textures: (width, height, rgba*, filter) -> uuid;
    // (uuid, rgba*, byteCount) -> ok; (uuid) -> void.
    public delegate* unmanaged<int, int, byte*, int, ulong> Texture_CreateRuntime;
    public delegate* unmanaged<ulong, byte*, int, int>      Texture_UpdateRuntime;
    public delegate* unmanaged<ulong, void>                 Texture_DestroyRuntime;
    public delegate* unmanaged<ulong, int>                  Texture_IsRuntime;
    public delegate* unmanaged<ulong, int> Audio_LoadAsset;
    public delegate* unmanaged<ulong, float, void> Audio_PlayOneShotAsset;
    public delegate* unmanaged<ulong, int> Font_LoadAsset;

    // ── ParticleSystem2D ─────────────────────────────────────────
    public delegate* unmanaged<ulong, void> ParticleSystem2D_Play;
    public delegate* unmanaged<ulong, void> ParticleSystem2D_Pause;
    public delegate* unmanaged<ulong, void> ParticleSystem2D_Stop;
    public delegate* unmanaged<ulong, int> ParticleSystem2D_IsPlaying;
    public delegate* unmanaged<ulong, int> ParticleSystem2D_GetPlayOnAwake;
    public delegate* unmanaged<ulong, int, void> ParticleSystem2D_SetPlayOnAwake;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> ParticleSystem2D_GetColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> ParticleSystem2D_SetColor;
    public delegate* unmanaged<ulong, float> ParticleSystem2D_GetLifeTime;
    public delegate* unmanaged<ulong, float, void> ParticleSystem2D_SetLifeTime;
    public delegate* unmanaged<ulong, float> ParticleSystem2D_GetSpeed;
    public delegate* unmanaged<ulong, float, void> ParticleSystem2D_SetSpeed;
    public delegate* unmanaged<ulong, float*, float*, void> ParticleSystem2D_GetScale;
    public delegate* unmanaged<ulong, float, float, void> ParticleSystem2D_SetScale;
    public delegate* unmanaged<ulong, int> ParticleSystem2D_GetEmitOverTime;
    public delegate* unmanaged<ulong, int, void> ParticleSystem2D_SetEmitOverTime;
    public delegate* unmanaged<ulong, int, void> ParticleSystem2D_Emit;

    // ── Gizmos ───────────────────────────────────────────────────
    public delegate* unmanaged<float, float, float, float, void> Gizmo_DrawLine;
    public delegate* unmanaged<float, float, float, float, float, void> Gizmo_DrawWireSquare;
    public delegate* unmanaged<float, float, float, int, void> Gizmo_DrawWireCircle;
    public delegate* unmanaged<float, float, float, float, void> Gizmo_SetColor;
    public delegate* unmanaged<float*, float*, float*, float*, void> Gizmo_GetColor;
    public delegate* unmanaged<float> Gizmo_GetLineWidth;
    public delegate* unmanaged<float, void> Gizmo_SetLineWidth;
    public delegate* unmanaged<int, void> Gizmo_SetMaxVertices;
    public delegate* unmanaged<int> Gizmo_GetMaxVertices;
    public delegate* unmanaged<int> Gizmo_GetRegisteredVertices;

    // ── Physics2D ────────────────────────────────────────────────
    public delegate* unmanaged<float, float, float, float, float, ulong*, float*, float*, float*, float*, float*, int> Physics2D_Raycast;
    public delegate* unmanaged<float, float, float, int, ulong*, int> Physics2D_OverlapCircle;
    public delegate* unmanaged<float, float, float, float, float, int, ulong*, int> Physics2D_OverlapBox;
    public delegate* unmanaged<float, float, float*, int, int, ulong*, int> Physics2D_OverlapPolygon;
    public delegate* unmanaged<float, float, float, ulong*, int, int> Physics2D_OverlapCircleAll;
    public delegate* unmanaged<float, float, float, float, float, ulong*, int, int> Physics2D_OverlapBoxAll;
    public delegate* unmanaged<float, float, float*, int, ulong*, int, int> Physics2D_OverlapPolygonAll;
    public delegate* unmanaged<float, float, int, ulong*, int> Physics2D_ContainsPoint;
    public delegate* unmanaged<float, float, ulong*, int, int> Physics2D_ContainsPointAll;


    // ── EntityPicker ─────────────────────────────────────────────
    public delegate* unmanaged<float, float, ulong*, int> EntityPicker_TryPickEntity;

    // ── UI: RectTransform2D ──────────────────────────────────────
    public delegate* unmanaged<ulong, float*, float*, void> RectTransform_GetAnchorMin;
    public delegate* unmanaged<ulong, float, float, void> RectTransform_SetAnchorMin;
    public delegate* unmanaged<ulong, float*, float*, void> RectTransform_GetAnchorMax;
    public delegate* unmanaged<ulong, float, float, void> RectTransform_SetAnchorMax;
    public delegate* unmanaged<ulong, float*, float*, void> RectTransform_GetPivot;
    public delegate* unmanaged<ulong, float, float, void> RectTransform_SetPivot;
    public delegate* unmanaged<ulong, float*, float*, void> RectTransform_GetAnchoredPosition;
    public delegate* unmanaged<ulong, float, float, void> RectTransform_SetAnchoredPosition;
    public delegate* unmanaged<ulong, float*, float*, void> RectTransform_GetSizeDelta;
    public delegate* unmanaged<ulong, float, float, void> RectTransform_SetSizeDelta;
    public delegate* unmanaged<ulong, float> RectTransform_GetRotation;
    public delegate* unmanaged<ulong, float, void> RectTransform_SetRotation;
    public delegate* unmanaged<ulong, float*, float*, void> RectTransform_GetScale;
    public delegate* unmanaged<ulong, float, float, void> RectTransform_SetScale;
    public delegate* unmanaged<ulong, float> RectTransform_GetLocalRotation;
    public delegate* unmanaged<ulong, float, void> RectTransform_SetLocalRotation;
    public delegate* unmanaged<ulong, float*, float*, void> RectTransform_GetLocalScale;
    public delegate* unmanaged<ulong, float, float, void> RectTransform_SetLocalScale;
    public delegate* unmanaged<ulong, float*, float*, void> RectTransform_GetResolvedSize;

    // ── UI: Image ────────────────────────────────────────────────
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Image_GetColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Image_SetColor;
    public delegate* unmanaged<ulong, ulong> Image_GetTexture;
    public delegate* unmanaged<ulong, ulong, void> Image_SetTexture;
    public delegate* unmanaged<ulong, int> Image_GetSortingOrder;
    public delegate* unmanaged<ulong, int, void> Image_SetSortingOrder;
    public delegate* unmanaged<ulong, int> Image_GetSortingLayer;
    public delegate* unmanaged<ulong, int, void> Image_SetSortingLayer;

    // ── UI: Interactable ─────────────────────────────────────────
    public delegate* unmanaged<ulong, int> Interactable_GetInteractable;
    public delegate* unmanaged<ulong, int, void> Interactable_SetInteractable;
    public delegate* unmanaged<ulong, int> Interactable_GetIsHovered;
    public delegate* unmanaged<ulong, int> Interactable_GetIsClicked;
    public delegate* unmanaged<ulong, int> Interactable_GetIsPressed;
    public delegate* unmanaged<ulong, int> Interactable_GetIsMouseDown;
    public delegate* unmanaged<ulong, int> Interactable_GetIsMouseUp;
    // Optional focus / selection navigation. Order must match
    // ScriptGlue.hpp's NativeBindings struct exactly.
    public delegate* unmanaged<ulong, int> Interactable_GetFocusable;
    public delegate* unmanaged<ulong, int, void> Interactable_SetFocusable;
    public delegate* unmanaged<ulong, int> Interactable_GetIsFocused;
    public delegate* unmanaged<ulong, int, void> Interactable_SetIsFocused;

    // ── UI: Button ───────────────────────────────────────────────
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Button_GetNormalColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Button_SetNormalColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Button_GetHoveredColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Button_SetHoveredColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Button_GetPressedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Button_SetPressedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Button_GetDisabledColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Button_SetDisabledColor;
    // Per-widget FocusedColor accessors are grouped together below
    // (still inside Button's logical block in C#) because the C++
    // side declares them in one block right after Button_SetDisabledColor
    // and the layout has to match.
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Button_GetFocusedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Button_SetFocusedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Toggle_GetFocusedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Toggle_SetFocusedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Slider_GetFocusedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Slider_SetFocusedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> InputField_GetFocusedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> InputField_SetFocusedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetFocusedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetFocusedColor;

    // ── UI: TransitionMode + per-state sprite UUIDs ──────────────
    // Order must match ScriptGlue.hpp's NativeBindings struct exactly.
    public delegate* unmanaged<ulong, int> Button_GetTransitionMode;
    public delegate* unmanaged<ulong, int, void> Button_SetTransitionMode;
    public delegate* unmanaged<ulong, int> Toggle_GetTransitionMode;
    public delegate* unmanaged<ulong, int, void> Toggle_SetTransitionMode;
    public delegate* unmanaged<ulong, int> Slider_GetTransitionMode;
    public delegate* unmanaged<ulong, int, void> Slider_SetTransitionMode;
    public delegate* unmanaged<ulong, int> InputField_GetTransitionMode;
    public delegate* unmanaged<ulong, int, void> InputField_SetTransitionMode;
    public delegate* unmanaged<ulong, int> Dropdown_GetTransitionMode;
    public delegate* unmanaged<ulong, int, void> Dropdown_SetTransitionMode;

    public delegate* unmanaged<ulong, ulong> Button_GetNormalSprite;
    public delegate* unmanaged<ulong, ulong, void> Button_SetNormalSprite;
    public delegate* unmanaged<ulong, ulong> Button_GetHoveredSprite;
    public delegate* unmanaged<ulong, ulong, void> Button_SetHoveredSprite;
    public delegate* unmanaged<ulong, ulong> Button_GetPressedSprite;
    public delegate* unmanaged<ulong, ulong, void> Button_SetPressedSprite;
    public delegate* unmanaged<ulong, ulong> Button_GetDisabledSprite;
    public delegate* unmanaged<ulong, ulong, void> Button_SetDisabledSprite;
    public delegate* unmanaged<ulong, ulong> Button_GetFocusedSprite;
    public delegate* unmanaged<ulong, ulong, void> Button_SetFocusedSprite;

    public delegate* unmanaged<ulong, ulong> Toggle_GetNormalSprite;
    public delegate* unmanaged<ulong, ulong, void> Toggle_SetNormalSprite;
    public delegate* unmanaged<ulong, ulong> Toggle_GetHoveredSprite;
    public delegate* unmanaged<ulong, ulong, void> Toggle_SetHoveredSprite;
    public delegate* unmanaged<ulong, ulong> Toggle_GetPressedSprite;
    public delegate* unmanaged<ulong, ulong, void> Toggle_SetPressedSprite;
    public delegate* unmanaged<ulong, ulong> Toggle_GetDisabledSprite;
    public delegate* unmanaged<ulong, ulong, void> Toggle_SetDisabledSprite;
    public delegate* unmanaged<ulong, ulong> Toggle_GetFocusedSprite;
    public delegate* unmanaged<ulong, ulong, void> Toggle_SetFocusedSprite;

    public delegate* unmanaged<ulong, ulong> Slider_GetNormalSprite;
    public delegate* unmanaged<ulong, ulong, void> Slider_SetNormalSprite;
    public delegate* unmanaged<ulong, ulong> Slider_GetHoveredSprite;
    public delegate* unmanaged<ulong, ulong, void> Slider_SetHoveredSprite;
    public delegate* unmanaged<ulong, ulong> Slider_GetPressedSprite;
    public delegate* unmanaged<ulong, ulong, void> Slider_SetPressedSprite;
    public delegate* unmanaged<ulong, ulong> Slider_GetDisabledSprite;
    public delegate* unmanaged<ulong, ulong, void> Slider_SetDisabledSprite;
    public delegate* unmanaged<ulong, ulong> Slider_GetFocusedSprite;
    public delegate* unmanaged<ulong, ulong, void> Slider_SetFocusedSprite;

    public delegate* unmanaged<ulong, ulong> InputField_GetNormalSprite;
    public delegate* unmanaged<ulong, ulong, void> InputField_SetNormalSprite;
    public delegate* unmanaged<ulong, ulong> InputField_GetHoveredSprite;
    public delegate* unmanaged<ulong, ulong, void> InputField_SetHoveredSprite;
    public delegate* unmanaged<ulong, ulong> InputField_GetPressedSprite;
    public delegate* unmanaged<ulong, ulong, void> InputField_SetPressedSprite;
    public delegate* unmanaged<ulong, ulong> InputField_GetDisabledSprite;
    public delegate* unmanaged<ulong, ulong, void> InputField_SetDisabledSprite;
    public delegate* unmanaged<ulong, ulong> InputField_GetFocusedSprite;
    public delegate* unmanaged<ulong, ulong, void> InputField_SetFocusedSprite;

    public delegate* unmanaged<ulong, ulong> Dropdown_GetNormalSprite;
    public delegate* unmanaged<ulong, ulong, void> Dropdown_SetNormalSprite;
    public delegate* unmanaged<ulong, ulong> Dropdown_GetHoveredSprite;
    public delegate* unmanaged<ulong, ulong, void> Dropdown_SetHoveredSprite;
    public delegate* unmanaged<ulong, ulong> Dropdown_GetPressedSprite;
    public delegate* unmanaged<ulong, ulong, void> Dropdown_SetPressedSprite;
    public delegate* unmanaged<ulong, ulong> Dropdown_GetDisabledSprite;
    public delegate* unmanaged<ulong, ulong, void> Dropdown_SetDisabledSprite;
    public delegate* unmanaged<ulong, ulong> Dropdown_GetFocusedSprite;
    public delegate* unmanaged<ulong, ulong, void> Dropdown_SetFocusedSprite;

    // ── UI: IsReadOnly + entity-ref + popup-option colors ────────
    // Order must match ScriptGlue.hpp's NativeBindings struct.
    public delegate* unmanaged<ulong, int> Toggle_GetIsReadOnly;
    public delegate* unmanaged<ulong, int, void> Toggle_SetIsReadOnly;
    public delegate* unmanaged<ulong, int> Slider_GetIsReadOnly;
    public delegate* unmanaged<ulong, int, void> Slider_SetIsReadOnly;
    public delegate* unmanaged<ulong, int> Dropdown_GetIsReadOnly;
    public delegate* unmanaged<ulong, int, void> Dropdown_SetIsReadOnly;

    public delegate* unmanaged<ulong, ulong> Button_GetTargetGraphic;
    public delegate* unmanaged<ulong, ulong, void> Button_SetTargetGraphic;
    public delegate* unmanaged<ulong, ulong> Slider_GetFillEntity;
    public delegate* unmanaged<ulong, ulong, void> Slider_SetFillEntity;
    public delegate* unmanaged<ulong, ulong> Slider_GetHandleEntity;
    public delegate* unmanaged<ulong, ulong, void> Slider_SetHandleEntity;
    public delegate* unmanaged<ulong, ulong> Slider_GetBackgroundEntity;
    public delegate* unmanaged<ulong, ulong, void> Slider_SetBackgroundEntity;
    public delegate* unmanaged<ulong, ulong> Toggle_GetCheckmarkEntity;
    public delegate* unmanaged<ulong, ulong, void> Toggle_SetCheckmarkEntity;
    public delegate* unmanaged<ulong, ulong> InputField_GetTextEntity;
    public delegate* unmanaged<ulong, ulong, void> InputField_SetTextEntity;
    public delegate* unmanaged<ulong, ulong> Dropdown_GetLabelEntity;
    public delegate* unmanaged<ulong, ulong, void> Dropdown_SetLabelEntity;

    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetOptionNormalColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetOptionNormalColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetOptionHoverColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetOptionHoverColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetOptionPressedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetOptionPressedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetOptionSelectedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetOptionSelectedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetPopupBackgroundColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetPopupBackgroundColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetOptionTextColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetOptionTextColor;

    // ── UI: Slider ───────────────────────────────────────────────
    public delegate* unmanaged<ulong, float> Slider_GetValue;
    public delegate* unmanaged<ulong, float, void> Slider_SetValue;
    public delegate* unmanaged<ulong, float> Slider_GetMinValue;
    public delegate* unmanaged<ulong, float, void> Slider_SetMinValue;
    public delegate* unmanaged<ulong, float> Slider_GetMaxValue;
    public delegate* unmanaged<ulong, float, void> Slider_SetMaxValue;
    public delegate* unmanaged<ulong, int> Slider_GetWholeNumbers;
    public delegate* unmanaged<ulong, int, void> Slider_SetWholeNumbers;
    public delegate* unmanaged<ulong, int> Slider_GetValueChangedThisFrame;
    public delegate* unmanaged<ulong, void> Slider_MarkValueObserved;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Slider_GetNormalColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Slider_SetNormalColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Slider_GetHoveredColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Slider_SetHoveredColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Slider_GetPressedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Slider_SetPressedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Slider_GetDisabledColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Slider_SetDisabledColor;

    // ── UI: Toggle ───────────────────────────────────────────────
    public delegate* unmanaged<ulong, int> Toggle_GetIsOn;
    public delegate* unmanaged<ulong, int, void> Toggle_SetIsOn;
    public delegate* unmanaged<ulong, int> Toggle_GetValueChangedThisFrame;
    public delegate* unmanaged<ulong, void> Toggle_MarkIsOnObserved;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Toggle_GetNormalColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Toggle_SetNormalColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Toggle_GetHoveredColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Toggle_SetHoveredColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Toggle_GetPressedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Toggle_SetPressedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Toggle_GetDisabledColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Toggle_SetDisabledColor;

    // ── UI: InputField ───────────────────────────────────────────
    public delegate* unmanaged<ulong, byte*, int, int> InputField_GetTextBuffer;
    public delegate* unmanaged<ulong, byte*, void> InputField_SetText;
    public delegate* unmanaged<ulong, byte*, int, int> InputField_GetPlaceholderTextBuffer;
    public delegate* unmanaged<ulong, byte*, void> InputField_SetPlaceholderText;
    public delegate* unmanaged<ulong, int> InputField_GetIsFocused;
    public delegate* unmanaged<ulong, int, void> InputField_SetIsFocused;
    public delegate* unmanaged<ulong, int> InputField_GetSubmittedThisFrame;
    public delegate* unmanaged<ulong, int> InputField_GetCharacterLimit;
    public delegate* unmanaged<ulong, int, void> InputField_SetCharacterLimit;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> InputField_GetNormalColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> InputField_SetNormalColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> InputField_GetHoveredColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> InputField_SetHoveredColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> InputField_GetPressedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> InputField_SetPressedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> InputField_GetDisabledColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> InputField_SetDisabledColor;

    // ── UI: Dropdown ─────────────────────────────────────────────
    public delegate* unmanaged<ulong, int> Dropdown_GetSelectedIndex;
    public delegate* unmanaged<ulong, int, void> Dropdown_SetSelectedIndex;
    public delegate* unmanaged<ulong, int> Dropdown_GetIsOpen;
    public delegate* unmanaged<ulong, int, void> Dropdown_SetIsOpen;
    public delegate* unmanaged<ulong, int> Dropdown_GetSelectionChangedThisFrame;
    public delegate* unmanaged<ulong, void> Dropdown_MarkSelectedIndexObserved;
    public delegate* unmanaged<ulong, int> Dropdown_GetOptionCount;
    public delegate* unmanaged<ulong, int, byte*, int, int> Dropdown_GetOptionBuffer;
    public delegate* unmanaged<ulong, int, byte*, void> Dropdown_SetOption;
    public delegate* unmanaged<ulong, byte*, void> Dropdown_AddOption;
    public delegate* unmanaged<ulong, int, void> Dropdown_RemoveOption;
    public delegate* unmanaged<ulong, void> Dropdown_ClearOptions;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetNormalColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetNormalColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetHoveredColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetHoveredColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetPressedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetPressedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Dropdown_GetDisabledColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Dropdown_SetDisabledColor;

    // ── ECS ref-API (appended for binary compat with ScriptGlue.hpp) ──
    public delegate* unmanaged<ulong, byte*, void*> Entity_GetComponentPtr;
    public delegate* unmanaged<byte*, int> Entity_GetComponentSize;
    public delegate* unmanaged<byte*, byte*, byte*, byte*, byte*, int, void**, int, int> Scene_OpenQueryView;

    // Core API appended after the ref-API block to preserve existing field order.
    public delegate* unmanaged<int> Application_GetRunInBackground;
    public delegate* unmanaged<int, void> Application_SetRunInBackground;
    public delegate* unmanaged<void> Window_Restore;
    public delegate* unmanaged<int> Cursor_GetMode;
    public delegate* unmanaged<int, void> Cursor_SetMode;
    public delegate* unmanaged<ulong> Cursor_GetTexture;
    public delegate* unmanaged<ulong, int, int, void> Cursor_SetTexture;

    // ── EntityCommandBuffer (appended for binary compat) ────────────
    // Component_GetTypeId: resolve name → stable u32; cached at AppDomain load, no per-call marshaling. Returns 0 for unknown names.
    public delegate* unmanaged<byte*, uint> Component_GetTypeId;

    // Ecb_Playback: single-call playback of a recorded ECB stream; returns created-entity count or negative error; fills outRuntimeIds in record order.
    public delegate* unmanaged<byte*, int, ulong*, int, int> Ecb_Playback;

    // ── JobSystem (appended for binary compat) ──────────────────────
    public delegate* unmanaged<
        delegate* unmanaged<void*, void>,   // work(context)
        void*,                              // context
        delegate* unmanaged<void*, void>,   // releaseContext(context) (called on Release)
        ulong> JobSystem_Enqueue;

    public delegate* unmanaged<
        int, int, int,                                 // begin, end, batchSize
        delegate* unmanaged<void*, int, int, void>,    // work(context, lo, hi)
        void*,                                         // context
        delegate* unmanaged<void*, void>,              // releaseContext(context)
        ulong> JobSystem_ParallelFor;

    public delegate* unmanaged<ulong, void> JobSystem_Wait;
    public delegate* unmanaged<ulong, int> JobSystem_IsComplete;
    public delegate* unmanaged<ulong, void> JobSystem_Release;
    public delegate* unmanaged<int> JobSystem_GetWorkerCount;
    public delegate* unmanaged<int> JobSystem_GetCallerWorkerIndex;

    // ── UI: Scrollbar ─────────────────────────────────────────────
    public delegate* unmanaged<ulong, float> Scrollbar_GetValue;
    public delegate* unmanaged<ulong, float, void> Scrollbar_SetValue;
    public delegate* unmanaged<ulong, float> Scrollbar_GetSize;
    public delegate* unmanaged<ulong, float, void> Scrollbar_SetSize;
    public delegate* unmanaged<ulong, int> Scrollbar_GetNumberOfSteps;
    public delegate* unmanaged<ulong, int, void> Scrollbar_SetNumberOfSteps;
    public delegate* unmanaged<ulong, int> Scrollbar_GetDirection;
    public delegate* unmanaged<ulong, int, void> Scrollbar_SetDirection;
    public delegate* unmanaged<ulong, int> Scrollbar_GetIsReadOnly;
    public delegate* unmanaged<ulong, int, void> Scrollbar_SetIsReadOnly;
    public delegate* unmanaged<ulong, ulong> Scrollbar_GetHandleEntity;
    public delegate* unmanaged<ulong, ulong, void> Scrollbar_SetHandleEntity;
    public delegate* unmanaged<ulong, int> Scrollbar_GetValueChangedThisFrame;
    public delegate* unmanaged<ulong, void> Scrollbar_MarkValueObserved;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Scrollbar_GetNormalColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Scrollbar_SetNormalColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Scrollbar_GetHoveredColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Scrollbar_SetHoveredColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Scrollbar_GetPressedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Scrollbar_SetPressedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Scrollbar_GetDisabledColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Scrollbar_SetDisabledColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Scrollbar_GetFocusedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Scrollbar_SetFocusedColor;
    public delegate* unmanaged<ulong, int> Scrollbar_GetTransitionMode;
    public delegate* unmanaged<ulong, int, void> Scrollbar_SetTransitionMode;
    public delegate* unmanaged<ulong, ulong> Scrollbar_GetNormalSprite;
    public delegate* unmanaged<ulong, ulong, void> Scrollbar_SetNormalSprite;
    public delegate* unmanaged<ulong, ulong> Scrollbar_GetHoveredSprite;
    public delegate* unmanaged<ulong, ulong, void> Scrollbar_SetHoveredSprite;
    public delegate* unmanaged<ulong, ulong> Scrollbar_GetPressedSprite;
    public delegate* unmanaged<ulong, ulong, void> Scrollbar_SetPressedSprite;
    public delegate* unmanaged<ulong, ulong> Scrollbar_GetDisabledSprite;
    public delegate* unmanaged<ulong, ulong, void> Scrollbar_SetDisabledSprite;
    public delegate* unmanaged<ulong, ulong> Scrollbar_GetFocusedSprite;
    public delegate* unmanaged<ulong, ulong, void> Scrollbar_SetFocusedSprite;

    // ── UI: ScrollRect ────────────────────────────────────────────
    public delegate* unmanaged<ulong, ulong> ScrollRect_GetContent;
    public delegate* unmanaged<ulong, ulong, void> ScrollRect_SetContent;
    public delegate* unmanaged<ulong, ulong> ScrollRect_GetViewport;
    public delegate* unmanaged<ulong, ulong, void> ScrollRect_SetViewport;
    public delegate* unmanaged<ulong, int> ScrollRect_GetHorizontal;
    public delegate* unmanaged<ulong, int, void> ScrollRect_SetHorizontal;
    public delegate* unmanaged<ulong, int> ScrollRect_GetVertical;
    public delegate* unmanaged<ulong, int, void> ScrollRect_SetVertical;
    public delegate* unmanaged<ulong, int> ScrollRect_GetMovementType;
    public delegate* unmanaged<ulong, int, void> ScrollRect_SetMovementType;
    public delegate* unmanaged<ulong, float> ScrollRect_GetElasticity;
    public delegate* unmanaged<ulong, float, void> ScrollRect_SetElasticity;
    public delegate* unmanaged<ulong, int> ScrollRect_GetInertia;
    public delegate* unmanaged<ulong, int, void> ScrollRect_SetInertia;
    public delegate* unmanaged<ulong, float> ScrollRect_GetDecelerationRate;
    public delegate* unmanaged<ulong, float, void> ScrollRect_SetDecelerationRate;
    public delegate* unmanaged<ulong, float> ScrollRect_GetScrollSensitivity;
    public delegate* unmanaged<ulong, float, void> ScrollRect_SetScrollSensitivity;
    public delegate* unmanaged<ulong, ulong> ScrollRect_GetHorizontalScrollbar;
    public delegate* unmanaged<ulong, ulong, void> ScrollRect_SetHorizontalScrollbar;
    public delegate* unmanaged<ulong, ulong> ScrollRect_GetVerticalScrollbar;
    public delegate* unmanaged<ulong, ulong, void> ScrollRect_SetVerticalScrollbar;
    public delegate* unmanaged<ulong, int> ScrollRect_GetHorizontalScrollbarVisibility;
    public delegate* unmanaged<ulong, int, void> ScrollRect_SetHorizontalScrollbarVisibility;
    public delegate* unmanaged<ulong, int> ScrollRect_GetVerticalScrollbarVisibility;
    public delegate* unmanaged<ulong, int, void> ScrollRect_SetVerticalScrollbarVisibility;
    public delegate* unmanaged<ulong, float> ScrollRect_GetHorizontalScrollbarSpacing;
    public delegate* unmanaged<ulong, float, void> ScrollRect_SetHorizontalScrollbarSpacing;
    public delegate* unmanaged<ulong, float> ScrollRect_GetVerticalScrollbarSpacing;
    public delegate* unmanaged<ulong, float, void> ScrollRect_SetVerticalScrollbarSpacing;
    public delegate* unmanaged<ulong, float*, float*, void> ScrollRect_GetNormalizedPosition;
    public delegate* unmanaged<ulong, float, float, void> ScrollRect_SetNormalizedPosition;
    public delegate* unmanaged<ulong, int> ScrollRect_GetValueChangedThisFrame;
    public delegate* unmanaged<ulong, void> ScrollRect_MarkValueObserved;

    // ── UI: Mask ──────────────────────────────────────────────────
    public delegate* unmanaged<ulong, int> Mask_GetShowMaskGraphic;
    public delegate* unmanaged<ulong, int, void> Mask_SetShowMaskGraphic;

    // ── UI: CircularSlider ────────────────────────────────────────
    public delegate* unmanaged<ulong, float> CircularSlider_GetValue;
    public delegate* unmanaged<ulong, float, void> CircularSlider_SetValue;
    public delegate* unmanaged<ulong, float> CircularSlider_GetMinValue;
    public delegate* unmanaged<ulong, float, void> CircularSlider_SetMinValue;
    public delegate* unmanaged<ulong, float> CircularSlider_GetMaxValue;
    public delegate* unmanaged<ulong, float, void> CircularSlider_SetMaxValue;
    public delegate* unmanaged<ulong, int> CircularSlider_GetWholeNumbers;
    public delegate* unmanaged<ulong, int, void> CircularSlider_SetWholeNumbers;
    public delegate* unmanaged<ulong, int> CircularSlider_GetIsReadOnly;
    public delegate* unmanaged<ulong, int, void> CircularSlider_SetIsReadOnly;
    public delegate* unmanaged<ulong, float> CircularSlider_GetStartAngleDegrees;
    public delegate* unmanaged<ulong, float, void> CircularSlider_SetStartAngleDegrees;
    public delegate* unmanaged<ulong, float> CircularSlider_GetSweepDegrees;
    public delegate* unmanaged<ulong, float, void> CircularSlider_SetSweepDegrees;
    public delegate* unmanaged<ulong, int> CircularSlider_GetClockwise;
    public delegate* unmanaged<ulong, int, void> CircularSlider_SetClockwise;
    public delegate* unmanaged<ulong, float> CircularSlider_GetRingThickness;
    public delegate* unmanaged<ulong, float, void> CircularSlider_SetRingThickness;
    public delegate* unmanaged<ulong, int> CircularSlider_GetRingSegments;
    public delegate* unmanaged<ulong, int, void> CircularSlider_SetRingSegments;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> CircularSlider_GetBackgroundColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> CircularSlider_SetBackgroundColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> CircularSlider_GetFillColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> CircularSlider_SetFillColor;
    public delegate* unmanaged<ulong, ulong> CircularSlider_GetHandleEntity;
    public delegate* unmanaged<ulong, ulong, void> CircularSlider_SetHandleEntity;
    public delegate* unmanaged<ulong, int> CircularSlider_GetValueChangedThisFrame;
    public delegate* unmanaged<ulong, void> CircularSlider_MarkValueObserved;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> CircularSlider_GetNormalColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> CircularSlider_SetNormalColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> CircularSlider_GetHoveredColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> CircularSlider_SetHoveredColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> CircularSlider_GetPressedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> CircularSlider_SetPressedColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> CircularSlider_GetDisabledColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> CircularSlider_SetDisabledColor;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> CircularSlider_GetFocusedColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> CircularSlider_SetFocusedColor;
    public delegate* unmanaged<ulong, int> CircularSlider_GetTransitionMode;
    public delegate* unmanaged<ulong, int, void> CircularSlider_SetTransitionMode;
    public delegate* unmanaged<ulong, ulong> CircularSlider_GetNormalSprite;
    public delegate* unmanaged<ulong, ulong, void> CircularSlider_SetNormalSprite;
    public delegate* unmanaged<ulong, ulong> CircularSlider_GetHoveredSprite;
    public delegate* unmanaged<ulong, ulong, void> CircularSlider_SetHoveredSprite;
    public delegate* unmanaged<ulong, ulong> CircularSlider_GetPressedSprite;
    public delegate* unmanaged<ulong, ulong, void> CircularSlider_SetPressedSprite;
    public delegate* unmanaged<ulong, ulong> CircularSlider_GetDisabledSprite;
    public delegate* unmanaged<ulong, ulong, void> CircularSlider_SetDisabledSprite;
    public delegate* unmanaged<ulong, ulong> CircularSlider_GetFocusedSprite;
    public delegate* unmanaged<ulong, ulong, void> CircularSlider_SetFocusedSprite;

    // ── UI: HorizontalLayoutGroup ─────────────────────────────────
    public delegate* unmanaged<ulong, float> HorizontalLayoutGroup_GetPaddingLeft;
    public delegate* unmanaged<ulong, float, void> HorizontalLayoutGroup_SetPaddingLeft;
    public delegate* unmanaged<ulong, float> HorizontalLayoutGroup_GetPaddingRight;
    public delegate* unmanaged<ulong, float, void> HorizontalLayoutGroup_SetPaddingRight;
    public delegate* unmanaged<ulong, float> HorizontalLayoutGroup_GetPaddingTop;
    public delegate* unmanaged<ulong, float, void> HorizontalLayoutGroup_SetPaddingTop;
    public delegate* unmanaged<ulong, float> HorizontalLayoutGroup_GetPaddingBottom;
    public delegate* unmanaged<ulong, float, void> HorizontalLayoutGroup_SetPaddingBottom;
    public delegate* unmanaged<ulong, float> HorizontalLayoutGroup_GetSpacing;
    public delegate* unmanaged<ulong, float, void> HorizontalLayoutGroup_SetSpacing;
    public delegate* unmanaged<ulong, int> HorizontalLayoutGroup_GetChildAlignment;
    public delegate* unmanaged<ulong, int, void> HorizontalLayoutGroup_SetChildAlignment;
    public delegate* unmanaged<ulong, int> HorizontalLayoutGroup_GetReverseArrangement;
    public delegate* unmanaged<ulong, int, void> HorizontalLayoutGroup_SetReverseArrangement;
    public delegate* unmanaged<ulong, int> HorizontalLayoutGroup_GetControlChildWidth;
    public delegate* unmanaged<ulong, int, void> HorizontalLayoutGroup_SetControlChildWidth;
    public delegate* unmanaged<ulong, int> HorizontalLayoutGroup_GetControlChildHeight;
    public delegate* unmanaged<ulong, int, void> HorizontalLayoutGroup_SetControlChildHeight;
    public delegate* unmanaged<ulong, int> HorizontalLayoutGroup_GetUseChildScaleWidth;
    public delegate* unmanaged<ulong, int, void> HorizontalLayoutGroup_SetUseChildScaleWidth;
    public delegate* unmanaged<ulong, int> HorizontalLayoutGroup_GetUseChildScaleHeight;
    public delegate* unmanaged<ulong, int, void> HorizontalLayoutGroup_SetUseChildScaleHeight;
    public delegate* unmanaged<ulong, int> HorizontalLayoutGroup_GetChildForceExpandWidth;
    public delegate* unmanaged<ulong, int, void> HorizontalLayoutGroup_SetChildForceExpandWidth;
    public delegate* unmanaged<ulong, int> HorizontalLayoutGroup_GetChildForceExpandHeight;
    public delegate* unmanaged<ulong, int, void> HorizontalLayoutGroup_SetChildForceExpandHeight;

    // ── UI: VerticalLayoutGroup ───────────────────────────────────
    public delegate* unmanaged<ulong, float> VerticalLayoutGroup_GetPaddingLeft;
    public delegate* unmanaged<ulong, float, void> VerticalLayoutGroup_SetPaddingLeft;
    public delegate* unmanaged<ulong, float> VerticalLayoutGroup_GetPaddingRight;
    public delegate* unmanaged<ulong, float, void> VerticalLayoutGroup_SetPaddingRight;
    public delegate* unmanaged<ulong, float> VerticalLayoutGroup_GetPaddingTop;
    public delegate* unmanaged<ulong, float, void> VerticalLayoutGroup_SetPaddingTop;
    public delegate* unmanaged<ulong, float> VerticalLayoutGroup_GetPaddingBottom;
    public delegate* unmanaged<ulong, float, void> VerticalLayoutGroup_SetPaddingBottom;
    public delegate* unmanaged<ulong, float> VerticalLayoutGroup_GetSpacing;
    public delegate* unmanaged<ulong, float, void> VerticalLayoutGroup_SetSpacing;
    public delegate* unmanaged<ulong, int> VerticalLayoutGroup_GetChildAlignment;
    public delegate* unmanaged<ulong, int, void> VerticalLayoutGroup_SetChildAlignment;
    public delegate* unmanaged<ulong, int> VerticalLayoutGroup_GetReverseArrangement;
    public delegate* unmanaged<ulong, int, void> VerticalLayoutGroup_SetReverseArrangement;
    public delegate* unmanaged<ulong, int> VerticalLayoutGroup_GetControlChildWidth;
    public delegate* unmanaged<ulong, int, void> VerticalLayoutGroup_SetControlChildWidth;
    public delegate* unmanaged<ulong, int> VerticalLayoutGroup_GetControlChildHeight;
    public delegate* unmanaged<ulong, int, void> VerticalLayoutGroup_SetControlChildHeight;
    public delegate* unmanaged<ulong, int> VerticalLayoutGroup_GetUseChildScaleWidth;
    public delegate* unmanaged<ulong, int, void> VerticalLayoutGroup_SetUseChildScaleWidth;
    public delegate* unmanaged<ulong, int> VerticalLayoutGroup_GetUseChildScaleHeight;
    public delegate* unmanaged<ulong, int, void> VerticalLayoutGroup_SetUseChildScaleHeight;
    public delegate* unmanaged<ulong, int> VerticalLayoutGroup_GetChildForceExpandWidth;
    public delegate* unmanaged<ulong, int, void> VerticalLayoutGroup_SetChildForceExpandWidth;
    public delegate* unmanaged<ulong, int> VerticalLayoutGroup_GetChildForceExpandHeight;
    public delegate* unmanaged<ulong, int, void> VerticalLayoutGroup_SetChildForceExpandHeight;

    // ── UI: GridLayoutGroup ───────────────────────────────────────
    public delegate* unmanaged<ulong, float> GridLayoutGroup_GetPaddingLeft;
    public delegate* unmanaged<ulong, float, void> GridLayoutGroup_SetPaddingLeft;
    public delegate* unmanaged<ulong, float> GridLayoutGroup_GetPaddingRight;
    public delegate* unmanaged<ulong, float, void> GridLayoutGroup_SetPaddingRight;
    public delegate* unmanaged<ulong, float> GridLayoutGroup_GetPaddingTop;
    public delegate* unmanaged<ulong, float, void> GridLayoutGroup_SetPaddingTop;
    public delegate* unmanaged<ulong, float> GridLayoutGroup_GetPaddingBottom;
    public delegate* unmanaged<ulong, float, void> GridLayoutGroup_SetPaddingBottom;
    public delegate* unmanaged<ulong, float*, float*, void> GridLayoutGroup_GetCellSize;
    public delegate* unmanaged<ulong, float, float, void> GridLayoutGroup_SetCellSize;
    public delegate* unmanaged<ulong, float*, float*, void> GridLayoutGroup_GetSpacing;
    public delegate* unmanaged<ulong, float, float, void> GridLayoutGroup_SetSpacing;
    public delegate* unmanaged<ulong, int> GridLayoutGroup_GetStartCorner;
    public delegate* unmanaged<ulong, int, void> GridLayoutGroup_SetStartCorner;
    public delegate* unmanaged<ulong, int> GridLayoutGroup_GetStartAxis;
    public delegate* unmanaged<ulong, int, void> GridLayoutGroup_SetStartAxis;
    public delegate* unmanaged<ulong, int> GridLayoutGroup_GetChildAlignment;
    public delegate* unmanaged<ulong, int, void> GridLayoutGroup_SetChildAlignment;
    public delegate* unmanaged<ulong, int> GridLayoutGroup_GetConstraint;
    public delegate* unmanaged<ulong, int, void> GridLayoutGroup_SetConstraint;
    public delegate* unmanaged<ulong, int> GridLayoutGroup_GetConstraintCount;
    public delegate* unmanaged<ulong, int, void> GridLayoutGroup_SetConstraintCount;
    public delegate* unmanaged<ulong, int> GridLayoutGroup_GetReverse;
    public delegate* unmanaged<ulong, int, void> GridLayoutGroup_SetReverse;

    // ── UI: ContentSizeFitter ─────────────────────────────────────
    public delegate* unmanaged<ulong, int> ContentSizeFitter_GetHorizontalFit;
    public delegate* unmanaged<ulong, int, void> ContentSizeFitter_SetHorizontalFit;
    public delegate* unmanaged<ulong, int> ContentSizeFitter_GetVerticalFit;
    public delegate* unmanaged<ulong, int, void> ContentSizeFitter_SetVerticalFit;
    public delegate* unmanaged<ulong, float> ContentSizeFitter_GetPaddingLeft;
    public delegate* unmanaged<ulong, float, void> ContentSizeFitter_SetPaddingLeft;
    public delegate* unmanaged<ulong, float> ContentSizeFitter_GetPaddingRight;
    public delegate* unmanaged<ulong, float, void> ContentSizeFitter_SetPaddingRight;
    public delegate* unmanaged<ulong, float> ContentSizeFitter_GetPaddingTop;
    public delegate* unmanaged<ulong, float, void> ContentSizeFitter_SetPaddingTop;
    public delegate* unmanaged<ulong, float> ContentSizeFitter_GetPaddingBottom;
    public delegate* unmanaged<ulong, float, void> ContentSizeFitter_SetPaddingBottom;

    // ── UI: WidthConstraint ───────────────────────────────────────
    public delegate* unmanaged<ulong, float> WidthConstraint_GetMinWidth;
    public delegate* unmanaged<ulong, float, void> WidthConstraint_SetMinWidth;
    public delegate* unmanaged<ulong, float> WidthConstraint_GetMaxWidth;
    public delegate* unmanaged<ulong, float, void> WidthConstraint_SetMaxWidth;

    // ── CPU / JobSystem tuning (appended for binary compat; order must match ScriptGlue.hpp) ──
    public delegate* unmanaged<int> Application_GetProcessorCount;
    public delegate* unmanaged<int, int> JobSystem_Reconfigure;

    // ── SpriteRenderer filter (appended for binary compat) ──
    public delegate* unmanaged<ulong, int> SpriteRenderer_GetFilter;
    public delegate* unmanaged<ulong, int, void> SpriteRenderer_SetFilter;

    // Two-call buffer pattern; empty = full texture (default).
    public delegate* unmanaged<ulong, byte*, int, int>     SpriteRenderer_GetSpriteNameBuffer;
    public delegate* unmanaged<ulong, byte*, void>         SpriteRenderer_SetSpriteName;

    // ── Dynamic component registration (appended for binary compat) ──
    // Called per IComponent struct at assembly load; no codegen needed. Returns assigned typeId (0 on failure). Category: 0=Component, 1=Tag.
    public delegate* unmanaged<byte*, byte*, byte*, uint, uint, uint, uint> Component_RegisterDynamic;
    public delegate* unmanaged<void> Component_UnregisterAllDynamic;

    // ── Scene load-by-GUID (appended for binary compat) ──
    public delegate* unmanaged<ulong, int>  Scene_LoadByGuid;
    public delegate* unmanaged<ulong, int>  Scene_LoadAdditiveByGuid;
    public delegate* unmanaged<ulong, void> Scene_UnloadByGuid;
    public delegate* unmanaged<ulong, int>  Scene_SetActiveByGuid;
    public delegate* unmanaged<ulong, int>  Scene_ReloadByGuid;
    public delegate* unmanaged<ulong, int>  Scene_DoesSceneExistByGuid;
    public delegate* unmanaged<ulong>       Scene_GetActiveSceneGuid;

    // ── Rigidbody2D motion locks (appended for binary compat) ──
    // Bools travel as int (0/1) to avoid platform-ABI ambiguity on bool width across the FFI.
    public delegate* unmanaged<ulong, int>          Rigidbody2D_GetFreezePositionX;
    public delegate* unmanaged<ulong, int, void>    Rigidbody2D_SetFreezePositionX;
    public delegate* unmanaged<ulong, int>          Rigidbody2D_GetFreezePositionY;
    public delegate* unmanaged<ulong, int, void>    Rigidbody2D_SetFreezePositionY;
    public delegate* unmanaged<ulong, int>          Rigidbody2D_GetFreezeRotation;
    public delegate* unmanaged<ulong, int, void>    Rigidbody2D_SetFreezeRotation;

    // ── ParticleSystem2D texture (appended for binary compat) ──
    public delegate* unmanaged<ulong, ulong>        ParticleSystem2D_GetTexture;
    public delegate* unmanaged<ulong, ulong, void>  ParticleSystem2D_SetTexture;

    // ── Application data path (appended for binary compat) ──
    public delegate* unmanaged<byte*, int, int>     Application_GetDataPathBuffer;

    // ECS ref-API entity rows (appended for binary compat).
    public delegate* unmanaged<byte*, byte*, byte*, byte*, byte*, int, void**, ulong*, int, int> Scene_OpenQueryViewWithEntities;

    // ── DataAsset (appended for binary compat) ──
    public delegate* unmanaged<ulong, int> DataAsset_EnsureLoaded;

    // ── Application name/version/company (appended for binary compat; keep in lock-step with C++ NativeBindings) ──
    public delegate* unmanaged<byte*, int, int> Application_GetNameBuffer;
    public delegate* unmanaged<byte*, int, int> Application_GetVersionBuffer;
    public delegate* unmanaged<byte*, int, int> Application_GetCompanyBuffer;

    // ── Gizmos: filled + text (appended for binary compat; keep in lock-step with C++ NativeBindings) ──
    public delegate* unmanaged<float, float, float, float, float, void> Gizmo_DrawSquare;
    public delegate* unmanaged<float, float, float, int, void> Gizmo_DrawCircle;
    public delegate* unmanaged<byte*, float, float, float, float, void> Gizmo_DrawText;

    // ── UI: hovered entity (appended for binary compat; keep in lock-step with C++ NativeBindings) ──
    public delegate* unmanaged<ulong*, int> UI_GetHoveredEntity;

    // RectTransform screen position (appended for binary compat; keep in lock-step with C++ NativeBindings).
    public delegate* unmanaged<ulong, float*, float*, void> RectTransform_GetScreenPosition;
    public delegate* unmanaged<ulong, float, float, void> RectTransform_SetScreenPosition;

    // InputField multiline toggle (appended for binary compat; keep in lock-step with C++ NativeBindings).
    public delegate* unmanaged<ulong, int> InputField_GetMultiline;
    public delegate* unmanaged<ulong, int, void> InputField_SetMultiline;
    public delegate* unmanaged<ulong, ulong> InputField_GetVerticalScrollbarEntity;
    public delegate* unmanaged<ulong, ulong, void> InputField_SetVerticalScrollbarEntity;

    // Gizmo enable toggle (appended for binary compat; keep in lock-step with C++ NativeBindings).
    public delegate* unmanaged<int> Gizmo_GetEnabled;
    public delegate* unmanaged<int, void> Gizmo_SetEnabled;

    // Application restart (appended for binary compat; keep in lock-step with C++ NativeBindings).
    public delegate* unmanaged<void> Application_Restart;

    public delegate* unmanaged<float*, int*, int*, int*, int*, void> RenderSettings_Get;
    public delegate* unmanaged<float, int, int, int, int, int> RenderSettings_Apply;
    public delegate* unmanaged<int> RenderSettings_ResetToDefaults;

    // ── Gamepad (appended for binary compat) ──
    public delegate* unmanaged<int, int> Input_IsGamepadConnected;
    public delegate* unmanaged<int, int, int> Input_GetGamepadButton;
    public delegate* unmanaged<int, int, int> Input_GetGamepadButtonDown;
    public delegate* unmanaged<int, int, int> Input_GetGamepadButtonUp;
    public delegate* unmanaged<int, int, float> Input_GetGamepadAxis;

    // ── Collider settings + query layer mask (appended for binary compat) ──
    public delegate* unmanaged<ulong, void> Physics2D_SetQueryLayerMask;
    public delegate* unmanaged<ulong, int, int> Collider2D_GetSensor;
    public delegate* unmanaged<ulong, int, int, void> Collider2D_SetSensor;
    public delegate* unmanaged<ulong, int, ulong> Collider2D_GetLayer;
    public delegate* unmanaged<ulong, int, ulong, void> Collider2D_SetLayer;
    public delegate* unmanaged<ulong, int, ulong> Collider2D_GetCollisionMask;
    public delegate* unmanaged<ulong, int, ulong, void> Collider2D_SetCollisionMask;
    public delegate* unmanaged<ulong, int, float> Collider2D_GetFriction;
    public delegate* unmanaged<ulong, int, float, void> Collider2D_SetFriction;
    public delegate* unmanaged<ulong, int, float> Collider2D_GetBounciness;
    public delegate* unmanaged<ulong, int, float, void> Collider2D_SetBounciness;

    // ── SpriteAnimator2D (appended for binary compat) ──
    public delegate* unmanaged<ulong, int, void> SpriteAnimator2D_Command;
    public delegate* unmanaged<ulong, int> SpriteAnimator2D_IsPlaying;
    public delegate* unmanaged<ulong, int> SpriteAnimator2D_GetFrame;
    public delegate* unmanaged<ulong, int, void> SpriteAnimator2D_SetFrame;
    public delegate* unmanaged<ulong, float> SpriteAnimator2D_GetFramesPerSecond;
    public delegate* unmanaged<ulong, float, void> SpriteAnimator2D_SetFramesPerSecond;

    // ── PostProcessing2D (appended for binary compat) ──
    public delegate* unmanaged<ulong, int, void*, int, int> PostProcessing2D_GetEffect;
    public delegate* unmanaged<ulong, int, void*, int, int> PostProcessing2D_SetEffect;

    // ── Safe area (appended for binary compat) ──
    public delegate* unmanaged<float*, float*, float*, float*, void> Application_GetSafeAreaInsets;

    // ── Read-only ECS ref (appended for binary compat) ──
    public delegate* unmanaged<ulong, byte*, void*> Entity_GetComponentPtrReadOnly;

    // ── 3D components (appended for binary compat) ──
    public delegate* unmanaged<ulong, float*, float*, float*, void> Transform3D_GetPosition;
    public delegate* unmanaged<ulong, float, float, float, void> Transform3D_SetPosition;
    public delegate* unmanaged<ulong, float*, float*, float*, void> Transform3D_GetRotation;
    public delegate* unmanaged<ulong, float, float, float, void> Transform3D_SetRotation;
    public delegate* unmanaged<ulong, float*, float*, float*, void> Transform3D_GetScale;
    public delegate* unmanaged<ulong, float, float, float, void> Transform3D_SetScale;
    public delegate* unmanaged<ulong, float*, float*, float*, void> Transform3D_GetWorldPosition;
    public delegate* unmanaged<ulong, float*, float*, float*, void> Transform3D_GetForward;
    public delegate* unmanaged<ulong, int> Camera3D_GetProjection;
    public delegate* unmanaged<ulong, int, void> Camera3D_SetProjection;
    public delegate* unmanaged<ulong, float> Camera3D_GetFieldOfView;
    public delegate* unmanaged<ulong, float, void> Camera3D_SetFieldOfView;
    public delegate* unmanaged<ulong, float> Camera3D_GetOrthographicSize;
    public delegate* unmanaged<ulong, float, void> Camera3D_SetOrthographicSize;
    public delegate* unmanaged<ulong, float> Camera3D_GetNearClip;
    public delegate* unmanaged<ulong, float, void> Camera3D_SetNearClip;
    public delegate* unmanaged<ulong, float> Camera3D_GetFarClip;
    public delegate* unmanaged<ulong, float, void> Camera3D_SetFarClip;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> Camera3D_GetClearColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> Camera3D_SetClearColor;
    public delegate* unmanaged<ulong> Camera3D_GetMainEntity;
    public delegate* unmanaged<ulong, int> MeshRenderer_GetMesh;
    public delegate* unmanaged<ulong, int, void> MeshRenderer_SetMesh;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> MeshRenderer_GetColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> MeshRenderer_SetColor;
    public delegate* unmanaged<ulong, int> MeshRenderer_GetLit;
    public delegate* unmanaged<ulong, int, void> MeshRenderer_SetLit;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> DirectionalLight_GetColor;
    public delegate* unmanaged<ulong, float, float, float, float, void> DirectionalLight_SetColor;
    public delegate* unmanaged<ulong, float> DirectionalLight_GetIntensity;
    public delegate* unmanaged<ulong, float, void> DirectionalLight_SetIntensity;
    public delegate* unmanaged<ulong, float*, float*, float*, float*, void> DirectionalLight_GetAmbient;
    public delegate* unmanaged<ulong, float, float, float, float, void> DirectionalLight_SetAmbient;

    // ── 3D materials, lights, sky, 3D graphics settings (appended for binary compat) ──
    public delegate* unmanaged<ulong, ulong> MeshRenderer_GetMaterial;
    public delegate* unmanaged<ulong, ulong, void> MeshRenderer_SetMaterial;
    public delegate* unmanaged<ulong, int> MeshRenderer_GetCastShadows;
    public delegate* unmanaged<ulong, int, void> MeshRenderer_SetCastShadows;
    public delegate* unmanaged<ulong, int> DirectionalLight_GetCastShadows;
    public delegate* unmanaged<ulong, int, void> DirectionalLight_SetCastShadows;
    public delegate* unmanaged<ulong, float*, int, int> Material_Get;
    public delegate* unmanaged<ulong, float*, int, void> Material_Set;
    public delegate* unmanaged<ulong, ulong> Material_GetTexture;
    public delegate* unmanaged<ulong, ulong, void> Material_SetTexture;
    public delegate* unmanaged<ulong, float*, int, void> PointLight_Get;
    public delegate* unmanaged<ulong, float*, int, void> PointLight_Set;
    public delegate* unmanaged<ulong, float*, int, void> SpotLight_Get;
    public delegate* unmanaged<ulong, float*, int, void> SpotLight_Set;
    public delegate* unmanaged<ulong, float*, int, void> Camera3D_GetSky;
    public delegate* unmanaged<ulong, float*, int, void> Camera3D_SetSky;
    public delegate* unmanaged<int*, float*, int*, float*, int*, void> RenderSettings_Get3D;
    public delegate* unmanaged<int, float, int, float, int, int> RenderSettings_Apply3D;

    // ── Audio streams, runtime permissions (appended for binary compat) ──
    public delegate* unmanaged<byte*, uint> AudioStream_Open;
    public delegate* unmanaged<uint, void> AudioStream_Close;
    public delegate* unmanaged<uint, int> AudioStream_Play;
    public delegate* unmanaged<uint, void> AudioStream_Pause;
    public delegate* unmanaged<uint, void> AudioStream_Stop;
    public delegate* unmanaged<uint, int> AudioStream_IsPlaying;
    public delegate* unmanaged<uint, int> AudioStream_IsAtEnd;
    public delegate* unmanaged<uint, float> AudioStream_GetCursor;
    public delegate* unmanaged<uint, float> AudioStream_GetLength;
    public delegate* unmanaged<uint, float, void> AudioStream_Seek;
    public delegate* unmanaged<uint, float> AudioStream_GetVolume;
    public delegate* unmanaged<uint, float, void> AudioStream_SetVolume;
    public delegate* unmanaged<uint, int> AudioStream_GetLoop;
    public delegate* unmanaged<uint, int, void> AudioStream_SetLoop;
    public delegate* unmanaged<byte*, void> Application_RequestPermission;
    public delegate* unmanaged<byte*, int> Application_GetPermissionState;

    // ── Physics 3D / Jolt (appended for binary compat) ──
    public delegate* unmanaged<ulong, float*, int, void> Rigidbody3D_Get;
    public delegate* unmanaged<ulong, float*, int, void> Rigidbody3D_Set;
    public delegate* unmanaged<ulong, int, float*, float*, float*, void> Rigidbody3D_GetVelocity;
    public delegate* unmanaged<ulong, int, float, float, float, void> Rigidbody3D_SetVelocity;
    public delegate* unmanaged<ulong, int, int, float, float, float, float, float, float, void> Rigidbody3D_AddForce;
    public delegate* unmanaged<ulong, int> Rigidbody3D_IsSleeping;
    public delegate* unmanaged<ulong, int, void> Rigidbody3D_SetSleeping;
    public delegate* unmanaged<ulong, int, float*, int, void> Collider3D_Get;
    public delegate* unmanaged<ulong, int, float*, int, void> Collider3D_Set;
    public delegate* unmanaged<float, float, float, float, float, float, float, ulong*, float*, int, int> Physics3D_Raycast;
    public delegate* unmanaged<float*, float*, float*, void> Physics3D_GetGravity;
    public delegate* unmanaged<float, float, float, void> Physics3D_SetGravity;

    // ── Background audio (appended for binary compat) ──
    public delegate* unmanaged<int, void> Audio_SetPlayInBackground;
    public delegate* unmanaged<int> Audio_GetPlayInBackground;

    // ── Image decoding (appended for binary compat) ──
    public delegate* unmanaged<byte*, int, int, byte*, int, int*, int*, int> Image_Decode;

    // ── Custom meshes (appended for binary compat) ──
    public delegate* unmanaged<ulong, ulong> MeshRenderer_GetCustomMesh;
    public delegate* unmanaged<ulong, ulong, void> MeshRenderer_SetCustomMesh;
    public delegate* unmanaged<ulong, ulong> MeshCollider3D_GetCustomMesh;
    public delegate* unmanaged<ulong, ulong, void> MeshCollider3D_SetCustomMesh;
    public delegate* unmanaged<ulong, float*, int, int> Mesh_GetInfo;

    // ── ParticleSystem3D (appended for binary compat) ──
    public delegate* unmanaged<ulong, float*, int, void> ParticleSystem3D_Get;
    public delegate* unmanaged<ulong, float*, int, void> ParticleSystem3D_Set;
    public delegate* unmanaged<ulong, int, void> ParticleSystem3D_Control;
    public delegate* unmanaged<ulong, int, void> ParticleSystem3D_Emit;
    public delegate* unmanaged<ulong, int*, int> ParticleSystem3D_GetState;
    public delegate* unmanaged<ulong, ulong> ParticleSystem3D_GetTexture;
    public delegate* unmanaged<ulong, ulong, void> ParticleSystem3D_SetTexture;

    // ── Media session (appended for binary compat) ──
    public delegate* unmanaged<byte*, byte*, byte*, float, byte*, int, int, void> Media_SetNowPlaying;
    public delegate* unmanaged<int, float, void> Media_SetPlaybackState;
    public delegate* unmanaged<float*, int> Media_PollCommand;

    // ── Runtime mesh (appended for binary compat) ──
    public delegate* unmanaged<ulong> Mesh_CreateRuntime;
    public delegate* unmanaged<ulong, float*, float*, float*, int, uint*, int, int> Mesh_SetData;
    public delegate* unmanaged<ulong, int> Mesh_DestroyRuntime;
    public delegate* unmanaged<ulong, int> Mesh_IsRuntime;

    // ── Motion sensors (appended for binary compat) ──
    public delegate* unmanaged<float*, float*, float*, void> Input_GetAcceleration;
    public delegate* unmanaged<float*, float*, float*, void> Input_GetGyroscope;
    public delegate* unmanaged<int, int> Input_IsMotionSensorAvailable;

    // ── Material shader properties (appended for binary compat) ──
    public delegate* unmanaged<ulong, ulong> Material_GetShader;
    public delegate* unmanaged<ulong, ulong, void> Material_SetShader;
    public delegate* unmanaged<ulong, byte*, float*, ulong*, int> Material_GetProperty;
    public delegate* unmanaged<ulong, byte*, int, float*, ulong, int> Material_SetProperty;
    public delegate* unmanaged<ulong, ulong> SpriteRenderer_GetMaterial;
    public delegate* unmanaged<ulong, ulong, void> SpriteRenderer_SetMaterial;

    // ── Camera3D skybox material (appended for binary compat) ──
    public delegate* unmanaged<ulong, ulong> Camera3D_GetSkyboxMaterial;
    public delegate* unmanaged<ulong, ulong, void> Camera3D_SetSkyboxMaterial;

    // ── Image 9-slice (appended for binary compat) ──
    public delegate* unmanaged<ulong, float*, int, void> Image_GetSlicing;
    public delegate* unmanaged<ulong, float*, int, void> Image_SetSlicing;

    // ── Touch (appended for binary compat) ──
    public delegate* unmanaged<int> Input_GetTouchCount;
    public delegate* unmanaged<int, long*, int*, float*, int> Input_GetTouch;

    // ── Image material (appended for binary compat) ──
    public delegate* unmanaged<ulong, ulong> Image_GetMaterial;
    public delegate* unmanaged<ulong, ulong, void> Image_SetMaterial;

    // ── Editor windows from C# (appended for binary compat) ──
    public delegate* unmanaged<byte*, int, void> EditorGUI_Text;
    public delegate* unmanaged<byte*, float, float, int> EditorGUI_Button;
    public delegate* unmanaged<byte*, int, int> EditorGUI_Toggle;
    public delegate* unmanaged<byte*, float, float, float, byte*, float> EditorGUI_Slider;
    public delegate* unmanaged<byte*, float, float, float, float, float, byte*, float> EditorGUI_VerticalSlider;
    public delegate* unmanaged<byte*, float, float, float> EditorGUI_FloatField;
    public delegate* unmanaged<byte*, byte*, int, int> EditorGUI_TextField;
    public delegate* unmanaged<float, float, float, byte*, void> EditorGUI_ProgressBar;
    public delegate* unmanaged<int, float, void> EditorGUI_Layout;
    public delegate* unmanaged<byte*, void> EditorGUI_PushId;
    public delegate* unmanaged<int, float> EditorGUI_Query;

    // ── Animator (appended for binary compat) ──
    public delegate* unmanaged<ulong, byte*, int, float, int> Animator_SetParameter;
    public delegate* unmanaged<ulong, byte*, int, float> Animator_GetParameter;
    public delegate* unmanaged<ulong, byte*, int> Animator_HasParameter;
    public delegate* unmanaged<ulong, byte*, float, float, int> Animator_Play;
    public delegate* unmanaged<ulong, int, byte*, int, int> Animator_GetStateBuffer;
    public delegate* unmanaged<ulong, float*, int, void> Animator_GetPlayback;
    public delegate* unmanaged<ulong, int, ulong> Animator_GetAsset;
    public delegate* unmanaged<ulong, int, ulong, void> Animator_SetAsset;
    public delegate* unmanaged<ulong, float> Animator_GetSpeed;
    public delegate* unmanaged<ulong, float, void> Animator_SetSpeed;

    // ── Camera3D screen <-> world (appended for binary compat) ──
    public delegate* unmanaged<ulong, float, float, float*, int, int> Camera3D_ScreenPointToRay;
    public delegate* unmanaged<ulong, float, float, float, float*, int, int> Camera3D_WorldToScreenPoint;

    // ── Dev tools (appended for binary compat) ──
    public delegate* unmanaged<byte*, int, int> Application_GetPrefsProfileBuffer;
    public delegate* unmanaged<byte*, byte*, int, int> DevConsole_Execute;
    public delegate* unmanaged<int> DevConsole_GetState;

    // ── Java bridge (appended for binary compat) ──
    public delegate* unmanaged<byte*, byte*, byte*, int> Java_Call;
    public delegate* unmanaged<int> Java_PollMessage;
    public delegate* unmanaged<byte*, int, int> Java_ReadResult;

    // ── SkinnedMeshRenderer (appended for binary compat) ──
    public delegate* unmanaged<ulong, float*, int, void> SkinnedMeshRenderer_Get;
    public delegate* unmanaged<ulong, float*, int, void> SkinnedMeshRenderer_Set;
    public delegate* unmanaged<ulong, int, ulong> SkinnedMeshRenderer_GetAsset;
    public delegate* unmanaged<ulong, int, ulong, void> SkinnedMeshRenderer_SetAsset;
    public delegate* unmanaged<ulong, byte*, float, void> SkinnedMeshRenderer_Play;
    public delegate* unmanaged<ulong, int, void> SkinnedMeshRenderer_Control;
    public delegate* unmanaged<ulong, byte*, int, int> SkinnedMeshRenderer_GetClip;
    public delegate* unmanaged<ulong, int> SkinnedMeshRenderer_GetClipCount;
    public delegate* unmanaged<ulong, int, byte*, int, int> SkinnedMeshRenderer_GetClipName;
    public delegate* unmanaged<ulong, int, float> SkinnedMeshRenderer_GetClipDuration;
    public delegate* unmanaged<ulong, byte*, float*, int, int> SkinnedMeshRenderer_GetBone;

    // ── Game localization (appended for binary compat) ──
    public delegate* unmanaged<byte*, byte*, int, int> Localization_GetBuffer;
    public delegate* unmanaged<byte*, int> Localization_HasKey;
    public delegate* unmanaged<int, byte*, int, int> Localization_GetLanguageBuffer;
    public delegate* unmanaged<byte*, int> Localization_SetLanguage;
    public delegate* unmanaged<byte*, int, int> Localization_GetLanguagesBuffer;
    public delegate* unmanaged<uint> Localization_GetRevision;
    public delegate* unmanaged<ulong, byte*, int, int> LocalizedText_GetKeyBuffer;
    public delegate* unmanaged<ulong, byte*, void> LocalizedText_SetKey;

    // ── Image slice (appended for binary compat) ──
    public delegate* unmanaged<ulong, byte*, int, int> Image_GetSpriteNameBuffer;
    public delegate* unmanaged<ulong, byte*, void> Image_SetSpriteName;
}

internal static unsafe class NativeCallbacks
{
    internal static NativeBindingsStruct Bindings;

    internal static void SetFrom(NativeBindingsStruct* native)
    {
        Bindings = *native;
    }
}
