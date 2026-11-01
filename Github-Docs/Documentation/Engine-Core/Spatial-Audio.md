# Spatial audio and headphones

Add an **Audio Source** to an entity, assign its clip, and enable **3D Sound → Spatial**.
The source follows the entity's world position. The main Camera3D supplies the listener's
position and orientation; a 2D scene uses its main Camera2D.

Enable **HRTF (Headphones)** for binaural playback. The measured ear filters include
front/back and elevation-dependent spectral cues, interaural delay, and level differences.
Listener rotation follows the camera, including pitch and roll. No separate audio assets
or external audio DLLs are required.

```csharp
var source = GetComponent<AudioSource>();
source.Spatial = true;
source.Hrtf = true;
source.MinDistance = 1f;
source.MaxDistance = 30f;
source.Rolloff = AudioRolloff.Linear;
source.Play();

// A one-shot at a fixed world position.
ImpactSound?.PlayOneShotAt(position, hrtf: true);
```

HRTF is off by default, including when loading older scenes. It replaces `Panning`;
volume, distance attenuation, pitch, looping, and `DopplerLevel` still apply. Stereo clips
are averaged to a mono point source before the two ear filters. Prefer mono recordings
for positioned effects; leave music and UI sounds non-spatial. HRTF can be changed during
playback. Directions interpolate between measurements and filter changes blend over 10 ms.

Use stereo headphones and disable additional system/headset spatialization to avoid
processing the signal twice. This is a generic head model, so perceived front/back and
height separation varies with the listener and headphones. It does not simulate walls,
reflections, or room reverb. The KEMAR measurements cover elevation -40° to +90°; directions
below -40° use the lowest measured elevation. The output must be stereo at 8–192 kHz;
otherwise initialization warns and retains ordinary spatial panning. Low sample rates
cannot reproduce the high-frequency elevation cues.

The engine embeds the diffuse-field equalized KEMAR measurements by **Bill Gardner and
Keith Martin, MIT Media Laboratory (1994)**. See the [original dataset and terms](https://sound.media.mit.edu/resources/KEMAR.html)
and `External/kemar/LICENSE`. Recreate the generated header with:

```powershell
Invoke-WebRequest https://sound.media.mit.edu/resources/KEMAR/diffuse.zip -OutFile diffuse.zip
python scripts/audio/GenerateHrtfData.py diffuse.zip
```

The generator checks the archive's SHA-256. The committed header makes builds independent
of network access. SDK/runtime license staging includes the attribution.

Verification: `scripts/dev/Test.ps1 -Suite Engine -Filter "*HRTF*,*spatial*"` mixes actual
audio without opening an output device and checks direction spectra, ear delays, camera
orientation, resampling, short clip tails, thread handoff, component playback, and scene
serialization. `scripts/dev/Test.ps1 -Suite CSharp -Filter SpatialAudioTests` checks the
managed one-shot packing and backward-compatible overload.
