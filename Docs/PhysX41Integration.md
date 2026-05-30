# PhysX 4.1 Integration

## M1 checklist

1. PhysX 4.1 SDK source build
   - Source: `C:\Users\jungle\Desktop\피직이\PhysX-4.1`
   - Built output: `physx\bin\win.x86_64.vc142.md`
   - Only the engine-linked `debug` and `release` outputs are copied into the engine.

2. Engine ThirdParty layout
   - Root: `KraftonEngine\ThirdParty\PhysX41`
   - Include:
     - `include\physx`
     - `include\pxshared`
   - Libraries:
     - `lib\debug`
     - `lib\release`
   - Runtime DLLs:
     - `bin\debug`
     - `bin\release`

3. Legacy package cleanup
   - PhysX is now resolved from `ThirdParty\PhysX41`.
   - `KraftonEngine.vcxproj` imports the source-built PhysX paths directly.
   - The old package-based PhysX dependency is no longer part of the engine build.

4. Include path
   - PhysX headers are resolved from:
     - `ThirdParty\PhysX41\include\physx`
     - `ThirdParty\PhysX41\include\pxshared`

5. Link path
   - `Debug|x64` uses `ThirdParty\PhysX41\lib\debug`.
   - `Release|x64`, `Game|x64`, `ObjViewDebug|x64`, and `Demo|x64` use `ThirdParty\PhysX41\lib\release`.

6. Linked libraries
   - `PhysX_64.lib`
   - `PhysXCommon_64.lib`
   - `PhysXFoundation_64.lib`
   - `PhysXCooking_64.lib`
   - `PhysXExtensions_static_64.lib`
   - `PhysXPvdSDK_static_64.lib`
   - `PhysXVehicle_static_64.lib`
   - `PhysXCharacterKinematic_static_64.lib`
   - `PhysXTask_static_64.lib`

7. Runtime DLL copy
   - `Debug|x64` copies from `ThirdParty\PhysX41\bin\debug`.
   - Release-like x64 configs copy from `ThirdParty\PhysX41\bin\release`.
   - Old package-based PhysX DLLs in `KraftonEngine\Bin\Debug` were replaced with the new source-built debug DLLs.
   - `PhysXGpu_64.dll` is not included in the minimum set because the engine currently uses CPU PhysX.

8. Project generation script
   - `Scripts\GenerateProjectFiles.py` now generates the same ThirdParty PhysX paths.
   - It no longer restores or imports the old package-based PhysX dependency.
   - `ThirdParty\PhysX41` is excluded from source scanning so the SDK header tree is not added as project items.

## Verification

- `Release|x64` builds successfully with the new ThirdParty PhysX path.
- `Debug|x64` links against the source-built PhysX `debug` configuration.

## Minimum file set

- Headers:
  - `include\physx`
  - `include\pxshared`
- Per configuration libraries:
  - the 9 libraries listed above in `lib\debug` and `lib\release`
- Per configuration runtime DLLs:
  - `PhysX_64.dll`
  - `PhysXCommon_64.dll`
  - `PhysXFoundation_64.dll`
  - `PhysXCooking_64.dll`
- Not required for the current engine link:
  - `checked`
  - `LowLevel*_static_64.lib`
  - `SceneQuery_static_64.lib`
  - `SimulationController_static_64.lib`
  - `PhysXGpu_64.dll`
  - PhysX `.pdb` files
