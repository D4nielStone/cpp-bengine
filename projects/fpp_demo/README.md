# FPP demo

Open `project.bengine` in the editor and use Play. WASD moves the player,
mouse movement turns the camera. The orange pickup is a
separate trigger; collecting it does not remove arena geometry.

The scene uses project-relative models, a unit-scale player and camera, and a
camera parented to `PlayerCapsule`. The player's initial yaw faces the pickup.
Movement is a velocity in world units per second; diagonal input is normalized.

## Single-image cubemap

In the camera inspector, enable Skybox and choose one image in **Cubemap image
(4 x 3 cross)**. Each tile must be square; the complete image is four tiles wide
and three tiles high. The layout is:

```text
          +Y
     -X   +Z   +X   -Z
          -Y
```

PNG and JPEG use the existing image loader. Missing or invalid images fall back
to the procedural atmosphere. Paths inside the project are saved relative to
the project and included in game exports. The demo defaults to the atmosphere.

Scenes with six legacy `skybox_faces` remain readable. A nonempty
`skybox_image` takes precedence; clearing it restores the legacy faces. Lua
exposes the new path as `Camera.skyboxImage`.

The scene edited after the release baseline is preserved as
`main.experimental.scene`. It references an unavailable GLB and `NewScript.lua`;
`main.scene` remains the self-contained release example.
